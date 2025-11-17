# saga.cpp

Path: `src/transaction/saga.cpp`

Purpose: Implements SAGA pattern orchestration for distributed transactions and compensation flows.

Public functions / symbols:
- `if (compensated_) {`
- `if (it->compensated) {`
- `for (const auto& step : steps_) {`
- `if (!status.ok) {`
- `compensate();`
- `THEMIS_WARN("SAGA: Already compensated, skipping");`
- `for (auto it = steps_.rbegin(); it != steps_.rend(); ++it) {`
- `THEMIS_DEBUG("SAGA: Step '{}' already compensated, skipping", it->operation_name);`
- `THEMIS_DEBUG("SAGA: Compensating step '{}'", it->operation_name);`
- `THEMIS_ERROR("SAGA: Unknown error during compensation of '{}'", it->operation_name);`
- `THEMIS_DEBUG("SAGA: Restored old value for key '{}'", key);`
- `THEMIS_WARN("SAGA: Delete of non-existent key '{}' - no compensation needed", key);`
- `THEMIS_DEBUG("SAGA: Restored deleted key '{}'", key);`
- `THEMIS_WARN("SAGA: Index compensation requires direct DB access - not fully implemented yet");`
- `THEMIS_WARN("SAGA: Graph compensation requires batch context - simplified implementation");`
- `THEMIS_WARN("SAGA: Vector compensation failed for '{}': {}", pk, status.message);`
- `THEMIS_DEBUG("SAGA: Removed vector '{}' from cache", pk);`

