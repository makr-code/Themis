#include <benchmark/benchmark.h>

#include <memory>
#include <string>
#include <vector>
#include <random>
#include <filesystem>

#include "security/mock_key_provider.h"
#include "security/encryption.h"
#include "document/encrypted_entities.h"
#include "storage/rocksdb_wrapper.h"
#include "index/secondary_index.h"
#include "storage/base_entity.h"
#include "utils/hkdf_helper.h"

using themis::MockKeyProvider;
using themis::FieldEncryption;
using themis::EncryptedField;
using themis::EncryptedBlob;
using themis::RocksDBWrapper;
using themis::SecondaryIndexManager;
using themis::BaseEntity;

namespace {

std::string makeRandomString(size_t len) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    static thread_local std::mt19937 rng{std::random_device{}()};
    static thread_local std::uniform_int_distribution<size_t> dist(0, sizeof(charset) - 2);
    std::string s; s.reserve(len);
    for (size_t i = 0; i < len; ++i) s.push_back(charset[dist(rng)]);
    return s;
}

void cleanupPath(const std::string& path) {
    std::error_code ec; std::filesystem::remove_all(path, ec);
}

struct CryptoEnv {
    std::shared_ptr<MockKeyProvider> provider;
    std::shared_ptr<FieldEncryption> enc;
    std::vector<uint8_t> user_pii_key;
    uint32_t user_pii_version{1};

    static CryptoEnv& instance() {
        static CryptoEnv env; return env;
    }

    void initOnce() {
        if (enc) return;
        provider = std::make_shared<MockKeyProvider>();
        provider->createKey("user_pii", 1);
        provider->createKey("user_sensitive", 1);
        provider->createKey("customer_financial", 1);

        enc = std::make_shared<FieldEncryption>(provider);
        EncryptedField<std::string>::setFieldEncryption(enc);
        EncryptedField<int64_t>::setFieldEncryption(enc);
        EncryptedField<double>::setFieldEncryption(enc);

        user_pii_key = provider->getKey("user_pii", 1);
        user_pii_version = 1;
    }
};

struct DBEnv {
    std::string db_path = "data/bench_encryption_db";
    std::shared_ptr<RocksDBWrapper> db;
    std::unique_ptr<SecondaryIndexManager> secIdx;

    void open() {
        cleanupPath(db_path);
        RocksDBWrapper::Config cfg; cfg.db_path = db_path; cfg.memtable_size_mb = 128; cfg.block_cache_size_mb = 256;
        db = std::make_shared<RocksDBWrapper>(cfg);
        secIdx = std::make_unique<SecondaryIndexManager>(*db);
        // Typical index on username and created_at
        secIdx->createIndex("User", "username", true);
        secIdx->createRangeIndex("User", "created_at");
    }

    void close() {
        secIdx.reset(); db.reset(); cleanupPath(db_path);
    }
};

} // namespace

// --- Pure crypto benchmarks ---

static void BM_Encrypt_String_UsingKey(benchmark::State& state) {
    auto& c = CryptoEnv::instance(); c.initOnce();
    const std::string plain = makeRandomString(static_cast<size_t>(state.range(0)));
    for (auto _ : state) {
        auto blob = c.enc->encryptWithKey(plain, "user_pii", c.user_pii_version, c.user_pii_key);
        benchmark::DoNotOptimize(blob);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Encrypt_String_UsingKey)->Arg(64)->Arg(256)->Arg(1024)->Unit(benchmark::kMicrosecond);

static void BM_Decrypt_String_UsingKey(benchmark::State& state) {
    auto& c = CryptoEnv::instance(); c.initOnce();
    const std::string plain = makeRandomString(static_cast<size_t>(state.range(0)));
    auto blob = c.enc->encryptWithKey(plain, "user_pii", c.user_pii_version, c.user_pii_key);
    for (auto _ : state) {
        auto out = c.enc->decryptWithKey(blob, c.user_pii_key);
        benchmark::DoNotOptimize(out);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Decrypt_String_UsingKey)->Arg(64)->Arg(256)->Arg(1024)->Unit(benchmark::kMicrosecond);

// --- Entity encryption + JSON serialization ---

static void BM_UserEntity_Encrypt_Serialize(benchmark::State& state) {
    auto& c = CryptoEnv::instance(); c.initOnce();
    for (auto _ : state) {
        themis::User u;
        u.id = "user_" + makeRandomString(8);
        u.username = makeRandomString(12);
        u.created_at = 1730265600000LL; // fixed timestamp
        u.status = "active";
        u.email.encrypt("alice." + makeRandomString(10) + "@example.com", "user_pii");
        u.phone.encrypt("+1-555-" + makeRandomString(4), "user_pii");
        u.ssn.encrypt("123-45-" + makeRandomString(4), "user_sensitive");
        u.address.encrypt("123 Main St, " + makeRandomString(6), "user_pii");
        auto j = u.toJson();
        auto dumped = j.dump();
        benchmark::DoNotOptimize(dumped);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_UserEntity_Encrypt_Serialize)->Unit(benchmark::kMicrosecond);

// --- RocksDB ingest of encrypted documents ---

static void BM_DB_Ingest_Encrypted(benchmark::State& state) {
    auto& c = CryptoEnv::instance(); c.initOnce();
    DBEnv db; db.open();
    const size_t N = static_cast<size_t>(state.range(0));

    for (auto _ : state) {
        size_t count = 0;
        for (size_t i = 0; i < N; ++i) {
            themis::User u;
            u.id = "user_" + std::to_string(i);
            u.username = makeRandomString(12);
            u.created_at = static_cast<int64_t>(i);
            u.status = "active";
            u.email.encrypt("u" + std::to_string(i) + "@ex.com", "user_pii");
            u.phone.encrypt("+1-555-" + std::to_string(1000 + (i % 9000)), "user_pii");
            u.ssn.encrypt("123-45-" + std::to_string(1000 + (i % 9000)), "user_sensitive");
            u.address.encrypt("Street " + std::to_string(i), "user_pii");
            auto j = u.toJson();
            auto s = j.dump();
            std::vector<uint8_t> bytes(s.begin(), s.end());
            db.db->put("user:" + u.id, bytes);
            ++count;
        }
        state.SetItemsProcessed(static_cast<int64_t>(count));
    }

    db.close();
}
// Run exactly once with 100k to capture throughput
BENCHMARK(BM_DB_Ingest_Encrypted)->Arg(100000)->Iterations(1)->Unit(benchmark::kMillisecond)->UseRealTime();

// --- Index write performance impact (with vs without encrypted payload) ---

static void BM_Index_Insert_Plain(benchmark::State& state) {
    DBEnv db; db.open();
    auto& c = CryptoEnv::instance(); c.initOnce();
    const size_t N = static_cast<size_t>(state.range(0));

    for (auto _ : state) {
        size_t count = 0;
        for (size_t i = 0; i < N; ++i) {
            themis::BaseEntity e("u_" + std::to_string(i));
            e.setField("username", makeRandomString(12));
            e.setField("created_at", static_cast<int64_t>(i));
            // No encrypted payload field
            db.secIdx->put("User", e);
            ++count;
        }
        state.SetItemsProcessed(static_cast<int64_t>(count));
    }

    db.close();
}

static void BM_Index_Insert_WithEncryptedPayload(benchmark::State& state) {
    DBEnv db; db.open();
    auto& c = CryptoEnv::instance(); c.initOnce();
    const size_t N = static_cast<size_t>(state.range(0));

    // Pre-encrypt a medium-sized blob (~300-600 bytes base64)
    const std::string medium = makeRandomString(256);
    auto blob = c.enc->encryptWithKey(medium, "user_pii", c.user_pii_version, c.user_pii_key);
    const std::string blob_b64 = blob.toBase64();

    for (auto _ : state) {
        size_t count = 0;
        for (size_t i = 0; i < N; ++i) {
            themis::BaseEntity e("u_" + std::to_string(i));
            e.setField("username", makeRandomString(12));
            e.setField("created_at", static_cast<int64_t>(i));
            // Attach non-indexed encrypted payload to simulate larger writes
            e.setField("email_enc", blob_b64);
            db.secIdx->put("User", e);
            ++count;
        }
        state.SetItemsProcessed(static_cast<int64_t>(count));
    }

    db.close();
}

// Compare at 100k rows
BENCHMARK(BM_Index_Insert_Plain)->Arg(100000)->Iterations(1)->Unit(benchmark::kMillisecond)->UseRealTime();
BENCHMARK(BM_Index_Insert_WithEncryptedPayload)->Arg(100000)->Iterations(1)->Unit(benchmark::kMillisecond)->UseRealTime();

// --- HKDF Derivation Benchmarks (Schema-based Encryption) ---

static void BM_HKDF_Derive_FieldKey(benchmark::State& state) {
    auto& c = CryptoEnv::instance(); c.initOnce();
    std::vector<uint8_t> dek = c.provider->getKey("user_pii", 1);
    std::string user_id = "user_12345";
    std::vector<uint8_t> salt(user_id.begin(), user_id.end());
    std::string info = "field:email";
    
    for (auto _ : state) {
        auto field_key = themis::utils::HKDFHelper::derive(dek, salt, info, 32);
        benchmark::DoNotOptimize(field_key);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_HKDF_Derive_FieldKey)->Unit(benchmark::kMicrosecond);

// --- Schema-based Full Field Encryption (HKDF + Encrypt) ---

static void BM_SchemaEncrypt_SingleField(benchmark::State& state) {
    auto& c = CryptoEnv::instance(); c.initOnce();
    std::vector<uint8_t> dek = c.provider->getKey("user_pii", 1);
    std::string user_id = "user_12345";
    std::string field_name = "email";
    std::string plaintext = makeRandomString(static_cast<size_t>(state.range(0)));
    
    for (auto _ : state) {
        // HKDF derivation
        std::vector<uint8_t> salt(user_id.begin(), user_id.end());
        std::string info = "field:" + field_name;
        auto field_key = themis::utils::HKDFHelper::derive(dek, salt, info, 32);
        
        // Encryption
        std::vector<uint8_t> plain_bytes(plaintext.begin(), plaintext.end());
        auto blob = c.enc->encryptWithKey(plain_bytes, "field:" + field_name, 1, field_key);
        benchmark::DoNotOptimize(blob);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SchemaEncrypt_SingleField)->Arg(64)->Arg(256)->Arg(1024)->Unit(benchmark::kMicrosecond);

// --- Schema-based Full Field Decryption (HKDF + Decrypt) ---

static void BM_SchemaDecrypt_SingleField(benchmark::State& state) {
    auto& c = CryptoEnv::instance(); c.initOnce();
    std::vector<uint8_t> dek = c.provider->getKey("user_pii", 1);
    std::string user_id = "user_12345";
    std::string field_name = "email";
    std::string plaintext = makeRandomString(static_cast<size_t>(state.range(0)));
    
    // Pre-encrypt
    std::vector<uint8_t> salt(user_id.begin(), user_id.end());
    std::string info = "field:" + field_name;
    auto field_key = themis::utils::HKDFHelper::derive(dek, salt, info, 32);
    std::vector<uint8_t> plain_bytes(plaintext.begin(), plaintext.end());
    auto blob = c.enc->encryptWithKey(plain_bytes, "field:" + field_name, 1, field_key);
    
    for (auto _ : state) {
        // HKDF derivation (same as encrypt)
        auto derived_key = themis::utils::HKDFHelper::derive(dek, salt, info, 32);
        
        // Decryption
        auto decrypted_bytes = c.enc->decryptWithKey(blob, derived_key);
        benchmark::DoNotOptimize(decrypted_bytes);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SchemaDecrypt_SingleField)->Arg(64)->Arg(256)->Arg(1024)->Unit(benchmark::kMicrosecond);

// --- Multi-Field Entity with Schema Encryption (Realistic Scenario) ---

static void BM_SchemaEncrypt_MultiField_Entity(benchmark::State& state) {
    auto& c = CryptoEnv::instance(); c.initOnce();
    std::vector<uint8_t> dek = c.provider->getKey("user_pii", 1);
    std::string user_id = "user_12345";
    
    std::vector<std::string> fields = {"email", "phone", "ssn", "address"};
    std::vector<std::string> plaintexts = {
        "alice@example.com",
        "+1-555-1234",
        "123-45-6789",
        "123 Main St, Anytown USA"
    };
    
    for (auto _ : state) {
        std::vector<EncryptedBlob> blobs;
        blobs.reserve(fields.size());
        
        for (size_t i = 0; i < fields.size(); ++i) {
            std::vector<uint8_t> salt(user_id.begin(), user_id.end());
            std::string info = "field:" + fields[i];
            auto field_key = themis::utils::HKDFHelper::derive(dek, salt, info, 32);
            std::vector<uint8_t> plain_bytes(plaintexts[i].begin(), plaintexts[i].end());
            blobs.push_back(c.enc->encryptWithKey(plain_bytes, "field:" + fields[i], 1, field_key));
        }
        
        benchmark::DoNotOptimize(blobs);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SchemaEncrypt_MultiField_Entity)->Unit(benchmark::kMicrosecond);

// --- Vector<float> Encryption (Embeddings) ---

static void BM_VectorFloat_Encryption(benchmark::State& state) {
    auto& c = CryptoEnv::instance(); c.initOnce();
    std::vector<uint8_t> dek = c.provider->getKey("user_pii", 1);
    std::string user_id = "user_12345";
    std::string field_name = "embedding";
    
    // 768-dim embedding (typical BERT size)
    std::vector<float> embedding(768);
    for (size_t i = 0; i < 768; ++i) embedding[i] = static_cast<float>(i) * 0.001f;
    
    // Serialize to JSON
    nlohmann::json j_arr = nlohmann::json::array();
    for (float val : embedding) j_arr.push_back(val);
    std::string json_str = j_arr.dump();
    std::vector<uint8_t> plain_bytes(json_str.begin(), json_str.end());
    
    for (auto _ : state) {
        std::vector<uint8_t> salt(user_id.begin(), user_id.end());
        std::string info = "field:" + field_name;
        auto field_key = themis::utils::HKDFHelper::derive(dek, salt, info, 32);
        auto blob = c.enc->encryptWithKey(plain_bytes, "field:" + field_name, 1, field_key);
        benchmark::DoNotOptimize(blob);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_VectorFloat_Encryption)->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
