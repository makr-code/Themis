# RBAC - Role-Based Access Control

Umfassende rollenbasierte Zugriffskontrolle mit Hierarchien, Vererbung und feingranularen Berechtigungen.

## Übersicht

Das RBAC-System bietet:
- **Rollenbasierte Zugriffskontrolle**: Admin, Operator, Analyst, Readonly (erweiterbar)
- **Rollenhierarchie**: Vererbung von Berechtigungen (Admin → Operator → Analyst → Readonly)
- **Resource-basierte Permissions**: Feingranulare Kontrolle über Ressourcen und Aktionen
- **Wildcard-Support**: `*` für alle Ressourcen oder Aktionen
- **JSON/YAML-Konfiguration**: Flexible Rollendefinition
- **User-Role Mapping**: Verwaltung von Benutzern und ihren Rollen

## Konzepte

### Permission (Berechtigung)

Eine Permission besteht aus:
- **resource**: Ressource (z.B. `data`, `keys`, `config`, `audit`, `*`)
- **action**: Aktion (z.B. `read`, `write`, `delete`, `rotate`, `*`)

Beispiele:
```cpp
{"data", "read"}      // Daten lesen
{"keys", "rotate"}    // Schlüssel rotieren
{"config", "*"}       // Alle Aktionen auf Konfiguration
{"*", "*"}            // Alle Ressourcen, alle Aktionen (Superuser)
```

### Role (Rolle)

Eine Rolle gruppiert Permissions und kann von anderen Rollen erben:

```json
{
  "name": "operator",
  "description": "Operator with data and key management",
  "permissions": [
    {"resource": "data", "action": "read"},
    {"resource": "data", "action": "write"},
    {"resource": "keys", "action": "rotate"}
  ],
  "inherits": ["analyst"]
}
```

### Rollenhierarchie

```
admin (Superuser)
  ├─ operator (Data + Key Management)
  │   └─ analyst (Read-only Data + Audit)
  │       └─ readonly (Minimal Read Access)
  └─ ...
```

## Konfiguration

### Environment Variables

```bash
# RBAC Konfigurationsdatei
export THEMIS_RBAC_CONFIG=/etc/themis/rbac.json

# User-Role Mapping
export THEMIS_RBAC_USERS=/etc/themis/users.json

# Features
export THEMIS_RBAC_ENABLE_INHERITANCE=true
export THEMIS_RBAC_ENABLE_WILDCARDS=true
```

### Rollen-Konfiguration (JSON)

**`/etc/themis/rbac.json`**:
```json
{
  "roles": [
    {
      "name": "admin",
      "description": "Administrator with full system access",
      "permissions": [
        {"resource": "*", "action": "*"}
      ],
      "inherits": []
    },
    {
      "name": "operator",
      "description": "Operator with data and key management",
      "permissions": [
        {"resource": "data", "action": "read"},
        {"resource": "data", "action": "write"},
        {"resource": "data", "action": "delete"},
        {"resource": "keys", "action": "read"},
        {"resource": "keys", "action": "rotate"},
        {"resource": "audit", "action": "read"}
      ],
      "inherits": ["analyst"]
    },
    {
      "name": "analyst",
      "description": "Analyst with read-only data access",
      "permissions": [
        {"resource": "data", "action": "read"},
        {"resource": "audit", "action": "read"},
        {"resource": "metrics", "action": "read"}
      ],
      "inherits": ["readonly"]
    },
    {
      "name": "readonly",
      "description": "Read-only user",
      "permissions": [
        {"resource": "metrics", "action": "read"},
        {"resource": "health", "action": "read"}
      ],
      "inherits": []
    }
  ]
}
```

### User-Role Mapping (JSON)

**`/etc/themis/users.json`**:
```json
{
  "users": [
    {
      "user_id": "alice@example.com",
      "roles": ["admin"],
      "attributes": {
        "department": "IT",
        "location": "DE"
      }
    },
    {
      "user_id": "bob@example.com",
      "roles": ["operator"],
      "attributes": {
        "department": "Operations"
      }
    },
    {
      "user_id": "charlie@example.com",
      "roles": ["analyst", "readonly"],
      "attributes": {
        "department": "Analytics"
      }
    }
  ]
}
```

## API Verwendung

### Programmatische Konfiguration

```cpp
#include "security/rbac.h"

using namespace themis::security;

// RBAC initialisieren
RBACConfig config;
config.config_path = "/etc/themis/rbac.json";
config.enable_role_inheritance = true;
config.enable_resource_wildcards = true;

RBAC rbac(config);

// User-Role Store laden
UserRoleStore users;
users.load("/etc/themis/users.json");
```

### Permission-Checks

```cpp
// Benutzer-Rollen abrufen
auto user_roles = users.getUserRoles("alice@example.com");
// => ["admin"]

// Permission prüfen
bool can_write = rbac.checkPermission(user_roles, "data", "write");
// => true (admin hat *)

user_roles = users.getUserRoles("charlie@example.com");
// => ["analyst", "readonly"]

can_write = rbac.checkPermission(user_roles, "data", "write");
// => false (analyst hat nur read)

bool can_read = rbac.checkPermission(user_roles, "data", "read");
// => true (analyst hat read)
```

### Rolle hinzufügen/ändern

```cpp
// Neue Rolle definieren
Role custom_role;
custom_role.name = "data_engineer";
custom_role.description = "Data engineer with ETL permissions";
custom_role.permissions = {
    {"data", "read"},
    {"data", "write"},
    {"data", "bulk_export"}
};
custom_role.inherits = {"analyst"};

rbac.addRole(custom_role);

// Speichern
rbac.saveConfig("/etc/themis/rbac.json");
```

### User-Rolle zuweisen

```cpp
// Rolle zuweisen
users.assignRole("dave@example.com", "data_engineer");

// Rolle entziehen
users.revokeRole("dave@example.com", "readonly");

// Speichern
users.save("/etc/themis/users.json");
```

### Effektive Permissions anzeigen

```cpp
auto user_roles = users.getUserRoles("bob@example.com");
auto permissions = rbac.getUserPermissions(user_roles);

for (const auto& perm : permissions) {
    std::cout << perm.toString() << "\n";
    // data:read
    // data:write
    // keys:rotate
    // audit:read (inherited from analyst)
    // metrics:read (inherited from readonly)
    // ...
}
```

## Integration mit HTTP Server

### AuthMiddleware Integration

```cpp
#include "server/auth_middleware.h"
#include "security/rbac.h"

// RBAC initialisieren
auto rbac = std::make_shared<RBAC>(rbac_config);
auto users = std::make_shared<UserRoleStore>();
users->load("/etc/themis/users.json");

// In HTTP Handler
auto authorize_rbac = [rbac, users](const std::string& user_id, 
                                    const std::string& resource, 
                                    const std::string& action) -> bool {
    auto roles = users->getUserRoles(user_id);
    return rbac->checkPermission(roles, resource, action);
};

// Beispiel: Data-Zugriff autorisieren
if (!authorize_rbac(auth_result.user_id, "data", "write")) {
    return json_error(403, "Forbidden: Insufficient permissions");
}
```

### REST API Endpunkte

```cpp
// GET /api/rbac/roles - Liste alle Rollen
server.route("GET", "/api/rbac/roles", [&rbac](auto req) {
    auto roles = rbac->listRoles();
    return json_response({{"roles", roles}});
});

// GET /api/rbac/roles/:name - Rollendetails
server.route("GET", "/api/rbac/roles/:name", [&rbac](auto req) {
    auto role = rbac->getRole(req.params["name"]);
    if (!role) {
        return json_error(404, "Role not found");
    }
    return json_response(role->toJson());
});

// POST /api/rbac/roles - Rolle erstellen
server.route("POST", "/api/rbac/roles", [&rbac](auto req) {
    auto role = Role::fromJson(req.body);
    rbac->addRole(role);
    rbac->saveConfig("/etc/themis/rbac.json");
    return json_response({{"ok", true}}, 201);
});

// DELETE /api/rbac/roles/:name - Rolle löschen
server.route("DELETE", "/api/rbac/roles/:name", [&rbac](auto req) {
    rbac->removeRole(req.params["name"]);
    rbac->saveConfig("/etc/themis/rbac.json");
    return json_response({{"ok", true}});
});

// GET /api/rbac/users/:user_id/roles - Benutzer-Rollen
server.route("GET", "/api/rbac/users/:user_id/roles", [&users](auto req) {
    auto roles = users->getUserRoles(req.params["user_id"]);
    return json_response({{"user_id", req.params["user_id"]}, {"roles", roles}});
});

// POST /api/rbac/users/:user_id/roles - Rolle zuweisen
server.route("POST", "/api/rbac/users/:user_id/roles", [&users](auto req) {
    auto role = req.body["role"].get<std::string>();
    users->assignRole(req.params["user_id"], role);
    users->save("/etc/themis/users.json");
    return json_response({{"ok", true}});
});
```

## Built-in Rollen

### admin
- **Beschreibung**: Voller Systemzugriff
- **Permissions**: `*:*` (alle Ressourcen, alle Aktionen)
- **Erbt**: -

### operator
- **Beschreibung**: Daten- und Schlüsselverwaltung
- **Permissions**:
  - `data:read`, `data:write`, `data:delete`
  - `keys:read`, `keys:rotate`
  - `audit:read`
- **Erbt**: analyst

### analyst
- **Beschreibung**: Read-only Datenzugriff
- **Permissions**:
  - `data:read`
  - `audit:read`
  - `metrics:read`
- **Erbt**: readonly

### readonly
- **Beschreibung**: Minimaler Read-Access
- **Permissions**:
  - `metrics:read`
  - `health:read`
- **Erbt**: -

## Ressourcen & Aktionen

### Standard-Ressourcen

| Ressource | Beschreibung | Typische Aktionen |
|-----------|--------------|-------------------|
| `data` | Datenbankdaten | `read`, `write`, `delete`, `bulk_export` |
| `keys` | Verschlüsselungsschlüssel | `read`, `create`, `rotate`, `delete` |
| `config` | Systemkonfiguration | `read`, `write` |
| `audit` | Audit-Logs | `read` |
| `metrics` | Metriken/Monitoring | `read` |
| `health` | Health-Check | `read` |
| `users` | Benutzerverwaltung | `read`, `write`, `delete` |
| `roles` | Rollenverwaltung | `read`, `write`, `delete` |
| `*` | Wildcard: Alle Ressourcen | - |

### Standard-Aktionen

| Aktion | Beschreibung |
|--------|--------------|
| `read` | Lesezugriff |
| `write` | Schreibzugriff (Erstellen/Ändern) |
| `delete` | Löschen |
| `create` | Explizites Erstellen |
| `rotate` | Schlüsselrotation |
| `bulk_export` | Massenexport |
| `*` | Wildcard: Alle Aktionen |

## Best Practices

### 1. Principle of Least Privilege

Weise Benutzern nur die minimal notwendigen Rollen zu:

```cpp
// ❌ Schlecht: Zu viele Rechte
users.assignRole("analyst@example.com", "admin");

// ✅ Gut: Nur notwendige Rechte
users.assignRole("analyst@example.com", "analyst");
```

### 2. Rollenhierarchie nutzen

Definiere spezifische Rollen, die von generischen erben:

```json
{
  "name": "data_scientist",
  "permissions": [
    {"resource": "models", "action": "read"},
    {"resource": "models", "action": "train"}
  ],
  "inherits": ["analyst"]  // Erbt data:read, audit:read, etc.
}
```

### 3. Regelmäßige Audits

Überprüfe regelmäßig User-Role-Assignments:

```cpp
// Alle Benutzer mit Admin-Rolle
auto admins = users.getRoleUsers("admin");
for (const auto& user_id : admins) {
    THEMIS_INFO("Admin user: {}", user_id);
}
```

### 4. Audit Logging für RBAC-Änderungen

```cpp
// Rolle zuweisen + Audit Log
users.assignRole(user_id, role);
audit_logger.logSecurityEvent(
    SecurityEventType::ROLE_CHANGED,
    admin_user_id,
    user_id,
    {{"role", role}, {"action", "assigned"}}
);
```

### 5. Segregation of Duties

Trenne kritische Rollen:

```cpp
// ❌ Schlecht: Eine Person hat alle Rollen
users.assignRole("alice@example.com", "admin");
users.assignRole("alice@example.com", "auditor");

// ✅ Gut: Verschiedene Personen
users.assignRole("alice@example.com", "admin");
users.assignRole("bob@example.com", "auditor");
```

## Troubleshooting

### Permission Denied trotz Rolle

**Problem**: Benutzer hat Rolle, aber Permission-Check schlägt fehl.

**Diagnose**:
```cpp
auto user = users.getUser("alice@example.com");
if (!user) {
    THEMIS_ERROR("User not found");
}

auto roles = user->roles;
THEMIS_INFO("User roles: {}", fmt::join(roles, ", "));

auto permissions = rbac.getUserPermissions(roles);
for (const auto& perm : permissions) {
    THEMIS_INFO("Permission: {}", perm.toString());
}
```

**Lösungen**:
- Rolle existiert nicht → `rbac.getRole(role_name)`
- Rollenhierarchie zyklisch → `rbac.validateRoleHierarchy()`
- Permission falsch geschrieben → Groß-/Kleinschreibung prüfen

### Zyklische Rollenhierarchie

**Problem**: Rolle A erbt von B, B erbt von A.

**Lösung**:
```cpp
bool valid = rbac.validateRoleHierarchy();
if (!valid) {
    auto hierarchy = rbac.getRoleHierarchy();
    THEMIS_ERROR("Invalid role hierarchy: {}", hierarchy.dump(2));
}
```

### Konfigurationsdatei nicht gefunden

**Problem**: `loadConfig()` schlägt fehl.

**Lösung**:
```bash
# Dateipfad prüfen
ls -la /etc/themis/rbac.json

# Berechtigungen
sudo chmod 644 /etc/themis/rbac.json
sudo chown themis:themis /etc/themis/rbac.json

# JSON-Syntax validieren
jq . /etc/themis/rbac.json
```

## Performance

### Overhead

- **Permission Check**: O(R × P) mit R=Rollen, P=Permissions pro Rolle
- **Typischer Fall**: < 1ms für 10 Rollen, 50 Permissions
- **Caching**: In-memory, keine Datenbankabfragen

### Optimierung für hohe Last

```cpp
// Permission-Cache (optional)
std::unordered_map<std::string, bool> perm_cache;
std::string cache_key = user_id + ":" + resource + ":" + action;

if (perm_cache.count(cache_key)) {
    return perm_cache[cache_key];
}

bool allowed = rbac.checkPermission(roles, resource, action);
perm_cache[cache_key] = allowed;
return allowed;
```

**Hinweis**: Cache-Invalidierung bei Rollenänderungen beachten!

## Compliance

### GDPR/DSGVO

- Nutze RBAC für Zugriffskontrolle auf personenbezogene Daten
- Definiere `pii:read`, `pii:write`, `pii:delete` Permissions
- Audit Log für alle RBAC-Änderungen

### SOC 2

- Segregation of Duties durch separate Rollen
- Least Privilege Principle
- Audit Trail für Rollenänderungen

### HIPAA

- PHI-Zugriff nur für autorisierte Rollen
- `phi:read`, `phi:write` Permissions
- Regelmäßige Access Reviews

## Migration

### Von AuthMiddleware Scopes zu RBAC

**Vorher (Scopes)**:
```cpp
auth.addToken({"token123", "alice@example.com", {"admin", "config:write"}});
```

**Nachher (RBAC)**:
```cpp
// Scopes → Rollen mappen
users.assignRole("alice@example.com", "admin");

// Permission-Check statt Scope-Check
if (!rbac.checkPermission(users.getUserRoles(user_id), "config", "write")) {
    return json_error(403, "Forbidden");
}
```

## Weitere Informationen

- [RBAC Implementation](../src/security/rbac.cpp)
- [User-Role Store](../include/security/rbac.h)
- [Auth Middleware Integration](AUTH_MIDDLEWARE.md)
- [Audit Logging](AUDIT_LOGGING.md)
