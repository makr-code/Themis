# regex_detection_engine.cpp

Path: `src/utils/regex_detection_engine.cpp`

Purpose: Regex based detectors for PII and pattern matching utilities.

Public functions / symbols:
- ``
- `for (const auto& [hint, type] : field_name_hints_) {`
- `for (const auto& flag : pattern_node["flags"]) {`
- `for (const auto& hint : pattern_node["field_hints"]) {`
- `if (type != PIIType::UNKNOWN) {`
- `if (flag == "icase") {`
- `for (char c : number) {`
- `if (digit > 9) {`
- `std::unique_ptr<IPIIDetectionEngine> createRegexEngine() {`
- `std::lock_guard<std::mutex> lock(mutex_);`
- `loadEmbeddedDefaults();`

