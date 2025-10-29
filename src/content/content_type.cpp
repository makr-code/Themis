#include "content/content_type.h"
#include <algorithm>
#include <cctype>

namespace themis {
namespace content {

// ============================================================================
// ContentType Serialization
// ============================================================================

json ContentType::toJson() const {
    json j = {
        {"mime_type", mime_type},
        {"category", static_cast<int>(category)},
        {"extensions", extensions},
        {"supports_text_extraction", supports_text_extraction},
        {"supports_embedding", supports_embedding},
        {"supports_chunking", supports_chunking},
        {"supports_metadata_extraction", supports_metadata_extraction},
        {"binary_storage_required", binary_storage_required},
        {"features", {
            {"geospatial", features.geospatial},
            {"temporal", features.temporal},
            {"hierarchical", features.hierarchical},
            {"versioned", features.versioned},
            {"multimodal", features.multimodal}
        }}
    };
    return j;
}

ContentType ContentType::fromJson(const json& j) {
    ContentType type;
    type.mime_type = j["mime_type"];
    type.category = static_cast<ContentCategory>(j["category"].get<int>());
    type.extensions = j["extensions"].get<std::vector<std::string>>();
    type.supports_text_extraction = j["supports_text_extraction"];
    type.supports_embedding = j["supports_embedding"];
    type.supports_chunking = j["supports_chunking"];
    type.supports_metadata_extraction = j["supports_metadata_extraction"];
    type.binary_storage_required = j["binary_storage_required"];
    
    if (j.contains("features")) {
        type.features.geospatial = j["features"]["geospatial"];
        type.features.temporal = j["features"]["temporal"];
        type.features.hierarchical = j["features"]["hierarchical"];
        type.features.versioned = j["features"]["versioned"];
        type.features.multimodal = j["features"]["multimodal"];
    }
    
    return type;
}

// ============================================================================
// ContentTypeRegistry Implementation
// ============================================================================

ContentTypeRegistry& ContentTypeRegistry::instance() {
    static ContentTypeRegistry registry;
    return registry;
}

ContentTypeRegistry::ContentTypeRegistry() {
    registerDefaultTypes();
}

void ContentTypeRegistry::registerType(const ContentType& type) {
    types_.push_back(type);
}

const ContentType* ContentTypeRegistry::getByMimeType(const std::string& mime_type) const {
    for (const auto& type : types_) {
        if (type.mime_type == mime_type) {
            return &type;
        }
    }
    return nullptr;
}

const ContentType* ContentTypeRegistry::getByExtension(const std::string& extension) const {
    std::string ext_lower = extension;
    std::transform(ext_lower.begin(), ext_lower.end(), ext_lower.begin(), ::tolower);
    
    // Ensure extension starts with dot
    if (!ext_lower.empty() && ext_lower[0] != '.') {
        ext_lower = "." + ext_lower;
    }
    
    for (const auto& type : types_) {
        for (const auto& type_ext : type.extensions) {
            std::string type_ext_lower = type_ext;
            std::transform(type_ext_lower.begin(), type_ext_lower.end(), type_ext_lower.begin(), ::tolower);
            
            if (type_ext_lower == ext_lower) {
                return &type;
            }
        }
    }
    return nullptr;
}

const ContentType* ContentTypeRegistry::detectFromBlob(const std::string& blob) const {
    if (blob.empty()) {
        return nullptr;
    }
    
    // Check magic bytes for common formats
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(blob.data());
    
    // PDF: %PDF-
    if (blob.size() >= 5 && blob.substr(0, 5) == "%PDF-") {
        return getByMimeType("application/pdf");
    }
    
    // PNG: 89 50 4E 47 0D 0A 1A 0A
    if (blob.size() >= 8 && bytes[0] == 0x89 && bytes[1] == 0x50 && 
        bytes[2] == 0x4E && bytes[3] == 0x47) {
        return getByMimeType("image/png");
    }
    
    // JPEG: FF D8 FF
    if (blob.size() >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF) {
        return getByMimeType("image/jpeg");
    }
    
    // GIF: GIF87a or GIF89a
    if (blob.size() >= 6 && blob.substr(0, 3) == "GIF" && 
        (blob.substr(3, 3) == "87a" || blob.substr(3, 3) == "89a")) {
        return getByMimeType("image/gif");
    }
    
    // ZIP: 50 4B 03 04 (also used by DOCX, XLSX, etc.)
    if (blob.size() >= 4 && bytes[0] == 0x50 && bytes[1] == 0x4B && 
        bytes[2] == 0x03 && bytes[3] == 0x04) {
        return getByMimeType("application/zip");
    }
    
    // GeoJSON: starts with { (JSON)
    if (blob.size() >= 10 && blob[0] == '{') {
        // Check if it looks like GeoJSON
        if (blob.find("\"type\"") != std::string::npos && 
            (blob.find("\"Feature\"") != std::string::npos || 
             blob.find("\"FeatureCollection\"") != std::string::npos)) {
            return getByMimeType("application/geo+json");
        }
        return getByMimeType("application/json");
    }
    
    // GPX: starts with <?xml
    if (blob.size() >= 20 && blob.substr(0, 5) == "<?xml") {
        if (blob.find("<gpx") != std::string::npos) {
            return getByMimeType("application/gpx+xml");
        }
        return getByMimeType("application/xml");
    }
    
    // CSV: heuristic (starts with alphanumeric, contains commas)
    if (blob.size() >= 10 && std::isalnum(blob[0])) {
        size_t first_line_end = blob.find('\n');
        if (first_line_end != std::string::npos && first_line_end < 1000) {
            std::string first_line = blob.substr(0, first_line_end);
            if (std::count(first_line.begin(), first_line.end(), ',') >= 2) {
                return getByMimeType("text/csv");
            }
        }
    }
    
    // Default to text/plain if mostly printable ASCII
    if (blob.size() < 10000) { // Only check small files
        int printable_count = 0;
        for (size_t i = 0; i < std::min(blob.size(), size_t(1000)); i++) {
            if (std::isprint(blob[i]) || blob[i] == '\n' || blob[i] == '\r' || blob[i] == '\t') {
                printable_count++;
            }
        }
        if (printable_count > 900) { // >90% printable
            return getByMimeType("text/plain");
        }
    }
    
    return nullptr; // Unknown type
}

std::vector<const ContentType*> ContentTypeRegistry::getByCategory(ContentCategory category) const {
    std::vector<const ContentType*> result;
    for (const auto& type : types_) {
        if (type.category == category) {
            result.push_back(&type);
        }
    }
    return result;
}

std::vector<const ContentType*> ContentTypeRegistry::getAllTypes() const {
    std::vector<const ContentType*> result;
    for (const auto& type : types_) {
        result.push_back(&type);
    }
    return result;
}

void ContentTypeRegistry::registerDefaultTypes() {
    // TEXT Types
    registerType({
        .mime_type = "text/plain",
        .category = ContentCategory::TEXT,
        .extensions = {".txt", ".text"},
        .supports_text_extraction = true,
        .supports_embedding = true,
        .supports_chunking = true,
        .supports_metadata_extraction = false,
        .binary_storage_required = false
    });
    
    registerType({
        .mime_type = "text/markdown",
        .category = ContentCategory::TEXT,
        .extensions = {".md", ".markdown"},
        .supports_text_extraction = true,
        .supports_embedding = true,
        .supports_chunking = true,
        .supports_metadata_extraction = false,
        .binary_storage_required = false
    });
    
    registerType({
        .mime_type = "text/html",
        .category = ContentCategory::TEXT,
        .extensions = {".html", ".htm"},
        .supports_text_extraction = true,
        .supports_embedding = true,
        .supports_chunking = true,
        .supports_metadata_extraction = true,
        .binary_storage_required = false
    });
    
    registerType({
        .mime_type = "application/json",
        .category = ContentCategory::TEXT,
        .extensions = {".json"},
        .supports_text_extraction = true,
        .supports_embedding = true,
        .supports_chunking = false,
        .supports_metadata_extraction = true,
        .binary_storage_required = false
    });
    
    registerType({
        .mime_type = "application/xml",
        .category = ContentCategory::TEXT,
        .extensions = {".xml"},
        .supports_text_extraction = true,
        .supports_embedding = true,
        .supports_chunking = true,
        .supports_metadata_extraction = true,
        .binary_storage_required = false
    });
    
    registerType({
        .mime_type = "text/x-python",
        .category = ContentCategory::TEXT,
        .extensions = {".py"},
        .supports_text_extraction = true,
        .supports_embedding = true,
        .supports_chunking = true,
        .supports_metadata_extraction = false,
        .binary_storage_required = false
    });
    
    registerType({
        .mime_type = "text/x-c++src",
        .category = ContentCategory::TEXT,
        .extensions = {".cpp", ".cc", ".cxx", ".c++"},
        .supports_text_extraction = true,
        .supports_embedding = true,
        .supports_chunking = true,
        .supports_metadata_extraction = false,
        .binary_storage_required = false
    });
    
    registerType({
        .mime_type = "text/x-chdr",
        .category = ContentCategory::TEXT,
        .extensions = {".h", ".hpp", ".hh", ".hxx"},
        .supports_text_extraction = true,
        .supports_embedding = true,
        .supports_chunking = true,
        .supports_metadata_extraction = false,
        .binary_storage_required = false
    });
    
    registerType({
        .mime_type = "application/javascript",
        .category = ContentCategory::TEXT,
        .extensions = {".js", ".mjs"},
        .supports_text_extraction = true,
        .supports_embedding = true,
        .supports_chunking = true,
        .supports_metadata_extraction = false,
        .binary_storage_required = false
    });
    
    registerType({
        .mime_type = "application/pdf",
        .category = ContentCategory::TEXT,
        .extensions = {".pdf"},
        .supports_text_extraction = true,
        .supports_embedding = true,
        .supports_chunking = true,
        .supports_metadata_extraction = true,
        .binary_storage_required = true
    });
    
    // IMAGE Types
    registerType({
        .mime_type = "image/jpeg",
        .category = ContentCategory::IMAGE,
        .extensions = {".jpg", ".jpeg"},
        .supports_text_extraction = false,
        .supports_embedding = true,
        .supports_chunking = false,
        .supports_metadata_extraction = true,
        .binary_storage_required = true,
        .features = {.temporal = false}
    });
    
    registerType({
        .mime_type = "image/png",
        .category = ContentCategory::IMAGE,
        .extensions = {".png"},
        .supports_text_extraction = false,
        .supports_embedding = true,
        .supports_chunking = false,
        .supports_metadata_extraction = true,
        .binary_storage_required = true
    });
    
    registerType({
        .mime_type = "image/gif",
        .category = ContentCategory::IMAGE,
        .extensions = {".gif"},
        .supports_text_extraction = false,
        .supports_embedding = true,
        .supports_chunking = false,
        .supports_metadata_extraction = false,
        .binary_storage_required = true
    });
    
    registerType({
        .mime_type = "image/svg+xml",
        .category = ContentCategory::IMAGE,
        .extensions = {".svg"},
        .supports_text_extraction = true,
        .supports_embedding = true,
        .supports_chunking = false,
        .supports_metadata_extraction = false,
        .binary_storage_required = false
    });
    
    registerType({
        .mime_type = "image/tiff",
        .category = ContentCategory::GEO, // GeoTIFF
        .extensions = {".tif", ".tiff"},
        .supports_text_extraction = false,
        .supports_embedding = true,
        .supports_chunking = false,
        .supports_metadata_extraction = true,
        .binary_storage_required = true,
        .features = {.geospatial = true}
    });
    
    // AUDIO Types
    registerType({
        .mime_type = "audio/mpeg",
        .category = ContentCategory::AUDIO,
        .extensions = {".mp3"},
        .supports_text_extraction = false, // Unless speech-to-text
        .supports_embedding = true,
        .supports_chunking = true,
        .supports_metadata_extraction = true,
        .binary_storage_required = true,
        .features = {.temporal = true}
    });
    
    registerType({
        .mime_type = "audio/wav",
        .category = ContentCategory::AUDIO,
        .extensions = {".wav"},
        .supports_text_extraction = false,
        .supports_embedding = true,
        .supports_chunking = true,
        .supports_metadata_extraction = true,
        .binary_storage_required = true,
        .features = {.temporal = true}
    });
    
    registerType({
        .mime_type = "audio/flac",
        .category = ContentCategory::AUDIO,
        .extensions = {".flac"},
        .supports_text_extraction = false,
        .supports_embedding = true,
        .supports_chunking = true,
        .supports_metadata_extraction = true,
        .binary_storage_required = true,
        .features = {.temporal = true}
    });
    
    // VIDEO Types
    registerType({
        .mime_type = "video/mp4",
        .category = ContentCategory::VIDEO,
        .extensions = {".mp4", ".m4v"},
        .supports_text_extraction = false,
        .supports_embedding = true,
        .supports_chunking = true,
        .supports_metadata_extraction = true,
        .binary_storage_required = true,
        .features = {.temporal = true, .multimodal = true}
    });
    
    registerType({
        .mime_type = "video/webm",
        .category = ContentCategory::VIDEO,
        .extensions = {".webm"},
        .supports_text_extraction = false,
        .supports_embedding = true,
        .supports_chunking = true,
        .supports_metadata_extraction = true,
        .binary_storage_required = true,
        .features = {.temporal = true, .multimodal = true}
    });
    
    // GEO Types
    registerType({
        .mime_type = "application/geo+json",
        .category = ContentCategory::GEO,
        .extensions = {".geojson"},
        .supports_text_extraction = true,
        .supports_embedding = true,
        .supports_chunking = true,
        .supports_metadata_extraction = true,
        .binary_storage_required = false,
        .features = {.geospatial = true}
    });
    
    registerType({
        .mime_type = "application/gpx+xml",
        .category = ContentCategory::GEO,
        .extensions = {".gpx"},
        .supports_text_extraction = true,
        .supports_embedding = true,
        .supports_chunking = true,
        .supports_metadata_extraction = true,
        .binary_storage_required = false,
        .features = {.geospatial = true, .temporal = true}
    });
    
    // CAD Types
    registerType({
        .mime_type = "model/step",
        .category = ContentCategory::CAD,
        .extensions = {".stp", ".step"},
        .supports_text_extraction = true,
        .supports_embedding = true,
        .supports_chunking = true,
        .supports_metadata_extraction = true,
        .binary_storage_required = false,
        .features = {.hierarchical = true}
    });
    
    registerType({
        .mime_type = "model/stl",
        .category = ContentCategory::CAD,
        .extensions = {".stl"},
        .supports_text_extraction = false,
        .supports_embedding = true,
        .supports_chunking = false,
        .supports_metadata_extraction = true,
        .binary_storage_required = true
    });
    
    registerType({
        .mime_type = "application/dxf",
        .category = ContentCategory::CAD,
        .extensions = {".dxf"},
        .supports_text_extraction = true,
        .supports_embedding = true,
        .supports_chunking = true,
        .supports_metadata_extraction = true,
        .binary_storage_required = false,
        .features = {.hierarchical = true}
    });
    
    // STRUCTURED Types
    registerType({
        .mime_type = "text/csv",
        .category = ContentCategory::STRUCTURED,
        .extensions = {".csv"},
        .supports_text_extraction = true,
        .supports_embedding = true,
        .supports_chunking = true,
        .supports_metadata_extraction = true,
        .binary_storage_required = false
    });
    
    registerType({
        .mime_type = "application/vnd.apache.parquet",
        .category = ContentCategory::STRUCTURED,
        .extensions = {".parquet"},
        .supports_text_extraction = false,
        .supports_embedding = true,
        .supports_chunking = true,
        .supports_metadata_extraction = true,
        .binary_storage_required = true
    });
    
    registerType({
        .mime_type = "application/vnd.apache.arrow.file",
        .category = ContentCategory::STRUCTURED,
        .extensions = {".arrow"},
        .supports_text_extraction = false,
        .supports_embedding = true,
        .supports_chunking = true,
        .supports_metadata_extraction = true,
        .binary_storage_required = true
    });
    
    // ARCHIVE Types
    registerType({
        .mime_type = "application/zip",
        .category = ContentCategory::ARCHIVE,
        .extensions = {".zip"},
        .supports_text_extraction = false,
        .supports_embedding = false,
        .supports_chunking = false,
        .supports_metadata_extraction = true,
        .binary_storage_required = true,
        .features = {.hierarchical = true}
    });
    
    registerType({
        .mime_type = "application/x-tar",
        .category = ContentCategory::ARCHIVE,
        .extensions = {".tar"},
        .supports_text_extraction = false,
        .supports_embedding = false,
        .supports_chunking = false,
        .supports_metadata_extraction = true,
        .binary_storage_required = true,
        .features = {.hierarchical = true}
    });
    
    // BINARY (Fallback)
    registerType({
        .mime_type = "application/octet-stream",
        .category = ContentCategory::BINARY,
        .extensions = {".bin", ".dat"},
        .supports_text_extraction = false,
        .supports_embedding = false,
        .supports_chunking = false,
        .supports_metadata_extraction = false,
        .binary_storage_required = true
    });
}

void initializeDefaultContentTypes() {
    // Singleton instance is created on first access
    ContentTypeRegistry::instance();
}

} // namespace content
} // namespace themis
