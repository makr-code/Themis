# pii_detector.cpp

Path: `src/utils/pii_detector.cpp`

Purpose: Implements individual PII detectors (regex, heuristics) used by the detection engine.

Public functions / symbols:
- `for (const auto& engine : engines_) {`
- `if (type != PIIType::UNKNOWN) {`
- `if (mode != "none" && mode != default_redaction_mode_) {`
- ``
- `if (config["global_settings"]) {`
- `if (settings["default_redaction_mode"]) {`
- `if (!config["detection_engines"]) {`
- `for (const auto& item : yaml_node) {`
- `for (const auto& kv : yaml_node) {`
- `if (!engine) {`
- `if (!enabled) {`
- `if (pki_client_) {`
- `if (engine) {`
- `if (curr.start_offset < prev.end_offset) {`
- `if (prev.type == PIIType::PHONE && curr.type != PIIType::PHONE) {`
- `if (curr.confidence > prev.confidence) {`
- `initializeDefaultEngine();`
- `std::lock_guard<std::mutex> lock(mutex_);`
- `scanJsonRecursive(json_obj, "", result);`
- `convertYamlToJson(item, item_json);`
- `convertYamlToJson(kv.second, value_json);`
- `for (auto it = obj.begin(); it != obj.end(); ++it) {`
- `scanJsonRecursive(obj[i], new_path, findings);`

