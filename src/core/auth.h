#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace sp {

class Database;

class Auth {
public:
    explicit Auth(Database& db);

    // Password hashing (SHA256 + random salt)
    static std::string hashPassword(const std::string& pw);
    static bool verifyPassword(const std::string& pw, const std::string& stored);

    // Token generation
    static std::string generateToken();

    // Session
    struct Session {
        std::string token;
        std::string userId;
        std::string username;
        std::string role;
        std::string status;
        std::string expiresAt;
        bool valid = false;
    };
    Session getSession(const std::string& token);

    // Rate limiting (per IP)
    bool checkRateLimit(const std::string& ip);

    // Permissions
    static const std::vector<std::string> ALL_PERMISSIONS;
    static const std::vector<std::string> DEFAULT_OP_PERMISSIONS;

    struct Permissions {
        std::unordered_map<std::string, bool> perms;
        bool has(const std::string& perm) const;
    };

    Permissions getUserPermissions(int userId, const std::string& role);
    void insertDefaultPermissions(int userId);

private:
    Database& db_;
    std::unordered_map<std::string, std::pair<int, int64_t>> rateLimits_;
    std::mutex rateMtx_;
};

} // namespace sp
