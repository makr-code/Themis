# Hierarchical Network Model + VCC-URN Integration

**Version:** 2.0  
**Datum:** 20. November 2025  
**Status:** Design - URN-basierte Hierarchie-Adressierung

---

## Executive Summary

Das **Hierarchical Network Model** wird vollständig in das **VCC-URN System** integriert für:
- Globale Eindeutigkeit über verteilte Cluster
- Sharding-fähige hierarchische Strukturen
- Cross-Cluster Referenzen
- Föderation über Organisationsgrenzen

---

## 1. URN-SCHEMA FÜR HIERARCHIEN

### 1.1 Hierarchical Entity URN Format

```
urn:themis:hierarchy:{hierarchy_id}:{level}:{entity_id}

Komponenten:
- themis: Namespace (konstant)
- hierarchy: Model-Typ (neu)
- {hierarchy_id}: Hierarchie-Instanz (government, corporate, supply_chain)
- {level}: Hierarchie-Ebene (global, national, federal, institutional)
- {entity_id}: UUID der Entität
```

**Beispiele:**

```
# Government Hierarchy
urn:themis:hierarchy:government:global:un:550e8400-e29b-41d4-a716-446655440000
urn:themis:hierarchy:government:national:de:7c9e6679-7425-40de-944b-e07fc1f90ae7
urn:themis:hierarchy:government:federal:de_by:f47ac10b-58cc-4372-a567-0e02b2c3d479
urn:themis:hierarchy:government:institutional:de_bmf:3d6e3e3e-4c5d-4f5e-9e7f-8a9b0c1d2e3f

# Corporate Hierarchy
urn:themis:hierarchy:corporate:holding:acme:a1b2c3d4-e5f6-7890-abcd-ef1234567890
urn:themis:hierarchy:corporate:division:tech:b2c3d4e5-f6a7-8901-bcde-f12345678901
urn:themis:hierarchy:corporate:department:engineering:c3d4e5f6-a7b8-9012-cdef-123456789012

# Supply Chain Hierarchy
urn:themis:hierarchy:supply_chain:manufacturer:bosch:d4e5f6a7-b8c9-0123-def1-234567890123
urn:themis:hierarchy:supply_chain:distributor:metro:e5f6a7b8-c9d0-1234-ef12-345678901234
```

### 1.2 Relationship URN Format

```
urn:themis:hierarchy:{hierarchy_id}:rel:{relationship_type}:{from_entity_id}:{to_entity_id}

Beispiel:
urn:themis:hierarchy:government:rel:member_of:de:un
urn:themis:hierarchy:corporate:rel:owns:acme:tech_div
```

---

## 2. URN PARSER EXTENSION

### 2.1 Extended URN Structure

```cpp
namespace themis::sharding {

/**
 * Extended URN for Hierarchical Models
 */
struct HierarchyURN : public URN {
    std::string hierarchyId;      // government, corporate, etc.
    std::string level;            // global, national, federal, institutional
    std::string entityShortId;    // de, un, bmf (human-readable)
    
    /**
     * Parse hierarchical URN
     * Format: urn:themis:hierarchy:{hierarchy_id}:{level}:{entity_short_id}:{uuid}
     */
    static std::optional<HierarchyURN> parseHierarchy(std::string_view urn_str);
    
    /**
     * Create URN from hierarchy entity
     */
    static HierarchyURN fromEntity(
        std::string_view hierarchyId,
        std::string_view level,
        std::string_view entityShortId,
        std::string_view uuid
    );
    
    /**
     * Get parent URN (navigate up hierarchy)
     * Returns nullopt if already at top level
     */
    std::optional<HierarchyURN> getParentURN() const;
    
    /**
     * Get hierarchy chain URNs (bottom to top)
     */
    std::vector<HierarchyURN> getHierarchyChainURNs() const;
    
    /**
     * Serialize to string
     */
    std::string toString() const override;
};

} // namespace themis::sharding
```

### 2.2 URN Resolver Extension

```cpp
namespace themis::sharding {

class HierarchyURNResolver : public URNResolver {
public:
    /**
     * Resolve hierarchical entity to shard
     * Uses level-based partitioning strategy
     */
    std::optional<ShardInfo> resolveHierarchyEntity(const HierarchyURN& urn) const;
    
    /**
     * Resolve hierarchy chain (all ancestors)
     * Returns shard info for each level in chain
     */
    std::vector<ShardInfo> resolveHierarchyChain(const HierarchyURN& urn) const;
    
    /**
     * Find all children URNs
     */
    std::vector<HierarchyURN> findChildren(const HierarchyURN& parent_urn) const;
    
    /**
     * Cross-hierarchy reference resolution
     * Example: Entity in government hierarchy references entity in corporate hierarchy
     */
    std::optional<ShardInfo> resolveCrossHierarchy(
        const HierarchyURN& source_urn,
        const HierarchyURN& target_urn
    ) const;
};

} // namespace themis::sharding
```

---

## 3. SHARDING STRATEGY

### 3.1 Level-Based Partitioning

```cpp
/**
 * Hierarchical entities are sharded by level
 * 
 * Rationale:
 * - Top levels (global, national) are small, can be replicated
 * - Bottom levels (institutional) are large, need sharding
 * - Queries often traverse levels, minimize cross-shard hops
 */
class HierarchyShardingStrategy {
public:
    enum class ShardingMode {
        // All levels on same shard (small hierarchies)
        COLOCATED,
        
        // Top levels replicated, bottom levels sharded
        HYBRID,
        
        // Each level sharded independently
        DISTRIBUTED
    };
    
    /**
     * Determine shard for entity based on level
     */
    std::string selectShard(
        const HierarchyURN& urn,
        ShardingMode mode,
        const std::vector<ShardInfo>& available_shards
    ) const;
    
    /**
     * Optimize shard placement for hierarchy
     * Co-locate frequently accessed entities
     */
    std::map<HierarchyURN, std::string> optimizePlacement(
        const std::vector<HierarchyURN>& entities,
        const std::map<std::pair<HierarchyURN, HierarchyURN>, int>& access_patterns
    ) const;
};
```

### 3.2 Sharding Examples

**Small Hierarchy (Government - COLOCATED):**
```
Shard 1: All entities (global, national, federal, institutional)
- Fast traversal, no cross-shard queries
- Suitable for single-country deployments
```

**Medium Hierarchy (Government - HYBRID):**
```
Shard 1 (Replicated): Global + National entities
Shard 2-10: Federal + Institutional entities (sharded by region)
- Balance between locality and scalability
- Suitable for multi-national deployments
```

**Large Hierarchy (Corporate - DISTRIBUTED):**
```
Shard 1: Holding company
Shard 2-5: Divisions (sharded by division ID)
Shard 6-20: Departments (sharded by department ID)
Shard 21-100: Teams (sharded by team ID)
- Maximum scalability
- Suitable for Fortune 500 companies
```

---

## 4. CROSS-HIERARCHY REFERENCES

### 4.1 Federated URN References

```cpp
/**
 * Cross-hierarchy relationships
 * Example: Government ministry (hierarchy A) contracts with company (hierarchy B)
 */
struct CrossHierarchyRelation {
    HierarchyURN sourceURN;
    HierarchyURN targetURN;
    std::string relationType;  // CONTRACTS_WITH, REGULATES, PARTNERS_WITH
    
    /**
     * Resolve both endpoints
     */
    struct ResolvedRelation {
        ShardInfo sourceShardInfo;
        ShardInfo targetShardInfo;
        bool requiresCrossShardQuery;
    };
    
    ResolvedRelation resolve(const HierarchyURNResolver& resolver) const;
};
```

**Beispiel:**
```cpp
// German Ministry of Finance contracts with SAP
CrossHierarchyRelation relation{
    .sourceURN = HierarchyURN::parse("urn:themis:hierarchy:government:institutional:de_bmf:..."),
    .targetURN = HierarchyURN::parse("urn:themis:hierarchy:corporate:holding:sap:..."),
    .relationType = "CONTRACTS_WITH"
};

auto resolved = relation.resolve(urnResolver);
// May require cross-shard query if on different shards
```

---

## 5. YAML CONFIGURATION MIT URN

### 5.1 Hierarchy Definition with URN Namespace

```yaml
# config/hierarchies/government_urn.yaml
hierarchy:
  name: "Government Network"
  graph_id: "government"
  
  # URN Configuration
  urn_config:
    namespace: "hierarchy"
    hierarchy_id: "government"
    enable_sharding: true
    sharding_mode: "hybrid"  # colocated, hybrid, distributed
    
    # Replication strategy per level
    replication:
      global: 3      # 3 replicas (highly available)
      national: 3    # 3 replicas
      federal: 2     # 2 replicas
      institutional: 1  # 1 replica (sharded for scale)
  
  levels:
    - id: "global"
      urn_prefix: "urn:themis:hierarchy:government:global"
      shard_strategy: "replicated"
      
    - id: "national"
      urn_prefix: "urn:themis:hierarchy:government:national"
      shard_strategy: "replicated"
      
    - id: "federal"
      urn_prefix: "urn:themis:hierarchy:government:federal"
      shard_strategy: "hash"  # Consistent hashing
      
    - id: "institutional"
      urn_prefix: "urn:themis:hierarchy:government:institutional"
      shard_strategy: "hash"
```

### 5.2 Entity Creation with URN

```yaml
# VCC_VPB Process with URN
process:
  id: "gov_hierarchy_setup_urn"
  
  steps:
    - step: "create_global_entities"
      action: "hierarchy.create_entities"
      params:
        entities:
          - short_id: "un"
            level: "global"
            type: "international_org"
            # URN auto-generated: urn:themis:hierarchy:government:global:un:{uuid}
            properties:
              name: "United Nations"
              founded: "1945-10-24"
              
          - short_id: "eu"
            level: "global"
            type: "economic_union"
            # URN: urn:themis:hierarchy:government:global:eu:{uuid}
            properties:
              name: "European Union"
    
    - step: "create_national_entities"
      action: "hierarchy.create_entities"
      params:
        entities:
          - short_id: "de"
            level: "national"
            type: "nation_state"
            # URN: urn:themis:hierarchy:government:national:de:{uuid}
            properties:
              name: "Germany"
              iso_code: "DEU"
    
    - step: "create_relationships"
      action: "hierarchy.create_relationships"
      params:
        relationships:
          - from_urn: "urn:themis:hierarchy:government:national:de:{uuid}"
            to_urn: "urn:themis:hierarchy:government:global:un:{uuid}"
            type: "member_of"
```

---

## 6. IMPLEMENTATION

### 6.1 HierarchyManager with URN Support

```cpp
namespace themis {
namespace hierarchy {

class URNHierarchyManager : public HierarchyManager {
public:
    /**
     * Constructor with URN resolver
     */
    URNHierarchyManager(
        PropertyGraphManager& pgm,
        const HierarchyDefinition& definition,
        std::shared_ptr<sharding::HierarchyURNResolver> urnResolver
    );
    
    /**
     * Create entity with URN
     */
    struct EntityWithURN {
        sharding::HierarchyURN urn;
        BaseEntity entity;
    };
    
    Status createEntityWithURN(
        std::string_view entityShortId,
        std::string_view levelId,
        std::string_view typeId,
        const BaseEntity& properties
    );
    
    /**
     * Get entity by URN
     */
    std::optional<EntityWithURN> getEntityByURN(
        const sharding::HierarchyURN& urn
    ) const;
    
    /**
     * Get hierarchy chain by URN
     */
    std::vector<EntityWithURN> getHierarchyChainByURN(
        const sharding::HierarchyURN& urn
    ) const;
    
    /**
     * Create cross-hierarchy relationship
     */
    Status createCrossHierarchyRelationship(
        const sharding::HierarchyURN& fromURN,
        const sharding::HierarchyURN& toURN,
        std::string_view relationType,
        const BaseEntity& properties = BaseEntity("")
    );
    
    /**
     * Query by URN pattern
     * Example: "urn:themis:hierarchy:government:*:de:*" finds all German entities
     */
    std::vector<EntityWithURN> queryByURNPattern(
        std::string_view urnPattern
    ) const;
    
private:
    std::shared_ptr<sharding::HierarchyURNResolver> urnResolver_;
};

} // namespace hierarchy
} // namespace themis
```

### 6.2 Usage Examples

```cpp
// Create URN-based hierarchy manager
auto urnResolver = std::make_shared<HierarchyURNResolver>(topology, hashRing);
URNHierarchyManager manager(pgm, definition, urnResolver);

// Create entity - URN generated automatically
BaseEntity un("un");
un.setField("name", "United Nations");
manager.createEntityWithURN("un", "global", "international_org", un);
// URN: urn:themis:hierarchy:government:global:un:{generated-uuid}

// Query by URN
auto urn = HierarchyURN::parse("urn:themis:hierarchy:government:national:de:{uuid}");
auto entity = manager.getEntityByURN(urn);

// Get hierarchy chain (returns URNs for all ancestors)
auto chain = manager.getHierarchyChainByURN(urn);
// Returns: [DE, EU, UN] with their URNs

// Cross-hierarchy relationship
auto govURN = HierarchyURN::parse("urn:themis:hierarchy:government:institutional:de_bmf:{uuid}");
auto corpURN = HierarchyURN::parse("urn:themis:hierarchy:corporate:holding:sap:{uuid}");
manager.createCrossHierarchyRelationship(govURN, corpURN, "CONTRACTS_WITH");

// Query pattern
auto allGerman = manager.queryByURNPattern("urn:themis:hierarchy:government:*:de*:*");
// Finds all German entities across all levels
```

---

## 7. BENEFITS OF URN INTEGRATION

### 7.1 Global Uniqueness

✅ Entities eindeutig über alle Cluster hinweg  
✅ Keine ID-Kollisionen bei Federation  
✅ Cross-Cluster Referenzen möglich

### 7.2 Location Transparency

✅ Client weiß nicht, wo Daten liegen  
✅ Resharding ohne Client-Änderungen  
✅ Optimale Shard-Platzierung

### 7.3 Scalability

✅ Hierarchien über 1000+ Shards verteilbar  
✅ Milliarden Entities möglich  
✅ Geo-distributed Deployment

### 7.4 Federation

✅ Multi-Org Hierarchies (EU + Member States)  
✅ Cross-Company Supply Chains  
✅ Global Government Networks

---

## 8. MIGRATION PATH

```sql
-- Existing entities without URN
government:global:un -> { id: "un", name: "UN" }

-- Migrated to URN
urn:themis:hierarchy:government:global:un:550e8400-... -> { id: "un", name: "UN", urn: "..." }

-- Backward compatibility maintained via alias
government:global:un -> redirect to URN
```

---

## 9. NEXT STEPS

**Phase 1: URN Extension (2 Wochen)**
- [x] Extend URN parser for hierarchy model
- [ ] HierarchyURNResolver implementation
- [ ] URN-based sharding strategy

**Phase 2: Manager Integration (2 Wochen)**
- [ ] URNHierarchyManager implementation
- [ ] Migration tools (legacy → URN)
- [ ] Cross-hierarchy relationships

**Phase 3: Federation (2 Wochen)**
- [ ] Multi-cluster hierarchy support
- [ ] Cross-cluster query routing
- [ ] URN-based replication

**Total: 6 Wochen**

---

**Status:** Ready for Implementation  
**Priority:** P1  
**Dependencies:** URN System (✅ Done), HierarchyDefinition, Sharding

**Letzte Aktualisierung:** 20. November 2025
