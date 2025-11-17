# main.cpp.md

Path: TODO

Purpose: TODO

Public functions / symbols:

- ``
- `if (result) {`
- `if (!gs.ok) {`
- `if (vec) {`
- `if (!s1.ok) { THEMIS_ERROR("{}", s1.message); }`
- `if (!s1b.ok) { THEMIS_ERROR("{}", s1b.message); }`
- `if (!s2.ok) { THEMIS_ERROR("{}", s2.message); }`
- `if (!st.ok) {`
- `for (const auto& ent : entities) {`
- `for (const auto& en : ents) {`
- `if (!st2.ok) {`
- `for (const auto& en : ents2) {`
- `if (!vs.ok) {`
- `if (blob) {`
- `if (q) {`
- `for (const auto& r : hits) {`
- `if (!stc.ok) {`
- `THEMIS_INFO("Version: 0.1.0");`
- `THEMIS_INFO("Architecture: Hybrid Relational-Graph-Vector-Document");`
- `THEMIS_INFO("Initializing RocksDB storage engine...");`
- `RocksDBWrapper db(config);`
- `THEMIS_ERROR("Failed to open database!");`
- `THEMIS_INFO("Successfully inserted entity with key: {}", key);`
- `GraphIndexManager gidx(db);`
- `THEMIS_ERROR("Graph addEdge failed: {}", gs.message);`
- `THEMIS_INFO("Created edge and adjacency entries atomically");`
- `THEMIS_INFO("--- Test 4: Prefix Scan ---");`
- `THEMIS_INFO("Scanning all graph nodes...");`
- `THEMIS_INFO("  Found: {} -> {}", key, data);`
- `THEMIS_ERROR("{}", st.message);`
- `THEMIS_INFO("--- Test 7: Graph BFS Traversal ---");`
- `THEMIS_ERROR("Graph BFS failed: {}", st.message);`
- `SecondaryIndexManager idxm(db);`
- `QueryEngine qe(db, idxm);`
- `THEMIS_ERROR("Parallel query failed: {}", st.message);`
- `QueryOptimizer opt(idxm);`
- `THEMIS_INFO("Optimized predicate order: [{}]", orderStr);`
- `THEMIS_ERROR("Optimized query failed: {}", st2.message);`
- `THEMIS_INFO("--- Test 8: Vector ANN Index ---");`
- `VectorIndexManager vxim(db);`
- `THEMIS_ERROR("Vector init failed: {}", vs.message);`
- `THEMIS_ERROR("Vector search failed: {}", st.message);`
- `THEMIS_INFO("  KNN hit: pk={}, dist={}", r.pk, r.distance);`
- `THEMIS_INFO("--- Test 9: Transactional Update across layers ---");`
- `VectorIndexManager vidx(db); // VectorIndexManager parameter`
- `TransactionManager txm(db, idxm, gidx, vidx);`
- `THEMIS_ERROR("Transaction commit failed: {}", stc.message);`
- `THEMIS_INFO("Transaction committed successfully");`
- `THEMIS_INFO("--- Database Statistics ---");`
- `THEMIS_INFO("Closing database...");`

