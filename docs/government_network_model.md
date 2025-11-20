# Government & Institutional Network Model - ThemisDB

**Version:** 1.0  
**Datum:** 20. November 2025  
**Status:** Design & Implementation  
**Use Case:** Multi-Level Government Networks (Global → National → Federal → Institutional)

---

## Executive Summary

Hierarchisches Netzwerk-Modell für Government- und Institutionen-Strukturen mit Multi-Level-Föderationen.

**Kernkonzepte:**
- **Global Layer:** Internationale Organisationen (UN, NATO, EU, WHO)
- **National Layer:** Nationalstaaten und Regierungen
- **Federal Layer:** Bundesländer, Provinzen, States
- **Institutional Layer:** Behörden, Ministerien, Ämter

**Features:**
- Hierarchische Beziehungen mit Vererbung
- Cross-Level Queries
- Jurisdictional Boundaries
- Policy Propagation
- Authority Delegation

---

## 1. Schema-Design

### 1.1 Layer-Hierarchie

```
┌─────────────────────────────────────────┐
│         GLOBAL LAYER                    │
│  (UN, NATO, EU, WHO, IMF, World Bank)   │
└─────────────────┬───────────────────────┘
                  │ MEMBER_OF / GOVERNS
┌─────────────────▼───────────────────────┐
│         NATIONAL LAYER                  │
│  (Germany, USA, France, Japan, etc.)    │
└─────────────────┬───────────────────────┘
                  │ CONTAINS / ADMINISTERS
┌─────────────────▼───────────────────────┐
│         FEDERAL LAYER                   │
│  (Bavaria, California, Île-de-France)   │
└─────────────────┬───────────────────────┘
                  │ HAS_INSTITUTION / OPERATES
┌─────────────────▼───────────────────────┐
│         INSTITUTIONAL LAYER             │
│  (Ministries, Agencies, Departments)    │
└─────────────────────────────────────────┘
```

### 1.2 Entity-Typen

#### Global Entities
```cpp
// Labels: :GlobalOrganization, :InternationalBody
{
    "id": "un",
    "name": "United Nations",
    "_labels": "GlobalOrganization,InternationalBody",
    "type": "international_organization",
    "founded": "1945-10-24",
    "headquarters": "New York, USA",
    "member_count": 193,
    "mandate": "international_peace_security",
    "charter_url": "https://un.org/charter"
}

// Labels: :EconomicUnion, :RegionalBloc
{
    "id": "eu",
    "name": "European Union",
    "_labels": "EconomicUnion,RegionalBloc",
    "type": "supranational_union",
    "founded": "1993-11-01",
    "member_count": 27,
    "currency": "EUR",
    "parliament_seats": 705
}
```

#### National Entities
```cpp
// Labels: :Nation, :Sovereignty
{
    "id": "de",
    "name": "Federal Republic of Germany",
    "_labels": "Nation,Sovereignty,FederalState",
    "iso_code": "DEU",
    "iso_numeric": 276,
    "capital": "Berlin",
    "population": 83200000,
    "area_sq_km": 357022,
    "government_type": "federal_parliamentary_republic",
    "head_of_state": "President",
    "head_of_government": "Chancellor",
    "constitution": "Grundgesetz",
    "federal_structure": true,
    "federal_states_count": 16
}
```

#### Federal Entities (Bundesländer, States, Provinces)
```cpp
// Labels: :FederalState, :Region
{
    "id": "de_by",
    "name": "Bavaria",
    "german_name": "Bayern",
    "_labels": "FederalState,Region,Bundesland",
    "country": "de",
    "capital": "Munich",
    "population": 13140000,
    "area_sq_km": 70550,
    "government_type": "parliamentary_republic",
    "minister_president": "current_mp",
    "parliament_name": "Landtag",
    "parliament_seats": 205
}

// Labels: :State
{
    "id": "us_ca",
    "name": "California",
    "_labels": "State,Region",
    "country": "us",
    "capital": "Sacramento",
    "population": 39500000,
    "area_sq_km": 423970,
    "governor": "current_governor",
    "legislature": "bicameral"
}
```

#### Institutional Entities (Ministerien, Behörden)
```cpp
// Labels: :Ministry, :Government
{
    "id": "de_bmf",
    "name": "Federal Ministry of Finance",
    "german_name": "Bundesministerium der Finanzen",
    "_labels": "Ministry,FederalAgency,Government",
    "country": "de",
    "level": "federal",
    "minister": "current_minister",
    "budget_eur": 500000000,
    "employees": 1500,
    "responsibilities": ["taxation", "budget", "fiscal_policy"],
    "website": "https://bundesfinanzministerium.de"
}

// Labels: :Agency, :RegulatoryBody
{
    "id": "de_by_stmwi",
    "name": "Bavarian State Ministry of Economic Affairs",
    "german_name": "Bayerisches Staatsministerium für Wirtschaft",
    "_labels": "Ministry,StateAgency,RegulatoryBody",
    "country": "de",
    "federal_state": "de_by",
    "level": "state",
    "minister": "state_minister",
    "responsibilities": ["economy", "innovation", "tourism"]
}
```

### 1.3 Relationship-Typen

```cpp
// Global → National
:MEMBER_OF        // Nation → Global Organization
:GOVERNED_BY      // Global Organization → Nation (UN Security Council)
:CONTRIBUTES_TO   // Nation → Global Organization (funding)

// National → Federal
:CONTAINS         // Nation → Federal State
:ADMINISTERS      // Nation → Federal State
:DELEGATES_TO     // National Gov → Federal Gov (powers)

// Federal → Institutional
:HAS_MINISTRY     // Federal State → Ministry
:OPERATES         // Federal State → Agency
:SUPERVISES       // Ministry → Agency

// Cross-Level
:COORDINATES_WITH // Any Level → Any Level
:REPORTS_TO       // Lower → Higher
:DELEGATES_AUTHORITY // Higher → Lower

// Policy & Jurisdiction
:ENFORCES_LAW     // Institution → Jurisdiction
:HAS_JURISDICTION // Institution → Geographic Area
:IMPLEMENTS_POLICY // Institution → Policy
```

---

## 2. Implementierung

### 2.1 GovernmentNetworkManager

```cpp
#pragma once

#include "index/property_graph.h"
#include <string>
#include <vector>

namespace themis {
namespace government {

enum class GovernmentLevel {
    GLOBAL,       // UN, NATO, EU, WHO
    NATIONAL,     // Countries
    FEDERAL,      // States, Länder, Provinces
    INSTITUTIONAL // Ministries, Agencies
};

enum class OrganizationType {
    INTERNATIONAL_ORG,    // UN, NATO
    ECONOMIC_UNION,       // EU, ASEAN
    NATION_STATE,         // Countries
    FEDERAL_STATE,        // Bavaria, California
    MINISTRY,             // BMF, DoD
    AGENCY,              // BfV, FBI
    REGULATORY_BODY,     // BaFin, SEC
    LEGISLATIVE_BODY,    // Bundestag, Congress
    JUDICIAL_BODY        // BVerfG, Supreme Court
};

class GovernmentNetworkManager {
public:
    explicit GovernmentNetworkManager(PropertyGraphManager& pgm);
    
    // === Entity Creation ===
    
    // Create global organization
    Status createGlobalOrganization(
        std::string_view id,
        std::string_view name,
        OrganizationType type,
        const BaseEntity& properties
    );
    
    // Create nation
    Status createNation(
        std::string_view id,
        std::string_view name,
        std::string_view isoCode,
        const BaseEntity& properties
    );
    
    // Create federal state
    Status createFederalState(
        std::string_view id,
        std::string_view name,
        std::string_view nationId,
        const BaseEntity& properties
    );
    
    // Create institution (ministry, agency)
    Status createInstitution(
        std::string_view id,
        std::string_view name,
        GovernmentLevel level,
        std::string_view parentId,  // Nation or Federal State
        OrganizationType type,
        const BaseEntity& properties
    );
    
    // === Relationship Management ===
    
    // Nation joins global organization
    Status addMembership(
        std::string_view nationId,
        std::string_view globalOrgId,
        const BaseEntity& membershipProperties = {}
    );
    
    // Delegate authority from higher to lower level
    Status delegateAuthority(
        std::string_view fromId,
        std::string_view toId,
        std::string_view authorityType,
        const BaseEntity& delegationProperties = {}
    );
    
    // Add jurisdictional responsibility
    Status addJurisdiction(
        std::string_view institutionId,
        std::string_view geographicArea,
        const std::vector<std::string>& responsibilities
    );
    
    // === Queries ===
    
    // Get all entities at a specific level
    std::vector<BaseEntity> getEntitiesByLevel(
        GovernmentLevel level,
        std::string_view graphId = "government"
    );
    
    // Get hierarchy chain (e.g., Ministry → Federal State → Nation → Global)
    std::vector<BaseEntity> getHierarchyChain(
        std::string_view entityId,
        std::string_view graphId = "government"
    );
    
    // Get all federal states of a nation
    std::vector<BaseEntity> getFederalStates(
        std::string_view nationId,
        std::string_view graphId = "government"
    );
    
    // Get all institutions under federal state
    std::vector<BaseEntity> getInstitutions(
        std::string_view federalStateId,
        OrganizationType filterType = OrganizationType::MINISTRY,
        std::string_view graphId = "government"
    );
    
    // Get all member nations of global organization
    std::vector<BaseEntity> getMemberNations(
        std::string_view globalOrgId,
        std::string_view graphId = "government"
    );
    
    // Cross-level query: Find all institutions under a nation (all levels)
    std::vector<BaseEntity> getAllInstitutionsUnderNation(
        std::string_view nationId,
        std::string_view graphId = "government"
    );
    
    // Policy propagation: trace policy from global to local
    std::vector<BaseEntity> tracePolicyPropagation(
        std::string_view policyId,
        std::string_view graphId = "government"
    );
    
    // Jurisdictional overlap detection
    std::vector<std::pair<BaseEntity, BaseEntity>> findJurisdictionalOverlaps(
        std::string_view geographicArea,
        std::string_view graphId = "government"
    );
    
    // === Analytics ===
    
    // Calculate hierarchical depth
    int getHierarchicalDepth(std::string_view entityId);
    
    // Find shortest authority path between two entities
    std::vector<BaseEntity> findAuthorityPath(
        std::string_view fromId,
        std::string_view toId
    );
    
    // Analyze inter-governmental collaboration
    struct CollaborationMetrics {
        int directConnections;
        int sharedMemberships;
        double collaborationScore;
    };
    
    CollaborationMetrics analyzeCollaboration(
        std::string_view entity1Id,
        std::string_view entity2Id
    );
    
private:
    PropertyGraphManager& pgm_;
    std::string graphId_ = "government";
    
    std::string levelToLabel(GovernmentLevel level);
    std::string typeToLabel(OrganizationType type);
};

} // namespace government
} // namespace themis
```

### 2.2 Verwendungsbeispiele

```cpp
#include "government/government_network.h"

GovernmentNetworkManager gov(pgm);

// === 1. Create Global Layer ===

// United Nations
BaseEntity un("un");
un.setField("name", "United Nations");
un.setField("founded", "1945-10-24");
un.setField("headquarters", "New York");
un.setField("member_count", 193);
gov.createGlobalOrganization("un", "United Nations", 
    OrganizationType::INTERNATIONAL_ORG, un);

// European Union
BaseEntity eu("eu");
eu.setField("name", "European Union");
eu.setField("founded", "1993-11-01");
eu.setField("member_count", 27);
gov.createGlobalOrganization("eu", "European Union",
    OrganizationType::ECONOMIC_UNION, eu);

// === 2. Create National Layer ===

// Germany
BaseEntity germany("de");
germany.setField("name", "Federal Republic of Germany");
germany.setField("iso_code", "DEU");
germany.setField("capital", "Berlin");
germany.setField("population", 83200000);
germany.setField("federal_structure", true);
gov.createNation("de", "Germany", "DEU", germany);

// USA
BaseEntity usa("us");
usa.setField("name", "United States of America");
usa.setField("iso_code", "USA");
usa.setField("capital", "Washington D.C.");
usa.setField("population", 331000000);
gov.createNation("us", "USA", "USA", usa);

// === 3. Add Memberships ===

gov.addMembership("de", "un");
gov.addMembership("de", "eu");
gov.addMembership("us", "un");

// === 4. Create Federal Layer ===

// Bavaria
BaseEntity bavaria("de_by");
bavaria.setField("name", "Bavaria");
bavaria.setField("german_name", "Bayern");
bavaria.setField("capital", "Munich");
bavaria.setField("population", 13140000);
gov.createFederalState("de_by", "Bavaria", "de", bavaria);

// California
BaseEntity california("us_ca");
california.setField("name", "California");
california.setField("capital", "Sacramento");
california.setField("population", 39500000);
gov.createFederalState("us_ca", "California", "us", california);

// === 5. Create Institutional Layer ===

// Federal Ministry (Germany)
BaseEntity bmf("de_bmf");
bmf.setField("name", "Federal Ministry of Finance");
bmf.setField("german_name", "Bundesministerium der Finanzen");
bmf.setField("budget_eur", 500000000);
gov.createInstitution("de_bmf", "BMF", GovernmentLevel::NATIONAL,
    "de", OrganizationType::MINISTRY, bmf);

// State Ministry (Bavaria)
BaseEntity stmwi("de_by_stmwi");
stmwi.setField("name", "Bavarian Ministry of Economic Affairs");
stmwi.setField("responsibilities", "economy,innovation,tourism");
gov.createInstitution("de_by_stmwi", "StMWI", GovernmentLevel::FEDERAL,
    "de_by", OrganizationType::MINISTRY, stmwi);

// Federal Agency (USA)
BaseEntity dod("us_dod");
dod.setField("name", "Department of Defense");
dod.setField("budget_usd", 715000000000);
dod.setField("employees", 2870000);
gov.createInstitution("us_dod", "DoD", GovernmentLevel::NATIONAL,
    "us", OrganizationType::MINISTRY, dod);

// === 6. Queries ===

// Get hierarchy chain
auto chain = gov.getHierarchyChain("de_by_stmwi");
// Returns: [StMWI] → [Bavaria] → [Germany] → [EU, UN]

// Get all German federal states
auto federalStates = gov.getFederalStates("de");
// Returns: [Bavaria, Baden-Württemberg, Berlin, ...]

// Get all institutions under Germany (all levels)
auto allInstitutions = gov.getAllInstitutionsUnderNation("de");
// Returns: [BMF, BMI, ...] (federal) + [StMWI, ...] (state)

// Find authority path
auto path = gov.findAuthorityPath("de_by_stmwi", "eu");
// Returns: [StMWI] → [Bavaria] → [Germany] → [EU]

// Analyze collaboration
auto metrics = gov.analyzeCollaboration("de", "us");
// Returns: {directConnections: 0, sharedMemberships: 1 (UN), score: 0.5}
```

---

## 3. AQL Integration

### 3.1 Query Examples

```sql
-- Find all federal states in Germany
FOR state IN government
  FILTER state._labels CONTAINS "FederalState"
  FILTER state.country == "de"
  RETURN state.name

-- Hierarchy traversal: EU → Nations → Federal States
FOR org IN government
  FILTER org.id == "eu"
  FOR nation IN 1..1 INBOUND org GRAPH government
    FILTER EDGE_TYPE == "MEMBER_OF"
    FOR federal_state IN 1..1 OUTBOUND nation GRAPH government
      FILTER EDGE_TYPE == "CONTAINS"
      RETURN {
        organization: org.name,
        nation: nation.name,
        federal_state: federal_state.name
      }

-- Find all ministries under Bavaria
FOR state IN government
  FILTER state.id == "de_by"
  FOR ministry IN 1..2 OUTBOUND state GRAPH government
    FILTER ministry._labels CONTAINS "Ministry"
    RETURN ministry

-- Cross-level policy tracking
FOR policy IN policies
  FILTER policy.id == "gdpr"
  FOR entity IN 1..10 ANY policy GRAPH government
    FILTER EDGE_TYPE IN ["IMPLEMENTS_POLICY", "ENFORCES_LAW"]
    RETURN {
      level: entity.level,
      entity: entity.name,
      implementation_status: entity.gdpr_status
    }

-- Jurisdictional overlap detection
FOR inst1 IN government
  FILTER inst1._labels CONTAINS "RegulatoryBody"
  FOR inst2 IN government
    FILTER inst2._labels CONTAINS "RegulatoryBody"
    FILTER inst1.id != inst2.id
    FILTER inst1.jurisdiction OVERLAPS inst2.jurisdiction
    RETURN {inst1: inst1.name, inst2: inst2.name, overlap: INTERSECTION(inst1.jurisdiction, inst2.jurisdiction)}
```

---

## 4. Use Cases

### 4.1 E-Government Platform
- Multi-Level Portal-Verwaltung
- Föderale Datenaustausch
- Cross-Jurisdictional Services

### 4.2 Policy Compliance Tracking
- GDPR-Implementierung über Ebenen
- Policy Propagation Analysis
- Compliance Reporting

### 4.3 Inter-Governmental Collaboration
- Shared Services zwischen Ländern
- EU-Wide Programs Tracking
- International Treaties Management

### 4.4 Organizational Intelligence
- Government Structure Analysis
- Authority Chain Visualization
- Institutional Relationships

---

## 5. Security & Access Control

```cpp
// Row-Level Security per Government Level
class GovernmentAccessControl {
public:
    bool canAccess(
        const User& user,
        const BaseEntity& entity
    ) {
        // Global: Nur UN-Mitarbeiter
        if (entity.level == "global") {
            return user.hasRole("UN_EMPLOYEE");
        }
        
        // National: Nur Citizens + Federal Employees
        if (entity.level == "national") {
            return user.citizenship == entity.country ||
                   user.hasRole("FEDERAL_EMPLOYEE");
        }
        
        // Federal State: Nur State Residents + State Employees
        if (entity.level == "federal") {
            return user.residence == entity.federal_state ||
                   user.hasRole("STATE_EMPLOYEE");
        }
        
        // Institution: Nur Institution Employees
        return user.employedBy == entity.id ||
               user.hasRole("ADMIN");
    }
};
```

---

**Status:** Ready for Implementation  
**Priority:** P1 (Government & Enterprise Feature)  
**Effort:** 4-6 Wochen  
**Dependencies:** PropertyGraphManager (✅ Done)

**Letzte Aktualisierung:** 20. November 2025
