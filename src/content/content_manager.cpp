#include "content/content_manager.h"
#include "content/content_type.h"
#include "content/content_processor.h"
#include "utils/logger.h"
#include "storage/key_schema.h"
#include "utils/zstd_codec.h"
#include "utils/hkdf_helper.h"
#include "utils/hkdf_cache.h"

#include <algorithm>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <unordered_set>
#include <queue>
#include <set>
#include <sstream>

namespace themis {
namespace content {

using namespace std::chrono;

static std::string toHex(const std::string& in) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(in.size() * 2);
    for (unsigned char c : in) {
        out.push_back(hex[c >> 4]);
        out.push_back(hex[c & 0x0F]);
    }
    return out;
}

// Helper: parse category string to enum
static std::optional<ContentCategory> parseCategory(const std::string& s) {
    std::string up = s;
    std::transform(up.begin(), up.end(), up.begin(), ::toupper);
    if (up == "TEXT") return ContentCategory::TEXT;
    if (up == "IMAGE") return ContentCategory::IMAGE;
    if (up == "GEO") return ContentCategory::GEO;
    if (up == "CAD") return ContentCategory::CAD;
    if (up == "AUDIO") return ContentCategory::AUDIO;
    if (up == "STRUCTURED") return ContentCategory::STRUCTURED;
    if (up == "BINARY") return ContentCategory::BINARY;
    return std::nullopt;
}

// Build whitelist of chunk PKs ("chunks:<id>") based on filters
static std::vector<std::string> buildChunkWhitelist(
    RocksDBWrapper& storage,
    const json& filters
) {
    std::set<ContentCategory> allowedCats;
    std::unordered_set<std::string> allowedMimes;
    std::unordered_map<std::string, json> wantedMeta;
    std::unordered_set<std::string> wantedTags;
    bool hasAnyFilter = false;

    try {
        if (filters.contains("category")) {
            hasAnyFilter = true;
            if (filters["category"].is_array()) {
                for (const auto& v : filters["category"]) {
                    if (v.is_string()) {
                        auto cat = parseCategory(v.get<std::string>());
                        if (cat) allowedCats.insert(*cat);
                    } else if (v.is_number_integer()) {
                        int ci = v.get<int>();
                        if (ci >= 0 && ci <= static_cast<int>(ContentCategory::BINARY)) {
                            allowedCats.insert(static_cast<ContentCategory>(ci));
                        }
                    }
                }
            }
        }
        if (filters.contains("mime_type") && filters["mime_type"].is_array()) {
            hasAnyFilter = true;
            for (const auto& v : filters["mime_type"]) {
                if (v.is_string()) allowedMimes.insert(v.get<std::string>());
            }
        }
        if (filters.contains("metadata") && filters["metadata"].is_object()) {
            hasAnyFilter = true;
            for (auto it = filters["metadata"].begin(); it != filters["metadata"].end(); ++it) {
                wantedMeta[it.key()] = it.value();
            }
        }
        if (filters.contains("tags") && filters["tags"].is_array()) {
            hasAnyFilter = true;
            for (const auto& t : filters["tags"]) if (t.is_string()) wantedTags.insert(t.get<std::string>());
        }
    } catch (...) {}

    if (!hasAnyFilter) return {};

    // Load optional filter schema from config
    // Key: config:content_filter_schema, example: { "field_map": { "dataset": "user_metadata.dataset", "region": "user_metadata.region" } }
    std::unordered_map<std::string, std::string> fieldMap;
    try {
        auto v = storage.get("config:content_filter_schema");
        if (v) {
            std::string s(v->begin(), v->end());
            json sc = json::parse(s);
            if (sc.contains("field_map") && sc["field_map"].is_object()) {
                for (auto it = sc["field_map"].begin(); it != sc["field_map"].end(); ++it) {
                    if (it.value().is_string()) fieldMap[it.key()] = it.value().get<std::string>();
                }
            }
        }
    } catch (...) {}

    auto jsonPathEq = [](const json& j, const std::string& path, const json& expected) -> bool {
        // dotted path (no arrays)
        const json* cur = &j;
        size_t start = 0;
        while (start <= path.size()) {
            size_t dot = path.find('.', start);
            std::string key = dot == std::string::npos ? path.substr(start) : path.substr(start, dot - start);
            if (!cur->is_object() || !cur->contains(key)) return false;
            cur = &((*cur)[key]);
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
        try { return cur->dump() == expected.dump(); } catch (...) { return false; }
    };

    std::vector<std::string> whitelist;
    // Scan all content metas
    storage.scanPrefix("content:", [&](std::string_view key, std::string_view val){
        // Ignore non-meta keys like content:chunks lists by checking JSON
        try {
            std::string s(val);
            json j = json::parse(s);
            if (!j.is_object()) return true;
            // It is a meta object if it has mime_type/size_bytes etc.
            bool looksMeta = j.contains("mime_type") && j.contains("size_bytes");
            if (!looksMeta) return true;
            ContentMeta m = ContentMeta::fromJson(j);
            // category filter
            if (!allowedCats.empty() && allowedCats.count(m.category) == 0) return true;
            // mime filter
            if (!allowedMimes.empty() && allowedMimes.count(m.mime_type) == 0) return true;
            // metadata filter: only top-level user_metadata exact matches
            if (!wantedMeta.empty()) {
                bool allMatch = true;
                for (const auto& kv : wantedMeta) {
                    if (!m.user_metadata.contains(kv.first)) { allMatch = false; break; }
                    const auto& v = m.user_metadata[kv.first];
                    if (v.type() != kv.second.type()) {
                        // allow string/numeric loose comparison fallback
                        try {
                            if (v.dump() != kv.second.dump()) { allMatch = false; break; }
                        } catch (...) { allMatch = false; break; }
                    } else {
                        if (v.dump() != kv.second.dump()) { allMatch = false; break; }
                    }
                }
                if (!allMatch) return true;
            }
            // tags filter: any match
            if (!wantedTags.empty()) {
                bool any = false;
                for (const auto& t : m.tags) { if (wantedTags.count(t)) { any = true; break; } }
                if (!any) return true;
            }
            // custom filters via schema mapping: for any key present in filters but not reserved
            for (auto it = filters.begin(); it != filters.end(); ++it) {
                const std::string keyName = it.key();
                if (keyName == "category" || keyName == "mime_type" || keyName == "metadata" || keyName == "tags" || keyName == "scoring") continue;
                auto fmap = fieldMap.find(keyName);
                if (fmap == fieldMap.end()) continue; // unknown key → ignore
                const std::string& jpath = fmap->second;
                if (!jsonPathEq(j, jpath, it.value())) return true; // mismatch → reject
            }
            // This content matches → add all its chunks to whitelist
            std::string id = m.id;
            std::string lkey = std::string("content_chunks:") + id;
            auto lv = storage.get(lkey);
            if (lv) {
                try {
                    std::string ls(lv->begin(), lv->end());
                    json lj = json::parse(ls);
                    if (lj.contains("ids") && lj["ids"].is_array()) {
                        for (const auto& cid : lj["ids"]) {
                            if (cid.is_string()) whitelist.push_back(std::string("chunks:") + cid.get<std::string>());
                        }
                    }
                } catch (...) {}
            }
        } catch (...) {
            // ignore parsing errors
        }
        return true;
    });
    return whitelist;
}

json ContentMeta::toJson() const {
    return json{
        {"id", id},
        {"mime_type", mime_type},
        {"category", static_cast<int>(category)},
        {"original_filename", original_filename},
        {"size_bytes", size_bytes},
        {"compressed", compressed},
        {"compression_type", compression_type},
        {"encrypted", encrypted},
        {"encryption_type", encryption_type},
        {"created_at", created_at},
        {"modified_at", modified_at},
        {"hash_sha256", hash_sha256},
        {"text_extracted", text_extracted},
        {"chunked", chunked},
        {"indexed", indexed},
        {"chunk_count", chunk_count},
        {"embedding_dim", embedding_dim},
        {"extracted_metadata", extracted_metadata},
        {"user_metadata", user_metadata},
        {"tags", tags},
        {"parent_id", parent_id},
        {"child_ids", child_ids}
    };
}

ContentMeta ContentMeta::fromJson(const json& j) {
    ContentMeta m;
    m.id = j.value("id", "");
    m.mime_type = j.value("mime_type", "");
    m.category = j.contains("category") ? static_cast<ContentCategory>(j["category"].get<int>()) : ContentCategory::UNKNOWN;
    m.original_filename = j.value("original_filename", "");
    m.size_bytes = j.value("size_bytes", 0LL);
    m.compressed = j.value("compressed", false);
    m.compression_type = j.value("compression_type", "");
    m.encrypted = j.value("encrypted", false);
    m.encryption_type = j.value("encryption_type", "");
    m.created_at = j.value("created_at", 0LL);
    m.modified_at = j.value("modified_at", 0LL);
    m.hash_sha256 = j.value("hash_sha256", "");
    m.text_extracted = j.value("text_extracted", false);
    m.chunked = j.value("chunked", false);
    m.indexed = j.value("indexed", false);
    m.chunk_count = j.value("chunk_count", 0);
    m.embedding_dim = j.value("embedding_dim", 0);
    m.extracted_metadata = j.value("extracted_metadata", json::object());
    m.user_metadata = j.value("user_metadata", json::object());
    m.tags = j.value("tags", std::vector<std::string>{});
    m.parent_id = j.value("parent_id", "");
    m.child_ids = j.value("child_ids", std::vector<std::string>{});
    return m;
}

json ChunkMeta::toJson() const {
    return json{
        {"id", id},
        {"content_id", content_id},
        {"seq_num", seq_num},
        {"chunk_type", chunk_type},
        {"text", text},
        {"data", data},
        {"blob_ref", blob_ref},
        {"start_offset", start_offset},
        {"end_offset", end_offset},
        {"embedding", embedding},
        {"embedding_indexed", embedding_indexed},
        {"created_at", created_at}
    };
}

ChunkMeta ChunkMeta::fromJson(const json& j) {
    ChunkMeta c;
    c.id = j.value("id", "");
    c.content_id = j.value("content_id", "");
    c.seq_num = j.value("seq_num", 0);
    c.chunk_type = j.value("chunk_type", "");
    c.text = j.value("text", "");
    c.data = j.value("data", json::object());
    c.blob_ref = j.value("blob_ref", "");
    c.start_offset = j.value("start_offset", 0);
    c.end_offset = j.value("end_offset", 0);
    c.embedding = j.value("embedding", std::vector<float>{});
    c.embedding_indexed = j.value("embedding_indexed", false);
    c.created_at = j.value("created_at", 0LL);
    return c;
}

ContentManager::ContentManager(
        std::shared_ptr<RocksDBWrapper> storage,
        std::shared_ptr<VectorIndexManager> vector_index,
        std::shared_ptr<GraphIndexManager> graph_index,
        std::shared_ptr<SecondaryIndexManager> secondary_index,
        std::shared_ptr<FieldEncryption> field_encryption
) : storage_(std::move(storage))
    , vector_index_(std::move(vector_index))
    , graph_index_(std::move(graph_index))
    , secondary_index_(std::move(secondary_index))
    , field_encryption_(std::move(field_encryption))
{}

const ContentManager::Metrics& ContentManager::getMetrics() const {
    return metrics_;
}

void ContentManager::registerProcessor(std::unique_ptr<IContentProcessor> processor) {
    if (!processor) return;
    auto cats = processor->getSupportedCategories();
    if (cats.empty()) return;
    // Insert for the first supported category (current processors use single category)
    processors_[cats.front()] = std::move(processor);
}

std::string ContentManager::generateUuid() {
    static thread_local std::mt19937_64 rng{static_cast<uint64_t>(steady_clock::now().time_since_epoch().count()) ^ 0x9e3779b97f4a7c15ULL};
    auto u64 = rng();
    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << u64
        << std::hex << std::setw(16) << std::setfill('0') << rng();
    return oss.str();
}

std::string ContentManager::normalizeId(const std::string& id, const std::string& prefix) {
    if (id.rfind(prefix, 0) == 0) return id.substr(prefix.size());
    return id;
}

std::string ContentManager::computeSHA256(const std::string& blob) {
    // Placeholder: non-cryptographic hash to keep dependencies minimal for now
    std::hash<std::string> h;
    size_t a = h(blob);
    std::string raw;
    raw.reserve(16);
    for (int i = 0; i < 8; ++i) raw.push_back(static_cast<char>((a >> (i*8)) & 0xFF));
    // Mix with size and a constant
    size_t b = h(std::to_string(blob.size()) + "#themis");
    for (int i = 0; i < 8; ++i) raw.push_back(static_cast<char>((b >> (i*8)) & 0xFF));
    // Expand to 32 bytes by repeating pattern
    std::string exp = raw + raw;
    return toHex(exp);
}

std::optional<std::string> ContentManager::checkDuplicateByHash(const std::string& hash) {
    // Simple secondary lookup: store mapping hash -> content_id list (first only)
    std::string key = std::string("content_hash:") + hash;
    auto v = storage_->get(key);
    if (!v) return std::nullopt;
    try {
        std::string s(v->begin(), v->end());
        json j = json::parse(s);
        if (j.contains("ids") && j["ids"].is_array() && !j["ids"].empty()) {
            return j["ids"][0].get<std::string>();
        }
    } catch (...) {}
    return std::nullopt;
}

static ContentCategory detectCategory(const std::string& mime, const std::string& blob) {
    auto& reg = ContentTypeRegistry::instance();
    ContentType ct;
    if (!mime.empty()) {
        auto t = reg.getByMimeType(mime);
        if (t) ct = *t;
    }
    if (ct.mime_type.empty()) {
        auto t = reg.detectFromBlob(blob);
        if (t) ct = *t;
    }
    return ct.mime_type.empty() ? ContentCategory::UNKNOWN : ct.category;
}

Status ContentManager::importContent(const json& spec, const std::optional<std::string>& blob, const std::string& user_context) {
    try {
        if (!spec.is_object() || !spec.contains("content") || !spec["content"].is_object()) {
            return Status::Error("spec.content missing or invalid");
        }
        ContentMeta meta = ContentMeta::fromJson(spec["content"]);
        // ID vergeben falls nicht vorhanden
        if (meta.id.empty()) meta.id = generateUuid();
        // Blob optional speichern
        if (blob.has_value()) {
            std::string bkey = std::string("content_blob:") + meta.id;
            const std::string& bb = *blob;
            // Load content config from DB: key config:content
            bool compress = false;
            int zstd_level = 19;
            std::vector<std::string> skip_mimes = {"image/", "video/", "application/zip", "application/gzip"};
            try {
                if (auto cfgv = storage_->get("config:content")) {
                    std::string s(cfgv->begin(), cfgv->end());
                    json cj = json::parse(s);
                    compress = cj.value("compress_blobs", false);
                    zstd_level = cj.value("compression_level", 19);
                    if (cj.contains("skip_compressed_mimes") && cj["skip_compressed_mimes"].is_array()) {
                        skip_mimes.clear();
                        for (const auto& mv : cj["skip_compressed_mimes"]) if (mv.is_string()) skip_mimes.push_back(mv.get<std::string>());
                    }
                }
            } catch (...) {}

            std::string matched_skip_prefix;
            auto should_compress = [&](const std::string& mime, size_t size) -> bool {
                if (!compress) return false;
                if (size <= 4096) return false; // Skip small blobs (<4KB)
                // Skip if MIME starts with any of the skip prefixes
                for (const auto& p : skip_mimes) {
                    if (!p.empty()) {
                        if (p.back()=='/' && mime.rfind(p, 0) == 0) {
                            matched_skip_prefix = p;
                            return false; // prefix like image/
                        }
                        if (mime == p) {
                            matched_skip_prefix = p;
                            return false; // exact
                        }
                    }
                }
                return true;
            };

            std::vector<uint8_t> to_store;
            size_t original_size = bb.size();
            size_t compressed_size = original_size;
            float compression_ratio = 1.0f;
            
            if (should_compress(meta.mime_type, bb.size())) {
#ifdef THEMIS_HAS_ZSTD
                auto comp = utils::zstd_compress(reinterpret_cast<const uint8_t*>(bb.data()), bb.size(), zstd_level);
                // Only use compressed version if it actually reduces size (avoid decompression overhead for incompressible data)
                if (!comp.empty() && comp.size() < bb.size()) {
                    to_store = std::move(comp);
                    compressed_size = to_store.size();
                    compression_ratio = static_cast<float>(original_size) / static_cast<float>(compressed_size);
                    meta.compressed = true;
                    meta.compression_type = "zstd";
                    THEMIS_INFO("Content blob {} compressed: {}B -> {}B (ratio: {:.2f}x)", 
                               meta.id, original_size, compressed_size, compression_ratio);
                    // Update metrics
                    try {
                        metrics_.compressed_bytes_total.fetch_add(static_cast<uint64_t>(compressed_size));
                        metrics_.uncompressed_bytes_total.fetch_add(static_cast<uint64_t>(original_size));
                        // compression ratio tracking
                        uint64_t ratio_milli = static_cast<uint64_t>(compression_ratio * 1000.0f);
                        metrics_.comp_ratio_sum_milli.fetch_add(ratio_milli);
                        metrics_.comp_ratio_count.fetch_add(1);
                        // place into per-bucket (non-cumulative)
                        if (compression_ratio <= 1.0f) metrics_.comp_ratio_le_1.fetch_add(1);
                        else if (compression_ratio <= 1.5f) metrics_.comp_ratio_le_1_5.fetch_add(1);
                        else if (compression_ratio <= 2.0f) metrics_.comp_ratio_le_2.fetch_add(1);
                        else if (compression_ratio <= 3.0f) metrics_.comp_ratio_le_3.fetch_add(1);
                        else if (compression_ratio <= 5.0f) metrics_.comp_ratio_le_5.fetch_add(1);
                        else if (compression_ratio <= 10.0f) metrics_.comp_ratio_le_10.fetch_add(1);
                        else if (compression_ratio <= 100.0f) metrics_.comp_ratio_le_100.fetch_add(1);
                        else metrics_.comp_ratio_le_inf.fetch_add(1);
                    } catch (...) {}
                } else {
                    // Fallback to raw (compression failed or increased size)
                    to_store.assign(bb.begin(), bb.end());
                    meta.compressed = false;
                    meta.compression_type.clear();
                    if (!comp.empty()) {
                        THEMIS_INFO("Content blob {} skipped compression (would increase size: {}B -> {}B)", 
                                   meta.id, original_size, comp.size());
                    }
                }
#else
                // ZSTD not available at build time → store raw
                to_store.assign(bb.begin(), bb.end());
                meta.compressed = false;
                meta.compression_type.clear();
#endif
            } else {
                to_store.assign(bb.begin(), bb.end());
                meta.compressed = false;
                meta.compression_type.clear();
                // If compression was enabled but skipped due to MIME prefix, record skip metrics
                try {
                    if (compress && !matched_skip_prefix.empty()) {
                        metrics_.compression_skipped_total.fetch_add(1);
                        if (matched_skip_prefix == "image/" || matched_skip_prefix.rfind("image/",0)==0) metrics_.compression_skipped_image_total.fetch_add(1);
                        else if (matched_skip_prefix == "video/" || matched_skip_prefix.rfind("video/",0)==0) metrics_.compression_skipped_video_total.fetch_add(1);
                        else if (matched_skip_prefix == "application/zip" || matched_skip_prefix == "application/gzip") metrics_.compression_skipped_zip_total.fetch_add(1);
                    }
                } catch (...) {}
            }

            // Optional encryption of blob based on config:content_encryption_schema and user_context
            bool encrypt_blob = false;
            std::string encryption_key_id;
            try {
                if (auto encv = storage_->get("config:content_encryption_schema")) {
                    std::string es(encv->begin(), encv->end());
                    json ej = json::parse(es);
                    // Example schema: {"enabled":true, "key_id":"content_blob", "context":"user"}
                    encrypt_blob = ej.value("enabled", false);
                    encryption_key_id = ej.value("key_id", "content_blob");
                }
            } catch (...) {}
            if (encrypt_blob && field_encryption_) {
                // Kontextuelle Ableitung via HKDF (salt = user_context) – nutzt aktuelle Key-Version.
                // Falls user_context leer, verwende "anonymous" als Fallback
                std::string ctx = user_context.empty() ? "anonymous" : user_context;
                try {
                    auto kp = field_encryption_->getKeyProvider();
                    auto key_bytes = kp->getKey(encryption_key_id); // latest active
                    auto meta_info = kp->getKeyMetadata(encryption_key_id, 0);
                    // HKDF ableiten (info = "content_blob")
                    std::vector<uint8_t> salt(ctx.begin(), ctx.end());
                    auto derived_key = themis::utils::HKDFCache::threadLocal().derive_cached(key_bytes, salt, "content_blob", key_bytes.size());
                    auto blobEnc = field_encryption_->encryptWithKey(std::string(to_store.begin(), to_store.end()), encryption_key_id, meta_info.version, derived_key);
                    json bjson = blobEnc.toJson();
                    std::string benc = bjson.dump();
                    to_store.assign(benc.begin(), benc.end());
                    meta.encrypted = true;
                    meta.encryption_type = "AES-256-GCM";
                } catch (const std::exception& e) {
                    THEMIS_WARN("blob encryption failed: {}", e.what());
                }
            }
            if (!storage_->put(bkey, to_store)) {
                return Status::Error("failed to store blob");
            }
            meta.size_bytes = static_cast<int64_t>(bb.size());
            // If compression was not applied but compression enabled, still record uncompressed bytes total
            try {
                if (compress) {
                    // Only add uncompressed total if we didn't already add it for compressed path
                    if (!meta.compressed) metrics_.uncompressed_bytes_total.fetch_add(static_cast<uint64_t>(original_size));
                }
            } catch (...) {}
        }
        // Chunks verarbeiten
        std::vector<std::string> chunk_ids;
        int embedding_dim = 0;
        if (spec.contains("chunks") && spec["chunks"].is_array()) {
            int64_t now = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
            for (const auto& jc : spec["chunks"]) {
                ChunkMeta c = ChunkMeta::fromJson(jc);
                if (c.id.empty()) c.id = generateUuid();
                if (c.content_id.empty()) c.content_id = meta.id;
                if (c.created_at == 0) c.created_at = now;
                chunk_ids.push_back(c.id);
                // In RocksDB ablegen
                std::string ckey = std::string("chunk:") + c.id;
                std::string cjson = c.toJson().dump();
                if (!storage_->put(ckey, std::vector<uint8_t>(cjson.begin(), cjson.end()))) {
                    return Status::Error("failed to store chunk meta");
                }
                // Embedding in VectorIndex einfügen (falls vorhanden)
                if (!c.embedding.empty() && vector_index_) {
                    if (vector_index_->getDimension() == 0) {
                        (void)vector_index_->init("chunks", static_cast<int>(c.embedding.size()), VectorIndexManager::Metric::COSINE);
                    }
                    if (vector_index_->getDimension() == static_cast<int>(c.embedding.size())) {
                        BaseEntity e = BaseEntity::fromFields(
                            std::string("chunks:") + c.id,
                            BaseEntity::FieldMap{{"content_id", c.content_id}, {"seq_num", static_cast<int64_t>(c.seq_num)}, {"mime_type", meta.mime_type}, {"chunk_type", c.chunk_type}, {"embedding", c.embedding}}
                        );
                        auto st = vector_index_->addEntity(e);
                        if (!st.ok) THEMIS_WARN("Vector index addEntity failed: {}", st.message);
                        embedding_dim = static_cast<int>(c.embedding.size());
                    }
                }
            }
    }
        // Optionale Verschlüsselung von Metadaten-Feldern (vector metadata encryption)
        try {
            bool meta_encrypt_enabled = false;
            std::vector<std::string> meta_fields;
            if (auto mev = storage_->get("config:vector_metadata_encryption")) {
                std::string ms(mev->begin(), mev->end());
                auto mcfg = json::parse(ms);
                meta_encrypt_enabled = mcfg.value("enabled", false);
                if (meta_encrypt_enabled && mcfg.contains("fields") && mcfg["fields"].is_array()) {
                    for (const auto& f : mcfg["fields"]) if (f.is_string()) meta_fields.push_back(f.get<std::string>());
                }
            }
            if (meta_encrypt_enabled && field_encryption_) {
                // Derive base DEK once and then per-field HKDF
                auto kp = field_encryption_->getKeyProvider();
                // Salt = user_context (oder "anonymous")
                std::string ctx = user_context.empty() ? std::string("anonymous") : user_context;
                std::vector<uint8_t> salt(ctx.begin(), ctx.end());
                for (const auto& f : meta_fields) {
                    // Mapping: Feldname auf ContentMeta Struktur
                    // Unterstützte Felder: extracted_metadata, user_metadata, tags
                    nlohmann::json* target = nullptr;
                    if (f == "extracted_metadata") target = &meta.extracted_metadata;
                    else if (f == "user_metadata") target = &meta.user_metadata;
                    else if (f == "tags") {
                        // tags als Array -> JSON konvertieren
                        nlohmann::json arr = meta.tags;
                        target = new nlohmann::json(arr); // temporär, am Ende cleanup
                    }
                    if (!target) continue;
                    try {
                        if (target->is_null() || (target->is_object() && target->empty()) || (target->is_array() && target->empty())) {
                            if (f == "tags" && target) { delete target; }
                            continue; // nichts zu verschlüsseln
                        }
                        std::string plain = target->dump();
                        // HKDF ableiten: info = "vector_meta:" + feld
                        auto dek = kp->getKey("dek");
                        auto meta_info = kp->getKeyMetadata("dek", 0);
                        std::string info = std::string("vector_meta:") + f;
                        auto derived = themis::utils::HKDFCache::threadLocal().derive_cached(dek, salt, info, dek.size());
                        auto blobEnc = field_encryption_->encryptWithKey(plain, std::string("dek"), meta_info.version, derived);
                        std::string enc_b64 = blobEnc.toBase64();
                        // Metadaten ersetzen: <f>_encrypted + <f>_enc Flag, Original entfernen/neutralisieren
                        if (f == "extracted_metadata") {
                            meta.extracted_metadata = json::object(); // leeren
                        } else if (f == "user_metadata") {
                            meta.user_metadata = json::object();
                        } else if (f == "tags") {
                            meta.tags.clear();
                        }
                        // Wir lagern verschlüsselte Meta-Felder im allgemeinen Meta-JSON als Platzhalter unter reserved key
                        // Da ContentMeta::toJson() Felder fix zusammenstellt, hängen wir Zusatzfelder erst nachher an (siehe unten mjsonPatch)
                        // Temporär speichern in map structure
                        if (f == "tags" && target) { delete target; }
                        // Hänge verschlüsselte Strings in eine Zusatzliste (wird später gemerged)
                        if (!meta.extracted_metadata.contains("__enc_meta")) {
                            try { meta.extracted_metadata["__enc_meta"] = json::object(); } catch (...) {}
                        }
                        try {
                            meta.extracted_metadata["__enc_meta"][f + "_encrypted"] = enc_b64;
                            meta.extracted_metadata["__enc_meta"][f + "_enc"] = true;
                        } catch (...) {}
                    } catch (const std::exception& ex) {
                        THEMIS_WARN("vector metadata encryption field {} failed: {}", f, ex.what());
                        if (f == "tags" && target) { delete target; }
                    }
                }
            }
        } catch (const std::exception& ex) {
            THEMIS_WARN("vector metadata encryption processing error: {}", ex.what());
        }

    // Content-Meta aktualisieren/speichern (verschlüsselte Felder markiert)
        meta.chunk_count = static_cast<int>(chunk_ids.size());
        meta.chunked = meta.chunk_count > 0;
        if (meta.created_at == 0) meta.created_at = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
        meta.modified_at = meta.created_at;
        if (meta.embedding_dim == 0) meta.embedding_dim = embedding_dim;
        std::string mkey = std::string("content:") + meta.id;
        std::string mjson = meta.toJson().dump();
        if (!storage_->put(mkey, std::vector<uint8_t>(mjson.begin(), mjson.end()))) {
            return Status::Error("failed to store content meta");
        }
        // Chunk-Liste speichern
        std::string lkey = std::string("content_chunks:") + meta.id;
        json lj = json{{"ids", chunk_ids}};
        std::string lstr = lj.dump();
        storage_->put(lkey, std::vector<uint8_t>(lstr.begin(), lstr.end()));
        // Optionale Kanten übernehmen
        if (spec.contains("edges") && spec["edges"].is_array() && graph_index_) {
            for (const auto& je : spec["edges"]) {
                if (!je.is_object()) continue;
                BaseEntity::FieldMap fm;
                for (auto it = je.begin(); it != je.end(); ++it) {
                    const std::string key = it.key();
                    const auto& val = it.value();
                    if (val.is_string()) fm[key] = val.get<std::string>();
                    else if (val.is_number_integer()) fm[key] = static_cast<int64_t>(val.get<int64_t>());
                    else if (val.is_number_float()) fm[key] = val.get<double>();
                    else if (val.is_boolean()) fm[key] = val.get<bool>();
                }
                BaseEntity edge = BaseEntity::fromFields(std::string("graph:edge:"), std::move(fm));
                auto st = graph_index_->addEdge(edge);
                if (!st.ok) THEMIS_WARN("graph addEdge failed: {}", st.message);
            }
        }
        return Status::OK();
    } catch (const std::exception& e) {
        return Status::Error(std::string("import error: ") + e.what());
    } catch (...) {
        return Status::Error("import error");
    }
}

std::optional<ContentMeta> ContentManager::getContentMeta(const std::string& content_id, const std::string& user_context) {
    std::string id = normalizeId(content_id, "content:");
    std::string key = std::string("content:") + id;
    auto v = storage_->get(key);
    if (!v) return std::nullopt;
    try {
        std::string s(v->begin(), v->end());
        json j = json::parse(s);
        // Entschlüsselung verschlüsselter Metadaten falls vorhanden
        if (j.contains("_encrypted_meta") && field_encryption_) {
            bool meta_encrypt_enabled = false;
            std::vector<std::string> meta_fields;
            try {
                if (auto mev = storage_->get("config:vector_metadata_encryption")) {
                    std::string cfgs(mev->begin(), mev->end());
                    auto mcfg = json::parse(cfgs);
                    meta_encrypt_enabled = mcfg.value("enabled", false);
                    if (meta_encrypt_enabled && mcfg.contains("fields") && mcfg["fields"].is_array()) {
                        for (const auto& f : mcfg["fields"]) if (f.is_string()) meta_fields.push_back(f.get<std::string>());
                    }
                }
            } catch (...) { meta_encrypt_enabled = false; }
            if (meta_encrypt_enabled) {
                auto enc_section = j["_encrypted_meta"];
                std::string ctx = user_context.empty() ? std::string("anonymous") : user_context;
                std::vector<uint8_t> salt(ctx.begin(), ctx.end());
                auto kp = field_encryption_->getKeyProvider();
                auto dek = kp->getKey("dek");
                for (const auto& f : meta_fields) {
                    std::string enc_key = f + std::string("_encrypted");
                    if (!enc_section.contains(enc_key)) continue;
                    try {
                        std::string b64 = enc_section[enc_key].get<std::string>();
                        auto blob = EncryptedBlob::fromBase64(b64);
                        std::string info = std::string("vector_meta:") + f;
                        auto derived = themis::utils::HKDFCache::threadLocal().derive_cached(dek, salt, info, dek.size());
                        std::string plain = field_encryption_->decryptWithKey(blob, derived);
                        auto pj = json::parse(plain);
                        j[f] = pj; // wiederherstellen
                    } catch (const std::exception& exf) {
                        THEMIS_WARN("vector metadata decrypt field {} failed: {}", f, exf.what());
                    }
                }
                j.erase("_encrypted_meta");
            }
        }
        return ContentMeta::fromJson(j);
    } catch (...) { return std::nullopt; }
}
std::optional<std::string> ContentManager::getContentBlob(const std::string& content_id, const std::string& user_context) {
    std::string id = normalizeId(content_id, "content:");
    std::string key = std::string("content_blob:") + id;
    auto v = storage_->get(key);
    if (!v) return std::nullopt;
    // Inspect meta for compression
    auto m = getContentMeta(id);
    if (m && m->encrypted && field_encryption_) {
        // Falls user_context leer, verwende "anonymous" als Fallback für HKDF
        std::string ctx = user_context.empty() ? "anonymous" : user_context;
        try {
            // Attempt decrypt
            std::string raw(v->begin(), v->end());
            json bj = json::parse(raw);
            auto blob = EncryptedBlob::fromJson(bj);
            // Fetch key and derive contextual key same way
            auto kp = field_encryption_->getKeyProvider();
            auto key_bytes = kp->getKey(blob.key_id, blob.key_version);
            std::vector<uint8_t> salt(ctx.begin(), ctx.end());
            auto derived_key = themis::utils::HKDFCache::threadLocal().derive_cached(key_bytes, salt, "content_blob", key_bytes.size());
            std::string plain = field_encryption_->decryptWithKey(blob, derived_key);
            
            // Lazy Re-Encryption: Check if blob uses outdated key version
            bool needs_reencryption = false;
            uint32_t latest_version = 0;
            try {
                auto latest_meta = kp->getKeyMetadata(blob.key_id, 0); // version=0 -> latest
                latest_version = latest_meta.version;
                if (blob.key_version < latest_version) {
                    needs_reencryption = true;
                    THEMIS_INFO("Content blob {} uses outdated key version {} (latest: {}), triggering re-encryption", 
                                id, blob.key_version, latest_version);
                }
            } catch (...) {
                // If metadata check fails, skip re-encryption
            }
            
            if (needs_reencryption) {
                try {
                    // Re-encrypt with latest key version
                    auto latest_key = kp->getKey(blob.key_id); // no version arg -> latest
                    auto latest_derived = themis::utils::HKDFCache::threadLocal().derive_cached(latest_key, salt, "content_blob", latest_key.size());
                    auto new_blob = field_encryption_->encryptWithKey(plain, blob.key_id, latest_version, latest_derived);
                    json new_bj = new_blob.toJson();
                    std::string new_raw = new_bj.dump();
                    std::vector<uint8_t> new_store(new_raw.begin(), new_raw.end());
                    // Update storage with new encrypted version
                    if (storage_->put(key, new_store)) {
                        THEMIS_INFO("Content blob {} successfully re-encrypted to version {}", id, latest_version);
                    } else {
                        THEMIS_WARN("Failed to update storage after re-encryption for blob {}", id);
                    }
                } catch (const std::exception& re_ex) {
                    THEMIS_WARN("Re-encryption failed for blob {}: {}", id, re_ex.what());
                }
            }
            
            // Handle compression after decryption (ciphertext stored compressed? We encrypted post-compression so meta.compressed indicates original compression state)
            if (m->compressed && m->compression_type == "zstd") {
#ifdef THEMIS_HAS_ZSTD
                // Stored was compressed before encryption; decrypt returns compressed bytes now
                std::vector<uint8_t> tmp(plain.begin(), plain.end());
                auto decomp = utils::zstd_decompress(tmp);
                if (!decomp.empty()) return std::string(decomp.begin(), decomp.end());
#endif
            }
            return plain;
        } catch (const std::exception& e) {
            THEMIS_WARN("blob decrypt failed: {}", e.what());
        }
    }
    if (m && m->compressed && m->compression_type == "zstd") {
#ifdef THEMIS_HAS_ZSTD
        auto decomp = utils::zstd_decompress(*v);
        if (!decomp.empty()) return std::string(decomp.begin(), decomp.end());
        // Fallback on failure: return raw
        return std::string(v->begin(), v->end());
#else
        // Build without ZSTD: return raw bytes (client may handle)
        return std::string(v->begin(), v->end());
#endif
    }
    return std::string(v->begin(), v->end());
}

std::vector<ChunkMeta> ContentManager::getContentChunks(const std::string& content_id) {
    std::vector<ChunkMeta> out;
    std::string id = normalizeId(content_id, "content:");
    std::string lkey = std::string("content_chunks:") + id;
    auto lv = storage_->get(lkey);
    if (!lv) return out;
    std::vector<std::string> ids;
    try {
        std::string s(lv->begin(), lv->end());
        json j = json::parse(s);
        if (j.contains("ids")) ids = j["ids"].get<std::vector<std::string>>();
    } catch (...) { return out; }
    for (const auto& cid : ids) {
        auto v = storage_->get(std::string("chunk:") + cid);
        if (!v) continue;
        try {
            std::string s(v->begin(), v->end());
            json j = json::parse(s);
            out.push_back(ChunkMeta::fromJson(j));
        } catch (...) { continue; }
    }
    std::sort(out.begin(), out.end(), [](const ChunkMeta& a, const ChunkMeta& b){ return a.seq_num < b.seq_num; });
    return out;
}

std::optional<ChunkMeta> ContentManager::getChunk(const std::string& chunk_id) {
    std::string id = normalizeId(chunk_id, "chunk:");
    auto v = storage_->get(std::string("chunk:") + id);
    if (!v) return std::nullopt;
    try {
        std::string s(v->begin(), v->end());
        json j = json::parse(s);
        return ChunkMeta::fromJson(j);
    } catch (...) { return std::nullopt; }
}

std::vector<std::pair<std::string, float>> ContentManager::searchContent(
    const std::string& query_text, int k, const json& filters
) {
    std::vector<std::pair<std::string, float>> res;
    if (!vector_index_ || vector_index_->getDimension() <= 0) return res;

    // Simple text embedding via TextProcessor if available
    auto it = processors_.find(ContentCategory::TEXT);
    if (it == processors_.end()) return res;
    std::vector<float> q = it->second->generateEmbedding(query_text);
    // Optional: Build whitelist from filters to pre-filter vector search
    std::vector<std::string> whitelist = buildChunkWhitelist(*storage_, filters);
    const std::vector<std::string>* wptr = whitelist.empty() ? nullptr : &whitelist;
    auto [st, results] = vector_index_->searchKnn(q, static_cast<size_t>(k), wptr);
    if (!st.ok) return res;
    for (const auto& r : results) {
        res.emplace_back(r.pk, r.distance);
    }
    return res;
}

std::vector<std::pair<std::string, float>> ContentManager::searchWithExpansion(
    const std::string& query_text, int k, int expansion_hops, const json& filters
) {
    // 1) Basissuche (Vector Top-K)
    auto base = searchContent(query_text, k, filters);
    if (base.empty()) return base;

    // Scoring-Parameter (optional aus filters.scoring)
    double alpha = 1.0; // Gewicht für Vektor-Ähnlichkeit
    double beta = 0.0;  // reserviert (Graph-Distanz, z. B. Dijkstra)
    double gamma = 0.1; // Hop-Strafterm
    try {
        if (filters.contains("scoring")) {
            const auto& sc = filters["scoring"];
            if (sc.contains("alpha")) alpha = sc["alpha"].get<double>();
            if (sc.contains("beta")) beta = sc["beta"].get<double>();
            if (sc.contains("gamma")) gamma = sc["gamma"].get<double>();
        }
    } catch (...) {}

    // Erzeuge Map pk->score und Queue für Expansion
    std::unordered_map<std::string, double> bestScore; bestScore.reserve(base.size()*2);
    struct QItem { std::string origin; std::string node; int hop; };
    std::queue<QItem> q;

    // Metrik beachten: COSINE liefert distance = 1 - cosine → similarity = 1 - d
    auto metric = vector_index_ ? vector_index_->getMetric() : VectorIndexManager::Metric::COSINE;
    auto toSim = [&](float distance) -> double {
        if (metric == VectorIndexManager::Metric::COSINE) return 1.0 - static_cast<double>(distance);
        // L2: invertiert, grob normalisiert
        return -static_cast<double>(distance);
    };

    for (const auto& [pk, dist] : base) {
        double sim = toSim(dist);
        double score = alpha * sim; // basis ohne Graphanteil
        auto it = bestScore.find(pk);
        if (it == bestScore.end() || score > it->second) bestScore[pk] = score;
        if (expansion_hops > 0) q.push(QItem{pk, pk, 0});
    }

    // 2) Graph-Expansion (BFS bis expansion_hops)
    if (graph_index_ && expansion_hops > 0) {
        std::unordered_set<std::string> seen;
        while (!q.empty()) {
            QItem qi = q.front(); q.pop();
            if (qi.hop >= expansion_hops) continue;
            auto [st, neigh] = graph_index_->outNeighbors(qi.node);
            if (!st.ok) continue;
            int nextHop = qi.hop + 1;
            for (const auto& nb : neigh) {
                // optional: allow revisits if better path from different origin
                // Compute Dijkstra distance from origin to nb (weighted graph)
                double distCost = 0.0;
                if (beta != 0.0) {
                    auto pr = graph_index_->dijkstra(qi.origin, nb);
                    if (pr.first.ok) distCost = pr.second.totalCost; else distCost = static_cast<double>(nextHop);
                } else {
                    distCost = static_cast<double>(nextHop);
                }
                double expandedScore = - gamma * static_cast<double>(nextHop) - beta * distCost;
                auto it2 = bestScore.find(nb);
                if (it2 == bestScore.end() || expandedScore > it2->second) bestScore[nb] = expandedScore;
                q.push(QItem{qi.origin, nb, nextHop});
            }
        }
    }

    // 3) Ergebnisse zusammenstellen und nach Score sortieren
    std::vector<std::pair<std::string, float>> out;
    out.reserve(bestScore.size());
    for (const auto& kv : bestScore) {
        out.emplace_back(kv.first, static_cast<float>(kv.second));
    }
    // Optional: Post-Filter gegen erlaubte Whitelist (sichert, dass Expansion nicht aus Filters ausbricht)
    try {
        std::vector<std::string> allow = buildChunkWhitelist(*storage_, filters);
        if (!allow.empty()) {
            std::unordered_set<std::string> allowed(allow.begin(), allow.end());
            out.erase(std::remove_if(out.begin(), out.end(), [&](const auto& p){ return allowed.find(p.first) == allowed.end(); }), out.end());
        }
    } catch (...) {}

    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b){ return a.second > b.second; });
    if (out.size() > static_cast<size_t>(k)) out.resize(static_cast<size_t>(k));
    return out;
}

Status ContentManager::deleteContent(const std::string& content_id) {
    std::string id = normalizeId(content_id, "content:");
    // Load chunks
    auto chunks = getContentChunks(id);
    for (const auto& c : chunks) {
        storage_->del(std::string("chunk:") + c.id);
        // Remove vector object if present
        if (vector_index_) {
            vector_index_->removeByPk(std::string("chunks:") + c.id);
        }
    }
    storage_->del(std::string("content_chunks:") + id);
    storage_->del(std::string("content_blob:") + id);
    storage_->del(std::string("content:") + id);
    return Status::OK();
}

IContentProcessor* ContentManager::getProcessor(ContentCategory category) {
    auto it = processors_.find(category);
    if (it == processors_.end()) return nullptr;
    return it->second.get();
}

ContentManager::Stats ContentManager::getStats() {
    Stats s{};
    s.total_content_items = 0;
    s.total_chunks = 0;
    s.total_embeddings = 0;
    s.total_storage_bytes = static_cast<int64_t>(storage_->getApproximateSize());

    // naive count via scan
    storage_->scanPrefix("content:", [&](std::string_view, std::string_view){ s.total_content_items++; return true; });
    storage_->scanPrefix("chunk:", [&](std::string_view, std::string_view){ s.total_chunks++; return true; });
    // embeddings equal vector_index count if initialized
    if (vector_index_) s.total_embeddings = static_cast<int>(vector_index_->getVectorCount());
    return s;
}

} // namespace content
} // namespace themis
