# Styleguide & Konventionen – THEMIS

**Version:** 2.0  
**Status:** Implementiert  
**Letzte Aktualisierung:** 2. November 2025

---

## Übersicht

Dieser Styleguide definiert Coding-Standards, Namenskonventionen, Error Handling und Logging-Richtlinien für das THEMIS-Projekt. Ziel ist Konsistenz, Wartbarkeit und Qualität über alle C++-Module hinweg.

---

## 1. C++ Coding Standards

### 1.1 C++ Version & Compiler

- **Standard:** C++17 (minimum)
- **Compiler:** MSVC (Windows), GCC/Clang (Linux)
- **Features:** STL, `std::optional`, `std::variant`, `std::string_view`, structured bindings

### 1.2 Header Guards

```cpp
#pragma once  // Bevorzugt (MSVC/GCC/Clang)
```

**Alternativ (klassisch):**
```cpp
#ifndef THEMIS_MODULE_HEADER_H
#define THEMIS_MODULE_HEADER_H
// ...
#endif // THEMIS_MODULE_HEADER_H
```

### 1.3 Includes

**Reihenfolge:**
1. Eigener Header (`.cpp` → `.h`)
2. Themis-Headers (`include/...`)
3. Externe Libraries (RocksDB, Boost, spdlog)
4. STL-Headers (`<memory>`, `<string>`, etc.)

```cpp
#include "storage/base_entity.h"        // 1. Eigener Header

#include "index/secondary_index.h"      // 2. Themis-Headers
#include "utils/logger.h"

#include <rocksdb/db.h>                 // 3. Externe Libraries
#include <boost/beast/http.hpp>

#include <memory>                       // 4. STL
#include <string>
#include <vector>
```

---

## 2. Naming Conventions

### 2.1 Klassen & Structs

**PascalCase** mit sprechenden Namen:

```cpp
class BaseEntity { };
class SecondaryIndexManager { };
class TransactionManager { };
struct Status { };
struct DataPoint { };
```

### 2.2 Funktionen & Methoden

**camelCase** mit Verben (get/set/create/delete/has/is):

```cpp
class BaseEntity {
public:
    const std::string& getPrimaryKey() const;
    void setPrimaryKey(std::string_view pk);
    bool hasField(std::string_view field_name) const;
    
    static BaseEntity fromJson(std::string_view pk, std::string_view json);
    static BaseEntity deserialize(std::string_view pk, const Blob& blob);
};
```

### 2.3 Variablen

**snake_case** für Member-Variablen (mit Unterstrich-Suffix):

```cpp
class BaseEntity {
private:
    std::string primary_key_;
    Blob blob_;
    Format format_;
    mutable std::shared_ptr<FieldMap> field_cache_;
};
```

**camelCase** für lokale Variablen:

```cpp
void processEntity() {
    std::string entityKey = makeKey("users", "alice");
    auto blob = db.get(entityKey);
    BaseEntity entity = BaseEntity::deserialize("alice", *blob);
}
```

### 2.4 Konstanten & Enums

**UPPER_CASE** für Makros/Konstanten:

```cpp
#define THEMIS_INFO(...) ::themis::utils::Logger::info(__VA_ARGS__)

static constexpr const char* KEY_PREFIX = "ts:";
static constexpr size_t MAX_BATCH_SIZE = 1000;
```

**PascalCase** für Enums:

```cpp
enum class Format {
    BINARY,
    JSON
};

enum class Level {
    TRACE, DEBUG, INFO, WARN, ERROR, CRITICAL
};
```

### 2.5 Namespaces

**lowercase** mit Sub-Namespaces:

```cpp
namespace themis {
namespace utils {

class Logger { };

} // namespace utils
} // namespace themis
```

**Verwendung:**
```cpp
using themis::BaseEntity;
using themis::utils::Logger;
```

---

## 3. Error Handling

### 3.1 Status-Objekt (kein Exceptions)

Alle öffentlichen APIs verwenden `Status`-Objekte statt Exceptions:

```cpp
struct Status {
    bool ok = true;
    std::string message;
    
    static Status OK() { return {}; }
    static Status Error(std::string msg) { return Status{false, std::move(msg)}; }
};
```

**Verwendung:**

```cpp
// ✅ RICHTIG: Status zurückgeben
Status createIndex(std::string_view table, std::string_view column) {
    if (table.empty()) {
        return Status::Error("createIndex: table darf nicht leer sein");
    }
    // ...
    return Status::OK();
}

// Aufrufer prüft Status
auto status = idx.createIndex("users", "age");
if (!status.ok) {
    THEMIS_ERROR("Index creation failed: {}", status.message);
    return;
}
```

### 3.2 std::optional für optionale Werte

```cpp
// ✅ RICHTIG: optional für fehlende Werte
std::optional<std::string> getFieldAsString(std::string_view field_name) const;

auto name = entity.getFieldAsString("name");
if (name) {
    std::cout << *name << "\n";
}

// ❌ FALSCH: nullptr zurückgeben (unsicher)
std::string* getFieldAsString(std::string_view field_name) const;  // NEIN!
```

### 3.3 std::pair für Status + Wert

```cpp
// Query mit Result
std::pair<Status, std::vector<std::string>> scanKeysEqual(
    std::string_view table,
    std::string_view column,
    std::string_view value
) const;

// Aufrufer
auto [status, keys] = idx.scanKeysEqual("users", "age", "30");
if (!status.ok) {
    THEMIS_ERROR("Scan failed: {}", status.message);
    return;
}
for (const auto& key : keys) {
    // Process keys
}
```

---

## 4. Logging

### 4.1 Logger-Makros

**Verfügbare Level:**

```cpp
THEMIS_TRACE(...)     // Detaillierte Debug-Info (selten verwendet)
THEMIS_DEBUG(...)     // Debug-Informationen (Development)
THEMIS_INFO(...)      // Allgemeine Informationen
THEMIS_WARN(...)      // Warnungen (nicht kritisch)
THEMIS_ERROR(...)     // Fehler (kritisch, aber nicht fatal)
THEMIS_CRITICAL(...)  // Fatale Fehler (Server-Absturz)
```

### 4.2 Logging-Best Practices

```cpp
// ✅ RICHTIG: Strukturiertes Logging mit fmt-Syntax
THEMIS_INFO("Index erstellt: {}.{}", table, column);
THEMIS_ERROR("Put fehlgeschlagen: {}, Key: {}", status.message, key);
THEMIS_DEBUG("Cache hit: {}, Size: {} bytes", pk, blob.size());

// ✅ RICHTIG: Sensible Daten vermeiden
THEMIS_INFO("User authenticated: id={}", userId);  // OK
THEMIS_ERROR("Auth failed for user: {}", username);  // ❌ PII!

// ✅ RICHTIG: Error-Kontext
if (!db.put(key, value)) {
    THEMIS_ERROR("RocksDB put failed: key={}, table={}", key, table);
}

// ❌ FALSCH: std::cout/printf verwenden
std::cout << "Index created\n";  // NEIN! Nutze Logger
```

### 4.3 Log-Initialisierung

```cpp
// main_server.cpp
Logger::init("themis_server.log", Logger::Level::INFO);

// Runtime-Änderung
Logger::setLevel(Logger::Level::DEBUG);
Logger::setPattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [thread %t] %v");
```

---

## 5. Code-Struktur

### 5.1 Klassen-Layout

```cpp
class SecondaryIndexManager {
public:
    // 1. Nested Types
    struct Status { };
    enum class IndexType { };
    
    // 2. Konstruktoren
    explicit SecondaryIndexManager(RocksDBWrapper& db);
    
    // 3. Öffentliche API (alphabetisch oder logisch gruppiert)
    Status createIndex(std::string_view table, std::string_view column);
    Status dropIndex(std::string_view table, std::string_view column);
    bool hasIndex(std::string_view table, std::string_view column) const;
    
private:
    // 4. Private Helper-Methoden
    std::string makeIndexKey_(std::string_view table, std::string_view column);
    
    // 5. Member-Variablen (mit Unterstrich-Suffix)
    RocksDBWrapper& db_;
    std::unordered_set<std::string> indexed_columns_;
};
```

### 5.2 Funktions-Reihenfolge

1. **Öffentliche API** (Header + Implementierung)
2. **Private Helper** (nur Implementierung)
3. **Static Utilities** (am Ende)

### 5.3 Kommentare

```cpp
// ✅ RICHTIG: Doxygen-Style für öffentliche APIs
/**
 * @brief Create secondary index on table column
 * @param table Table name
 * @param column Column name
 * @param unique If true, enforce unique values
 * @return Status OK or Error with message
 */
Status createIndex(std::string_view table, std::string_view column, bool unique = false);

// ✅ RICHTIG: Inline-Kommentare für komplexe Logik
// Calculate expire timestamp: now + TTL seconds
auto now = std::chrono::system_clock::now();
int64_t expireTimestamp = currentTimestamp + ttlSeconds;

// ❌ FALSCH: Offensichtliches kommentieren
i++;  // increment i
```

---

## 6. Speicher-Management

### 6.1 Smart Pointers

```cpp
// ✅ RICHTIG: std::unique_ptr für Ownership
std::unique_ptr<BaseEntity> entity = std::make_unique<BaseEntity>("alice");

// ✅ RICHTIG: std::shared_ptr für Shared Ownership
mutable std::shared_ptr<FieldMap> field_cache_;

// ❌ FALSCH: Raw Pointers (außer für Nicht-Owning-References)
BaseEntity* entity = new BaseEntity("alice");  // NEIN!
```

### 6.2 String-Handling

```cpp
// ✅ RICHTIG: std::string_view für Read-Only-Parameter
void processKey(std::string_view key);

// ✅ RICHTIG: std::string für Ownership
std::string makeKey(std::string_view table, std::string_view pk) {
    return std::string(table) + ":" + std::string(pk);
}

// ❌ FALSCH: const char* (unsicher bei temporären Strings)
void processKey(const char* key);  // Verwende string_view!
```

---

## 7. Testing & Assertions

### 7.1 Unit Tests (Google Test)

```cpp
TEST_F(SecondaryIndexTest, CreateIndex) {
    auto status = idx_->createIndex("users", "age");
    ASSERT_TRUE(status.ok);
    
    EXPECT_TRUE(idx_->hasIndex("users", "age"));
}

TEST_F(SecondaryIndexTest, CreateIndex_EmptyTable) {
    auto status = idx_->createIndex("", "age");
    EXPECT_FALSE(status.ok);
    EXPECT_NE(status.message.find("table"), std::string::npos);
}
```

### 7.2 Runtime-Assertions (Development)

```cpp
#include <cassert>

// Nur in Debug-Builds
assert(pk.size() > 0 && "Primary key must not be empty");
assert(dim > 0 && "Vector dimension must be positive");
```

---

## 8. Dokumentations-Standards

### 8.1 Sprache

- **Code-Kommentare:** Deutsch (Doku-Konsistenz)
- **API-Doku (Doxygen):** Deutsch
- **Logs:** Deutsch (Fehlermeldungen)
- **Variablen/Funktionen:** Englisch (etablierte Begriffe wie `getPrimaryKey`, `serialize`)

### 8.2 Markdown-Dokumentation

- **Dateinamen:** `kebab-case.md` oder `snake_case.md` (z. B. `base_entity.md`)
- **Überschriften:** Eine `#` pro Datei, dann `##`-Abschnitte
- **Code-Blöcke:** Sprache angeben (```cpp, ```json, ```http)
- **Verweise:** Backticks für Code/Dateien (`src/server/http_server.cpp`)

### 8.3 Beispiel

````markdown
## BaseEntity – Verwendung

```cpp
// Entity erstellen
BaseEntity user = BaseEntity::fromJson("alice", json_string);

// Feld lesen
auto name = user.getFieldAsString("name");
if (name) {
    THEMIS_INFO("User: {}", *name);
}
```

**Siehe auch:** [RocksDB Storage](storage/rocksdb_layout.md)
````

---

## 9. Performance-Best Practices

### 9.1 Vermeidung von Kopien

```cpp
// ✅ RICHTIG: string_view für Parameter (keine Kopie)
void processKey(std::string_view key);

// ✅ RICHTIG: const& für große Objekte
void processEntity(const BaseEntity& entity);

// ❌ FALSCH: Pass-by-Value für große Objekte
void processEntity(BaseEntity entity);  // Kopiert Blob!
```

### 9.2 Reserve für Vektoren

```cpp
// ✅ RICHTIG: reserve() vor Push-Schleife
std::vector<std::string> keys;
keys.reserve(expectedSize);
for (...) {
    keys.push_back(key);
}
```

### 9.3 Move-Semantik

```cpp
// ✅ RICHTIG: std::move für Ownership-Transfer
std::string value = std::move(tempValue);
batch->put(key, std::move(blob));

// ✅ RICHTIG: Return-Value-Optimization (RVO)
BaseEntity createEntity() {
    BaseEntity entity("alice");
    // ... fill fields
    return entity;  // RVO, keine Kopie
}
```

---

## 10. Definition of Done (Code)

Bevor Code committed wird:

- [ ] **Kompiliert** ohne Warnings (MSVC `/W4`, GCC `-Wall -Wextra`)
- [ ] **Unit Tests** vorhanden und grün
- [ ] **Logging** an kritischen Stellen (Error Paths)
- [ ] **Status-Objekt** für Fehlerbehandlung (keine Exceptions in öffentlichen APIs)
- [ ] **Doxygen-Kommentare** für öffentliche APIs
- [ ] **Code-Review** abgeschlossen
- [ ] **Dokumentation** aktualisiert (wenn nötig)

---

## 11. Definition of Done (Dokumentation)

Bevor Doku als "fertig" gilt:

- [ ] **Inhalt korrekt** (mit Source-Code abgeglichen)
- [ ] **Beispiele valide** (kompilierbar/ausführbar)
- [ ] **Interne Verweise** funktionieren
- [ ] **Navigation** in `mkdocs.yml` verlinkt
- [ ] **Rechtschreibung** geprüft (DE)

---

## Referenzen

- **Logger:** `include/utils/logger.h`
- **Status Pattern:** `include/index/secondary_index.h` (Status struct)
- **BaseEntity:** `include/storage/base_entity.h`
- **Google Test:** https://github.com/google/googletest
- **spdlog:** https://github.com/gabime/spdlog
