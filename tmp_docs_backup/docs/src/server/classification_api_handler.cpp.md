# classification_api_handler.cpp

Path: `src/server/classification_api_handler.cpp`

Purpose: HTTP handlers for classification APIs, likely related to PII or policy classification.

Public functions / symbols:
- `: pii_detector_(pii_detector) {`
- `if (!pii_detector_) {`
- ``
- `for (const auto& finding : findings) {`
- `THEMIS_WARN("Classification API: PIIDetector not initialized");`
- `THEMIS_ERROR("Classification API: PIIDetector not initialized");`

