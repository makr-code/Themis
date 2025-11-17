# semantic_cache.cpp (query)

Path: `src/query/semantic_cache.cpp`

Purpose: Query side semantic cache; note: there is also `src/cache/semantic_cache.cpp`.

Public functions / symbols:
- `if (stats_.current_entries >= config_.max_entries) {`
- `if (!st.ok) {`
- `if (!stVec.ok) {`
- `if (config_.enable_exact_match) {`
- `if (config_.enable_similarity_match) {`
- ``
- `if (stats_.current_entries > 0) {`
- `if (stats_.total_result_bytes >= entry->result_size) {`
- `for (const auto& query : toRemove) {`
- `for (const auto& [feature, value] : features) {`
- `if (pos > 0) {`
- `if (pos < config_.embedding_dim - 1) {`
- `for (float val : embedding) {`
- `for (float& val : embedding) {`
- `if (st.ok) {`
- `for (const auto& token : tokens) {`
- `for (const auto& [token, count] : token_counts) {`
- `BaseEntity embEntity(entry.query);`
- `updateLRU_(query);`
- `std::lock_guard<std::mutex> lock(stats_mutex_);`
- `saveCacheEntry_(*entry);`
- `removeInternal_(query);`
- `updateLRU_(match.pk);`
- `return removeInternal_(query);`
- `std::lock_guard<std::mutex> lruLock(lru_mutex_);`
- `std::vector<float> embedding(config_.embedding_dim, 0.0f);`
- `std::lock_guard<std::mutex> lock(lru_mutex_);`

