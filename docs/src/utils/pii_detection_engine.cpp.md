# pii_detection_engine.cpp

Path: `src/utils/pii_detection_engine.cpp`

Purpose: Engine coordinating PII detection across various detectors and rules.

Public functions / symbols:
- `for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {`
- `if (computed_hash != config_hash) {`
- `switch (type) {`
- ``
- `if (type == PIIType::SSN) {`
- `for (char c : value) {`
- `if (to_mask > 0) { out.push_back('*'); --to_mask; }`
- `if (type == PIIType::CREDIT_CARD) {`
- `if (at_pos != std::string::npos && at_pos > 0) {`
- `if (to_mask > 0) {`
- `if (first_dot != std::string::npos) {`
- `if (space_pos != std::string::npos && space_pos > 0) {`
- `if (!engine) {`
- `std::unique_ptr<IPIIDetectionEngine> createRegexEngine();`
- `return createRegexEngine();`

