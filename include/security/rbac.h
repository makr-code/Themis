#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <optional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>

namespace themis {
namespace security {

/// Permission represents a single authorization right
struct Permission {
    std::string resource;  // e.g., "data", "keys", "config", "audit", "*"
    std::string action;    // e.g., "read", "write", "delete", "rotate", "*"
    
    bool operator==(const Permission& other) const {
        return resource == other.resource && action == other.action;
    }
    
    // Wildcard matching: resource="*" matches all, action="*" matches all
    bool matches(const std::string& res, const std::string& act) const;
    
    std::string toString() const { return resource + ":" + action; }
};

/// Role represents a set of permissions
struct Role {
    std::string name;                     // e.g., "admin", "operator", "analyst", "readonly"
    std::string description;              // Human-readable description
    std::vector<Permission> permissions;  // List of permissions
    std::vector<std::string> inherits;    // Role inheritance (e.g., admin inherits operator)
    
    nlohmann::json toJson() const;
    static Role fromJson(const nlohmann::json& j);
};

/// RBAC Configuration
struct RBACConfig {
    std::string config_path;              // Path to YAML/JSON config file
    bool enable_role_inheritance = true;  // Allow roles to inherit from others
    bool enable_resource_wildcards = true; // Allow "*" in resource/action
    
    // Built-in roles (if config file not found)
    bool use_builtin_roles = true;
};

/// Role-Based Access Control System
class RBAC {
public:
    explicit RBAC(const RBACConfig& config);
    
    /// Load roles from configuration file (YAML or JSON)
    bool loadConfig(const std::string& path);
    
    /// Save roles to configuration file
    bool saveConfig(const std::string& path);
    
    /// Add a role programmatically
    void addRole(const Role& role);
    
    /// Remove a role
    void removeRole(const std::string& role_name);
    
    /// Get role by name
    std::optional<Role> getRole(const std::string& role_name) const;
    
    /// List all role names
    std::vector<std::string> listRoles() const;
    
    /// Check if user with given roles has permission to perform action on resource
    /// @param user_roles List of roles assigned to user
    /// @param resource Resource identifier (e.g., "data", "keys", "config")
    /// @param action Action identifier (e.g., "read", "write", "delete")
    /// @return true if permission granted
    bool checkPermission(
        const std::vector<std::string>& user_roles,
        const std::string& resource,
        const std::string& action
    ) const;
    
    /// Get all effective permissions for a user (including inherited)
    std::vector<Permission> getUserPermissions(const std::vector<std::string>& user_roles) const;
    
    /// Validate role hierarchy (detect cycles)
    bool validateRoleHierarchy() const;
    
    /// Get role hierarchy tree (for debugging)
    nlohmann::json getRoleHierarchy() const;
    
    /// Built-in roles factory
    static std::vector<Role> getBuiltinRoles();
    
private:
    RBACConfig config_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Role> roles_;
    
    /// Helper: expand role with inheritance
    std::vector<Permission> expandRolePermissions(const std::string& role_name, std::unordered_set<std::string>& visited) const;
    
    /// Helper: load from JSON
    bool loadFromJson(const nlohmann::json& j);
    
    /// Helper: load from YAML (via json-yaml bridge)
    bool loadFromYaml(const std::string& content);
};

/// User represents an authenticated user with assigned roles
struct User {
    std::string user_id;                  // e.g., "alice@example.com"
    std::vector<std::string> roles;       // e.g., ["operator", "analyst"]
    std::unordered_map<std::string, std::string> attributes; // Custom attributes
    
    nlohmann::json toJson() const;
    static User fromJson(const nlohmann::json& j);
};

/// User-Role mapping storage
class UserRoleStore {
public:
    /// Add user-role assignment
    void assignRole(const std::string& user_id, const std::string& role);
    
    /// Remove user-role assignment
    void revokeRole(const std::string& user_id, const std::string& role);
    
    /// Get all roles for user
    std::vector<std::string> getUserRoles(const std::string& user_id) const;
    
    /// Get all users with given role
    std::vector<std::string> getRoleUsers(const std::string& role) const;
    
    /// Load from JSON file
    bool load(const std::string& path);
    
    /// Save to JSON file
    bool save(const std::string& path);
    
    /// Get user object
    std::optional<User> getUser(const std::string& user_id) const;
    
    /// Add or update user
    void setUser(const User& user);
    
private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, User> users_;
};

} // namespace security
} // namespace themis
