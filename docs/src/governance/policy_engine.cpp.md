# policy_engine.cpp

Path: `src/governance/policy_engine.cpp`

Purpose: Implements policy evaluation and enforcement; integrates with API and storage layers for access control.

Public functions / symbols:
- `if (config["vs_classification"]) {`
- `for (const auto& kv : vs) {`
- `if (config["enforcement"] && config["enforcement"]["resource_mapping"]) {`
- `for (const auto& kv : mappings) {`
- `if (config["enforcement"] && config["enforcement"]["default_mode"]) {`
- `if (profile) {`
- `if (enc_logs == "true" || enc_logs == "1" || enc_logs == "yes") {`
- `if (audit_logger_ && d.mode == "enforce") {`
- `return (c == "geheim" || c == "streng-geheim");`

