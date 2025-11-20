# Hierarchical Network Model - Configuration-Driven Architecture

**Version:** 2.0 (Flexible)  
**Datum:** 20. November 2025  
**Status:** Design - Configuration-Driven  
**Typ:** Abstract Hierarchical Networks via YAML/VCC_VPB

---

## Executive Summary

**Änderung:** Government Network Model wird **abstrakt und konfigurierbar** statt hard-coded.

**Vorteile:**
- ✅ Wiederverwendbar für beliebige hierarchische Strukturen
- ✅ YAML-basierte Konfiguration
- ✅ VCC_VPB Process Builder Integration
- ✅ Keine Code-Änderungen für neue Hierarchien

**Use Cases:**
- Government: Global → National → Federal → Institutional
- Corporate: Holding → Division → Department → Team
- Supply Chain: Manufacturer → Distributor → Retailer → Store
- Healthcare: WHO → Country → Region → Hospital → Department
- Education: International → National → University → Faculty → Department

---

## 1. Abstract Hierarchy Model

### 1.1 Konzept

```yaml
# config/hierarchies/government.yaml
hierarchy:
  name: "Government Network"
  description: "Multi-level government and institutional structure"
  graph_id: "government"
  
  # Define levels (top to bottom)
  levels:
    - id: "global"
      name: "Global Organizations"
      description: "International bodies like UN, NATO, EU"
      depth: 0
      
    - id: "national"
      name: "Nation States"
      description: "Countries and sovereign entities"
      depth: 1
      
    - id: "federal"
      name: "Federal States"
      description: "States, Länder, Provinces, Regions"
      depth: 2
      
    - id: "institutional"
      name: "Institutions"
      description: "Ministries, Agencies, Departments"
      depth: 3
  
  # Define entity types per level
  entity_types:
    global:
      - id: "international_org"
        name: "International Organization"
        labels: ["GlobalOrganization", "InternationalBody"]
        required_fields: ["name", "founded", "headquarters"]
        optional_fields: ["member_count", "mandate", "charter_url"]
        
      - id: "economic_union"
        name: "Economic Union"
        labels: ["EconomicUnion", "RegionalBloc"]
        required_fields: ["name", "founded"]
        optional_fields: ["member_count", "currency"]
    
    national:
      - id: "nation_state"
        name: "Nation State"
        labels: ["Nation", "Sovereignty"]
        required_fields: ["name", "iso_code", "capital"]
        optional_fields: ["population", "area_sq_km", "government_type"]
    
    federal:
      - id: "federal_state"
        name: "Federal State"
        labels: ["FederalState", "Region"]
        required_fields: ["name", "country"]
        optional_fields: ["capital", "population"]
    
    institutional:
      - id: "ministry"
        name: "Ministry"
        labels: ["Ministry", "Government"]
        required_fields: ["name", "level", "country"]
        optional_fields: ["minister", "budget", "employees"]
  
  # Define relationship types
  relationships:
    - id: "member_of"
      name: "MEMBER_OF"
      from_level: "national"
      to_level: "global"
      description: "Nation is member of global organization"
      required_fields: ["joined_date"]
      optional_fields: ["contribution", "voting_rights"]
      
    - id: "contains"
      name: "CONTAINS"
      from_level: "national"
      to_level: "federal"
      description: "Nation contains federal state"
      
    - id: "operates"
      name: "OPERATES"
      from_level: "federal"
      to_level: "institutional"
      description: "Federal state operates institution"
      
    - id: "reports_to"
      name: "REPORTS_TO"
      from_level: "*"
      to_level: "*"
      description: "Generic reporting relationship"
      
    - id: "delegates_authority"
      name: "DELEGATES_AUTHORITY"
      from_level: "*"
      to_level: "*"
      description: "Authority delegation from higher to lower"
      required_fields: ["authority_type", "scope"]
```

### 1.2 Corporate Hierarchy Example

```yaml
# config/hierarchies/corporate.yaml
hierarchy:
  name: "Corporate Structure"
  description: "Multi-national corporate organization"
  graph_id: "corporate"
  
  levels:
    - id: "holding"
      name: "Holding Company"
      depth: 0
      
    - id: "division"
      name: "Business Division"
      depth: 1
      
    - id: "department"
      name: "Department"
      depth: 2
      
    - id: "team"
      name: "Team"
      depth: 3
  
  entity_types:
    holding:
      - id: "parent_company"
        name: "Parent Company"
        labels: ["Holding", "Corporation"]
        required_fields: ["name", "stock_ticker", "ceo"]
        
    division:
      - id: "business_unit"
        name: "Business Unit"
        labels: ["Division", "BusinessUnit"]
        required_fields: ["name", "president"]
        
    department:
      - id: "dept"
        name: "Department"
        labels: ["Department"]
        required_fields: ["name", "manager"]
        
    team:
      - id: "work_team"
        name: "Work Team"
        labels: ["Team"]
        required_fields: ["name", "lead"]
  
  relationships:
    - id: "owns"
      name: "OWNS"
      from_level: "holding"
      to_level: "division"
      
    - id: "manages"
      name: "MANAGES"
      from_level: "division"
      to_level: "department"
      
    - id: "supervises"
      name: "SUPERVISES"
      from_level: "department"
      to_level: "team"
```

---

## 2. Implementation

### 2.1 HierarchyDefinition Class

```cpp
#pragma once

#include "storage/base_entity.h"
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <yaml-cpp/yaml.h>

namespace themis {
namespace hierarchy {

/**
 * Hierarchical level definition
 */
struct LevelDefinition {
    std::string id;
    std::string name;
    std::string description;
    int depth;
    
    static LevelDefinition fromYAML(const YAML::Node& node);
};

/**
 * Entity type definition for a specific level
 */
struct EntityTypeDefinition {
    std::string id;
    std::string name;
    std::vector<std::string> labels;
    std::vector<std::string> requiredFields;
    std::vector<std::string> optionalFields;
    
    static EntityTypeDefinition fromYAML(const YAML::Node& node);
};

/**
 * Relationship type definition
 */
struct RelationshipDefinition {
    std::string id;
    std::string name;
    std::string fromLevel;  // "*" means any level
    std::string toLevel;    // "*" means any level
    std::string description;
    std::vector<std::string> requiredFields;
    std::vector<std::string> optionalFields;
    
    static RelationshipDefinition fromYAML(const YAML::Node& node);
};

/**
 * Complete hierarchy definition
 * Loaded from YAML or VCC_VPB process builder
 */
class HierarchyDefinition {
public:
    HierarchyDefinition() = default;
    
    /**
     * Load from YAML file
     */
    static HierarchyDefinition loadFromYAML(const std::string& filepath);
    
    /**
     * Load from YAML string
     */
    static HierarchyDefinition loadFromYAMLString(const std::string& yaml);
    
    /**
     * Load from VCC_VPB process definition
     */
    static HierarchyDefinition loadFromVCC_VPB(const std::string& processId);
    
    /**
     * Save to YAML file
     */
    bool saveToYAML(const std::string& filepath) const;
    
    // Getters
    std::string getName() const { return name_; }
    std::string getDescription() const { return description_; }
    std::string getGraphId() const { return graphId_; }
    
    const std::vector<LevelDefinition>& getLevels() const { return levels_; }
    const std::map<std::string, std::vector<EntityTypeDefinition>>& getEntityTypes() const { 
        return entityTypes_; 
    }
    const std::vector<RelationshipDefinition>& getRelationships() const { 
        return relationships_; 
    }
    
    /**
     * Find level by ID
     */
    std::optional<LevelDefinition> getLevel(const std::string& levelId) const;
    
    /**
     * Find entity type by ID and level
     */
    std::optional<EntityTypeDefinition> getEntityType(
        const std::string& levelId,
        const std::string& typeId
    ) const;
    
    /**
     * Find relationship by ID
     */
    std::optional<RelationshipDefinition> getRelationship(const std::string& relId) const;
    
    /**
     * Validate entity against type definition
     */
    bool validateEntity(
        const std::string& levelId,
        const std::string& typeId,
        const BaseEntity& entity
    ) const;
    
    /**
     * Validate relationship
     */
    bool validateRelationship(
        const std::string& relId,
        const std::string& fromLevel,
        const std::string& toLevel
    ) const;
    
private:
    std::string name_;
    std::string description_;
    std::string graphId_;
    
    std::vector<LevelDefinition> levels_;
    std::map<std::string, std::vector<EntityTypeDefinition>> entityTypes_;
    std::vector<RelationshipDefinition> relationships_;
    
    void parseYAML(const YAML::Node& root);
};

} // namespace hierarchy
} // namespace themis
```

### 2.2 Generic Hierarchy Manager

```cpp
#pragma once

#include "hierarchy/hierarchy_definition.h"
#include "index/property_graph.h"

namespace themis {
namespace hierarchy {

/**
 * Generic Hierarchical Network Manager
 * Works with any hierarchy defined via YAML/VCC_VPB
 */
class HierarchyManager {
public:
    using Status = PropertyGraphManager::Status;
    
    /**
     * Constructor with hierarchy definition
     */
    explicit HierarchyManager(
        PropertyGraphManager& pgm,
        const HierarchyDefinition& definition
    );
    
    /**
     * Constructor loading from YAML file
     */
    explicit HierarchyManager(
        PropertyGraphManager& pgm,
        const std::string& yamlFilepath
    );
    
    // === Entity Operations ===
    
    /**
     * Create entity at specific level
     */
    Status createEntity(
        std::string_view entityId,
        std::string_view levelId,
        std::string_view typeId,
        const BaseEntity& properties
    );
    
    /**
     * Update entity
     */
    Status updateEntity(
        std::string_view entityId,
        const BaseEntity& properties
    );
    
    /**
     * Delete entity
     */
    Status deleteEntity(std::string_view entityId);
    
    // === Relationship Operations ===
    
    /**
     * Create relationship between entities
     */
    Status createRelationship(
        std::string_view fromId,
        std::string_view toId,
        std::string_view relationshipType,
        const BaseEntity& properties = BaseEntity("")
    );
    
    /**
     * Delete relationship
     */
    Status deleteRelationship(
        std::string_view fromId,
        std::string_view toId,
        std::string_view relationshipType
    );
    
    // === Query Operations ===
    
    /**
     * Get all entities at a specific level
     */
    std::vector<BaseEntity> getEntitiesByLevel(std::string_view levelId) const;
    
    /**
     * Get hierarchy chain for an entity (bottom-up)
     */
    std::vector<BaseEntity> getHierarchyChain(std::string_view entityId) const;
    
    /**
     * Get all children of an entity
     */
    std::vector<BaseEntity> getChildren(
        std::string_view entityId,
        std::optional<std::string> relationshipType = std::nullopt
    ) const;
    
    /**
     * Get all descendants (recursive)
     */
    std::vector<BaseEntity> getAllDescendants(std::string_view entityId) const;
    
    /**
     * Get parent entity
     */
    std::optional<BaseEntity> getParent(
        std::string_view entityId,
        std::optional<std::string> relationshipType = std::nullopt
    ) const;
    
    /**
     * Get all ancestors (recursive)
     */
    std::vector<BaseEntity> getAllAncestors(std::string_view entityId) const;
    
    /**
     * Find shortest path between two entities
     */
    std::vector<BaseEntity> findPath(
        std::string_view fromId,
        std::string_view toId
    ) const;
    
    // === Analytics ===
    
    /**
     * Get depth of entity in hierarchy
     */
    int getDepth(std::string_view entityId) const;
    
    /**
     * Get subtree statistics
     */
    struct SubtreeStats {
        int totalNodes;
        int maxDepth;
        std::map<std::string, int> nodesByLevel;
    };
    
    SubtreeStats getSubtreeStats(std::string_view rootId) const;
    
    /**
     * Validate entire hierarchy structure
     */
    bool validateHierarchy() const;
    
    // === Configuration ===
    
    const HierarchyDefinition& getDefinition() const { return definition_; }
    std::string_view getGraphId() const { return definition_.getGraphId(); }
    
private:
    PropertyGraphManager& pgm_;
    HierarchyDefinition definition_;
    
    // Helper methods
    Status validateEntityFields(
        const std::string& levelId,
        const std::string& typeId,
        const BaseEntity& entity
    ) const;
    
    BaseEntity enrichEntityWithLabels(
        const std::string& levelId,
        const std::string& typeId,
        const BaseEntity& entity
    ) const;
};

} // namespace hierarchy
} // namespace themis
```

---

## 3. VCC_VPB Process Builder Integration

### 3.1 Process Definition Format

```yaml
# VCC_VPB Process: Government Hierarchy Setup
process:
  id: "gov_hierarchy_setup"
  name: "Government Network Initialization"
  description: "Setup multi-level government structure"
  version: "1.0"
  
  steps:
    - step: "load_hierarchy"
      action: "hierarchy.load_definition"
      params:
        source: "config/hierarchies/government.yaml"
        
    - step: "create_global_orgs"
      action: "hierarchy.create_entities"
      params:
        level: "global"
        entities:
          - id: "un"
            type: "international_org"
            properties:
              name: "United Nations"
              founded: "1945-10-24"
              headquarters: "New York"
              member_count: 193
              
          - id: "eu"
            type: "economic_union"
            properties:
              name: "European Union"
              founded: "1993-11-01"
              member_count: 27
              currency: "EUR"
    
    - step: "create_nations"
      action: "hierarchy.create_entities"
      params:
        level: "national"
        entities:
          - id: "de"
            type: "nation_state"
            properties:
              name: "Germany"
              iso_code: "DEU"
              capital: "Berlin"
              population: 83200000
    
    - step: "create_memberships"
      action: "hierarchy.create_relationships"
      params:
        relationship: "member_of"
        links:
          - from: "de"
            to: "un"
            properties:
              joined_date: "1973-09-18"
          - from: "de"
            to: "eu"
            properties:
              joined_date: "1993-11-01"
              founding_member: true
```

### 3.2 VCC_VPB Loader

```cpp
namespace themis {
namespace hierarchy {

class VCC_VPB_HierarchyLoader {
public:
    /**
     * Load and execute VCC_VPB process for hierarchy setup
     */
    static Status executeProcess(
        HierarchyManager& manager,
        const std::string& processFilepath
    );
    
    /**
     * Load hierarchy definition from VCC_VPB process
     */
    static HierarchyDefinition extractDefinition(
        const std::string& processFilepath
    );
    
private:
    static Status executeStep(
        HierarchyManager& manager,
        const YAML::Node& step
    );
};

} // namespace hierarchy
} // namespace themis
```

---

## 4. Usage Examples

### 4.1 Basic Usage with YAML

```cpp
#include "hierarchy/hierarchy_manager.h"

// Load hierarchy definition
HierarchyManager gov(pgm, "config/hierarchies/government.yaml");

// Create entities
BaseEntity un("un");
un.setField("name", "United Nations");
un.setField("founded", "1945-10-24");
gov.createEntity("un", "global", "international_org", un);

BaseEntity germany("de");
germany.setField("name", "Germany");
germany.setField("iso_code", "DEU");
gov.createEntity("de", "national", "nation_state", germany);

// Create relationship
gov.createRelationship("de", "un", "member_of");

// Query
auto chain = gov.getHierarchyChain("de");
// Returns: [Germany] → [UN]

auto allGlobal = gov.getEntitiesByLevel("global");
// Returns: [UN, EU, NATO, ...]
```

### 4.2 Corporate Hierarchy

```cpp
// Use same code with different YAML!
HierarchyManager corp(pgm, "config/hierarchies/corporate.yaml");

BaseEntity acme("acme");
acme.setField("name", "ACME Corporation");
acme.setField("stock_ticker", "ACME");
corp.createEntity("acme", "holding", "parent_company", acme);

BaseEntity techDiv("tech_div");
techDiv.setField("name", "Technology Division");
corp.createEntity("tech_div", "division", "business_unit", techDiv);

corp.createRelationship("acme", "tech_div", "owns");
```

### 4.3 VCC_VPB Process Execution

```cpp
// Execute complete hierarchy setup from process definition
HierarchyManager manager(pgm, "config/hierarchies/government.yaml");

auto status = VCC_VPB_HierarchyLoader::executeProcess(
    manager,
    "processes/gov_hierarchy_setup.yaml"
);

if (status.ok) {
    std::cout << "Hierarchy initialized successfully" << std::endl;
}
```

---

## 5. AQL Integration

### 5.1 Generic Hierarchy Queries

```sql
-- Works for ANY hierarchy!

-- Get all entities at a level
FOR entity IN @graphId
  FILTER entity._hierarchy_level == @levelId
  RETURN entity

-- Get hierarchy chain
FOR entity IN @graphId
  FILTER entity.id == @entityId
  LET chain = (
    FOR parent IN 1..10 INBOUND entity GRAPH @graphId
      RETURN parent
  )
  RETURN APPEND([entity], chain)

-- Cross-level aggregation
FOR level0 IN @graphId
  FILTER level0._hierarchy_level == "global"
  LET descendants = (
    FOR desc IN 1..10 OUTBOUND level0 GRAPH @graphId
      RETURN desc
  )
  RETURN {
    entity: level0,
    descendant_count: LENGTH(descendants),
    by_level: COUNT_BY_LEVEL(descendants)
  }
```

---

## 6. Benefits of Abstract Model

### 6.1 Flexibility

**Same Code, Different Hierarchies:**
- Government
- Corporate
- Supply Chain
- Healthcare
- Education
- Military
- Religious Organizations
- Sports Leagues
- Academic Institutions

### 6.2 Reusability

**Core Components:**
- `HierarchyManager` - Generic for all hierarchies
- `HierarchyDefinition` - Declarative configuration
- Property Graph - Already flexible

**No Code Changes Needed:**
- Just create new YAML file
- Instant new hierarchy type

### 6.3 Maintainability

**Single Source of Truth:**
- All hierarchy structure in YAML
- Version controlled
- Easy to update
- Self-documenting

---

## 7. Migration from Hard-Coded

```bash
# Old (hard-coded):
GovernmentNetworkManager gov(pgm);
gov.createGlobalOrganization(...);
gov.createNation(...);

# New (configuration-driven):
HierarchyManager gov(pgm, "config/hierarchies/government.yaml");
gov.createEntity(...);  # Generic method

# Same functionality, more flexible!
```

---

## 8. Next Steps

**Implementation Priority:**
1. ✅ YAML Parser (yaml-cpp)
2. ✅ HierarchyDefinition Class
3. ✅ HierarchyManager Class
4. VCC_VPB Loader
5. Example YAML Files
6. Documentation
7. Migration Guide

**Timeline:** 2-3 Wochen

---

**Status:** Ready for Implementation  
**Priority:** P1  
**Dependencies:** yaml-cpp, PropertyGraphManager

**Letzte Aktualisierung:** 20. November 2025
