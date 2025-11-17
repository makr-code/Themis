# content_type.cpp

Path: `src/content/content_type.cpp`

Purpose: Detect and manage content MIME types and metadata.

Public functions / symbols:
- `for (const auto& type : types_) {`
- `if (type.mime_type == mime_type) {`
- ``
- `for (const auto& type_ext : type.extensions) {`
- `if (first_line_end != std::string::npos && first_line_end < 1000) {`
- `if (printable_count > 900) { // >90% printable`
- `if (type.category == category) {`
- `registerDefaultTypes();`
- `return getByMimeType("application/pdf");`
- `return getByMimeType("image/png");`
- `return getByMimeType("image/jpeg");`
- `return getByMimeType("image/gif");`
- `return getByMimeType("application/zip");`
- `return getByMimeType("application/geo+json");`
- `return getByMimeType("application/json");`
- `return getByMimeType("application/gpx+xml");`
- `return getByMimeType("application/xml");`
- `return getByMimeType("text/csv");`
- `return getByMimeType("text/plain");`
- `registerType({`
- `ContentTypeRegistry::ContentTypeRegistry() {`

