#include "utils/logger.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include "storage/key_schema.h"
#include "index/secondary_index.h"
#include "index/graph_index.h"
#include "index/vector_index.h"
#include "transaction/transaction_manager.h"
#include "query/query_engine.h"
#include "query/query_optimizer.h"
#include <iostream>
#include <string>

using namespace themis;

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    // Initialize logger
    utils::Logger::init("vccdb.log", utils::Logger::Level::INFO);
    
    THEMIS_INFO("=== Themis Multi-Model Database System ===");
    THEMIS_INFO("Version: 0.1.0");
    THEMIS_INFO("Architecture: Hybrid Relational-Graph-Vector-Document");
    
    try {
        // Configure RocksDB
    RocksDBWrapper::Config config;
    config.db_path = "./data/themis_test";
        config.memtable_size_mb = 64;
        config.block_cache_size_mb = 256;
        config.enable_wal = true;
        config.enable_blobdb = false; // Start simple
        
        // Create database wrapper
        THEMIS_INFO("Initializing RocksDB storage engine...");
        RocksDBWrapper db(config);
        
        if (!db.open()) {
            THEMIS_ERROR("Failed to open database!");
            return 1;
        }
        
        THEMIS_INFO("Database opened successfully at: {}", config.db_path);
        
        // === Demo: Insert a relational entity ===
        {
            THEMIS_INFO("--- Test 1: Relational Model (New API) ---");
            
            // Create entity using field map
            BaseEntity::FieldMap fields;
            fields["id"] = std::string("user_123");
            fields["name"] = std::string("Alice");
            fields["age"] = int64_t(30);
            fields["email"] = std::string("alice@example.com");
            fields["active"] = true;
            fields["score"] = 95.5;
            
            BaseEntity entity = BaseEntity::fromFields("user_123", fields);
            
            std::string key = KeySchema::makeRelationalKey("users", "user_123");
            auto blob = entity.serialize();
            
            THEMIS_INFO("Inserting user: {}", entity.toJson());
            if (db.put(key, blob)) {
                THEMIS_INFO("Successfully inserted entity with key: {}", key);
                THEMIS_INFO("Blob size: {} bytes", blob.size());
            }
            
            // Retrieve and parse
            auto result = db.get(key);
            if (result) {
                BaseEntity retrieved = BaseEntity::deserialize("user_123", *result);
                THEMIS_INFO("Retrieved: {}", retrieved.toJson());
                
                // Test field access
                THEMIS_INFO("  Name: {}", retrieved.getFieldAsString("name").value());
                THEMIS_INFO("  Age: {}", retrieved.getFieldAsInt("age").value());
                THEMIS_INFO("  Active: {}", retrieved.getFieldAsBool("active").value());
                THEMIS_INFO("  Score: {}", retrieved.getFieldAsDouble("score").value());
            }
        }
        
        // === Demo: Insert a graph node ===
        {
            THEMIS_INFO("--- Test 2: Graph Model (Node) ---");
            
            BaseEntity::FieldMap fields;
            fields["id"] = std::string("user/alice");
            fields["label"] = std::string("User");
            fields["name"] = std::string("Alice");
            fields["role"] = std::string("Developer");
            
            BaseEntity entity = BaseEntity::fromFields("user/alice", fields);
            std::string key = KeySchema::makeGraphNodeKey("user/alice");
            
            THEMIS_INFO("Inserting graph node: {}", entity.toJson());
            db.put(key, entity.serialize());
        }
        
        // === Demo: Insert a graph edge (via GraphIndexManager) ===
        {
            THEMIS_INFO("--- Test 3: Graph Model (Edge via Manager) ---");
            GraphIndexManager gidx(db);

            BaseEntity::FieldMap fields;
            fields["id"] = std::string("edge_1");
            fields["_from"] = std::string("user/alice");
            fields["_to"] = std::string("company/acme");
            fields["label"] = std::string("WORKS_AT");
            fields["since"] = int64_t(2020);
            
            BaseEntity entity = BaseEntity::fromFields("edge_1", fields);
            THEMIS_INFO("Inserting graph edge via manager: {}", entity.toJson());
            auto gs = gidx.addEdge(entity);
            if (!gs.ok) {
                THEMIS_ERROR("Graph addEdge failed: {}", gs.message);
            } else {
                THEMIS_INFO("Created edge and adjacency entries atomically");
            }
        }
        
        // === Demo: Scan prefix (nodes) ===
        {
            THEMIS_INFO("--- Test 4: Prefix Scan ---");
            THEMIS_INFO("Scanning all graph nodes...");
            
            int count = 0;
            db.scanPrefix("node:", [&count](std::string_view key, std::string_view value) {
                std::string data(value.begin(), value.end());
                THEMIS_INFO("  Found: {} -> {}", key, data);
                count++;
                return true; // Continue iteration
            });
            
            THEMIS_INFO("Found {} graph nodes", count);
        }
        
        // === Demo: Vector Model ===
        {
            THEMIS_INFO("--- Test 5: Vector Model (Document with Embedding) ---");
            
            BaseEntity::FieldMap fields;
            fields["id"] = std::string("doc_1");
            fields["text"] = std::string("Machine learning is amazing");
            fields["category"] = std::string("AI");
            
            // Create a sample embedding vector
            std::vector<float> embedding = {0.12f, 0.45f, 0.67f, 0.89f, 0.23f, 
                                           0.56f, 0.78f, 0.34f, 0.91f, 0.15f};
            fields["embedding"] = embedding;
            
            BaseEntity entity = BaseEntity::fromFields("doc_1", fields);
            std::string key = KeySchema::makeVectorKey("documents", "doc_1");
            
            THEMIS_INFO("Inserting document with embedding: {}", entity.toJson());
            db.put(key, entity.serialize());
            
            // Retrieve and verify vector
            auto result = db.get(key);
            if (result) {
                BaseEntity retrieved = BaseEntity::deserialize("doc_1", *result);
                auto vec = retrieved.extractVector("embedding");
                if (vec) {
                    THEMIS_INFO("Retrieved embedding vector with {} dimensions", vec->size());
                    THEMIS_INFO("  First value: {}, Last value: {}", (*vec)[0], (*vec)[vec->size()-1]);
                }
            }

            // Füge eine zweite Vektordokument-Entity hinzu
            BaseEntity::FieldMap fields2;
            fields2["id"] = std::string("doc_2");
            fields2["text"] = std::string("Deep learning is powerful");
            fields2["category"] = std::string("AI");
            std::vector<float> embedding2 = {0.11f, 0.44f, 0.65f, 0.88f, 0.21f,
                                            0.57f, 0.76f, 0.35f, 0.89f, 0.14f};
            fields2["embedding"] = embedding2;
            BaseEntity e2 = BaseEntity::fromFields("doc_2", fields2);
            std::string key2 = KeySchema::makeVectorKey("documents", "doc_2");
            db.put(key2, e2.serialize());
        }
        
    // === Demo: Secondary index (Manager) ===
        {
            THEMIS_INFO("--- Test 6: Secondary Index (Manager) ---");

            SecondaryIndexManager idxm(db);
            auto s1 = idxm.createIndex("users", "age");
            if (!s1.ok) { THEMIS_ERROR("{}", s1.message); }
            auto s1b = idxm.createIndex("users", "active");
            if (!s1b.ok) { THEMIS_ERROR("{}", s1b.message); }

            // Neue Entity via Manager einfügen (atomar inkl. Indexpflege)
            BaseEntity::FieldMap u2;
            u2["id"] = std::string("user_456");
            u2["name"] = std::string("Bob");
            u2["age"] = int64_t(30);
            u2["email"] = std::string("bob@example.com");
            u2["active"] = true;
            BaseEntity e2 = BaseEntity::fromFields("user_456", u2);
            auto s2 = idxm.put("users", e2);
            if (!s2.ok) { THEMIS_ERROR("{}", s2.message); }

            // Abfrage über den Index (age = 30)
            THEMIS_INFO("Querying (Manager): SELECT * FROM users WHERE age = 30");
            auto [st, entities] = idxm.scanEntitiesEqual("users", "age", "30");
            if (!st.ok) {
                THEMIS_ERROR("{}", st.message);
            } else {
                for (const auto& ent : entities) {
                    THEMIS_INFO("  Hit: PK={} -> {}", ent.getPrimaryKey(), ent.toJson());
                }
            }
        }

        // === Demo: Graph traversal (BFS) ===
        {
            THEMIS_INFO("--- Test 7: Graph BFS Traversal ---");
            GraphIndexManager gidx(db);
            auto [st, order] = gidx.bfs("user/alice", 2);
            if (!st.ok) {
                THEMIS_ERROR("Graph BFS failed: {}", st.message);
            } else {
                std::string path;
                for (size_t i = 0; i < order.size(); ++i) {
                    if (i) path += " -> ";
                    path += order[i];
                }
                THEMIS_INFO("BFS order (depth<=2): {}", path);
            }
        }

        // === Demo: Parallele Query Execution (AND über Sekundärindizes) ===
        {
            THEMIS_INFO("--- Test 10: Parallel Query AND(users.age=30 AND users.active=true) ---");
            SecondaryIndexManager idxm(db);
            QueryEngine qe(db, idxm);
            ConjunctiveQuery q{ "users", { {"age", "30"}, {"active", "true"} } };
            auto [st, ents] = qe.executeAndEntities(q);
            if (!st.ok) {
                THEMIS_ERROR("Parallel query failed: {}", st.message);
            } else {
                for (const auto& en : ents) {
                    THEMIS_INFO("  Match: PK={} -> {}", en.getPrimaryKey(), en.toJson());
                }
            }

            // Optimierter Plan: Reihenfolge nach geschätzter Selektivität
            QueryOptimizer opt(idxm);
            auto plan = opt.chooseOrderForAndQuery(q, 1000);
            std::string orderStr;
            for (size_t i = 0; i < plan.orderedPredicates.size(); ++i) {
                if (i) orderStr += ", ";
                orderStr += plan.orderedPredicates[i].column + "=" + plan.orderedPredicates[i].value;
            }
            THEMIS_INFO("Optimized predicate order: [{}]", orderStr);

            auto [st2, ents2] = opt.executeOptimizedEntities(qe, q, plan);
            if (!st2.ok) {
                THEMIS_ERROR("Optimized query failed: {}", st2.message);
            } else {
                for (const auto& en : ents2) {
                    THEMIS_INFO("  Opt-Match: PK={} -> {}", en.getPrimaryKey(), en.toJson());
                }
            }
        }

        // === Demo: Vector ANN Index (HNSW/Fallback) ===
        {
            THEMIS_INFO("--- Test 8: Vector ANN Index ---");
            VectorIndexManager vxim(db);
            auto vs = vxim.init("documents", 10, VectorIndexManager::Metric::COSINE, 16, 100, 64);
            if (!vs.ok) {
                THEMIS_ERROR("Vector init failed: {}", vs.message);
            } else {
                vxim.rebuildFromStorage();
                // Query: nutze den Vektor von doc_1
                auto blob = db.get(KeySchema::makeVectorKey("documents", "doc_1"));
                if (blob) {
                    BaseEntity e = BaseEntity::deserialize("doc_1", *blob);
                    auto q = e.extractVector("embedding");
                    if (q) {
                        auto [st, hits] = vxim.searchKnn(*q, 2);
                        if (!st.ok) {
                            THEMIS_ERROR("Vector search failed: {}", st.message);
                        } else {
                            for (const auto& r : hits) {
                                THEMIS_INFO("  KNN hit: pk={}, dist={}", r.pk, r.distance);
                            }
                        }
                    }
                }
            }
        }

        // === Demo: Transaktionale Konsistenz (ACID via WriteBatch) ===
        {
            THEMIS_INFO("--- Test 9: Transactional Update across layers ---");
            SecondaryIndexManager idxm(db);
            GraphIndexManager gidx(db);
            VectorIndexManager vidx(db); // VectorIndexManager parameter
            TransactionManager txm(db, idxm, gidx, vidx);
            auto tx = txm.begin();

            // Update: users.user_456 -> age=31 (Index wird atomar gepflegt) und neue Kante edge_2
            BaseEntity::FieldMap upd;
            upd["id"] = std::string("user_456");
            upd["name"] = std::string("Bob");
            upd["age"] = int64_t(31);
            upd["email"] = std::string("bob@example.com");
            BaseEntity e = BaseEntity::fromFields("user_456", upd);

            auto st1 = tx.putEntity("users", e);
            if (!st1.ok) THEMIS_ERROR("TX putEntity failed: {}", st1.message);

            BaseEntity::FieldMap ef;
            ef["id"] = std::string("edge_2");
            ef["_from"] = std::string("user/alice");
            ef["_to"] = std::string("project/phoenix");
            ef["label"] = std::string("ASSIGNED_TO");
            BaseEntity edge = BaseEntity::fromFields("edge_2", ef);
            auto st2 = tx.addEdge(edge);
            if (!st2.ok) THEMIS_ERROR("TX addEdge failed: {}", st2.message);

            auto stc = tx.commit();
            if (!stc.ok) {
                THEMIS_ERROR("Transaction commit failed: {}", stc.message);
            } else {
                THEMIS_INFO("Transaction committed successfully");
            }
        }
        
        // Print statistics
        THEMIS_INFO("--- Database Statistics ---");
        THEMIS_INFO("{}", db.getStats());
        
        // Close database
        THEMIS_INFO("Closing database...");
        db.close();
        THEMIS_INFO("Database closed successfully");
        
    } catch (const std::exception& e) {
        THEMIS_ERROR("Exception: {}", e.what());
        return 1;
    }
    
    utils::Logger::shutdown();
    
    std::cout << "\n=== Demo completed successfully! ===" << std::endl;
    std::cout << "Check vccdb.log for detailed logs" << std::endl;
    
    return 0;
}
