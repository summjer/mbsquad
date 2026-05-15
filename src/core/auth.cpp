#include "core/auth.h"
#include "core/database.h"
#include "core/core.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <cstring>
#include <algorithm>
#include <chrono>

namespace sp {

// Permission constants

const std::vector<std::string> Auth::ALL_PERMISSIONS = {
    "players", "kick", "ban", "reserved", "tk",
    "points", "quick_commands", "rcon", "settings",
    "user_admin", "plugins"
};

const std::vector<std::string> Auth::DEFAULT_OP_PERMISSIONS = {
    "players", "kick", "ban", "reserved", "tk",
    "points", "quick_commands"
};

Auth::Auth(Database& db) : db_(db) {}

// Hex helpers

static std::string hexEncode(const unsigned char* data, size_t len) {
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        out += hex[data[i] >> 4];
        out += hex[data[i] & 0x0F];
    }
    return out;
}

static std::string randomHex(size_t bytes) {
    unsigned char buf[64];
    if (bytes > sizeof(buf)) bytes = sizeof(buf);
    RAND_bytes(buf, (int)bytes);
    return hexEncode(buf, bytes);
}

// scrypt (compatible with Node.js crypto.scryptSync)

static bool scryptHash(const std::string& password, const std::string& salt,
                       unsigned char* out, size_t outLen) {
    // Node.js scryptSync defaults: N=16384, r=8, p=1, maxmem=32MB
    // Salt is the hex string as UTF-8 bytes (not decoded)
    return EVP_PBE_scrypt(
        password.c_str(), password.size(),
        (const unsigned char*)salt.c_str(), salt.size(),
        16384, 8, 1, 32 * 1024 * 1024,
        out, outLen
    ) == 1;
}

// Password

std::string Auth::hashPassword(const std::string& pw) {
    std::string salt = randomHex(16);  // 32 hex chars, same as Node.js
    unsigned char hash[64];
    if (!scryptHash(pw, salt, hash, 64)) return "";
    return salt + ":" + hexEncode(hash, 64);
}

bool Auth::verifyPassword(const std::string& pw, const std::string& stored) {
    auto pos = stored.find(':');
    if (pos == std::string::npos) return false;
    std::string salt = stored.substr(0, pos);
    std::string expectedHex = stored.substr(pos + 1);

    unsigned char hash[64];
    if (!scryptHash(pw, salt, hash, 64)) return false;

    std::string actualHex = hexEncode(hash, 64);
    if (actualHex.size() != expectedHex.size()) return false;
    return CRYPTO_memcmp(actualHex.data(), expectedHex.data(), actualHex.size()) == 0;
}

// Token

static std::string base64Encode(const unsigned char* data, size_t len) {
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    for (size_t i = 0; i < len; i += 3) {
        unsigned int n = (unsigned int)data[i] << 16;
        if (i + 1 < len) n |= (unsigned int)data[i + 1] << 8;
        if (i + 2 < len) n |= data[i + 2];
        out += t[(n >> 18) & 0x3F];
        out += t[(n >> 12) & 0x3F];
        out += (i + 1 < len) ? t[(n >> 6) & 0x3F] : '=';
        out += (i + 2 < len) ? t[n & 0x3F] : '=';
    }
    return out;
}

static std::string base64Decode(const std::string& in) {
    static bool init = false;
    static int t[256];
    if (!init) {
        std::memset(t, -1, sizeof(t));
        for (int i = 0; i < 26; i++) t[(int)'A'+i] = i;
        for (int i = 0; i < 26; i++) t[(int)'a'+i] = i+26;
        for (int i = 0; i < 10; i++) t[(int)'0'+i] = i+52;
        t[(int)'+'] = 62; t[(int)'/'] = 63;
        init = true;
    }
    std::string out;
    int val = 0, bits = -8;
    for (unsigned char c : in) {
        if (c == '=') break;
        if (t[c] == -1) continue;
        val = (val << 6) | t[c]; bits += 6;
        if (bits >= 0) { out.push_back((char)((val >> bits) & 0xFF)); bits -= 8; }
    }
    return out;
}

static std::string hmacSha256(const std::string& key, const std::string& data) {
    unsigned char result[32]; unsigned int len = 32;
    HMAC(EVP_sha256(), key.data(), (int)key.size(),
         (const unsigned char*)data.data(), data.size(), result, &len);
    return base64Encode(result, len);
}

std::string Auth::generateToken() {
    // Generate a self-contained token: base64(userId|username|role|exp).hmac
    // But for simple session-based auth, just use random token
    return randomHex(32);  // 64-char random hex token
}

// Session

Auth::Session Auth::getSession(const std::string& token) {
    Session sess;
    if (token.empty()) return sess;

    auto row = db_.queryOne(
        "SELECT s.token, s.expiresAt, u.id, u.username, u.role, u.status "
        "FROM sessions s JOIN users u ON s.userId = u.id WHERE s.token = ?",
        {token});

    // Fallback: try without join alias issue
    if (row.empty()) {
        row = db_.queryOne(
            "SELECT s.token, s.expiresAt, s.userId, u.username, u.role, u.status "
            "FROM sessions s JOIN users u ON s.userId = u.id WHERE s.token = ?",
            {token});
    }

    if (row.empty()) return sess;

    // Check status
    if (row.count("status") && row.at("status") != "active") return sess;

    sess.token     = row.at("token");
    sess.userId    = row.count("userId") ? row.at("userId") : row.at("id");
    sess.username  = row.at("username");
    sess.role      = row.at("role");
    sess.status    = row.count("status") ? row.at("status") : "";
    sess.expiresAt = row.count("expiresAt") ? row.at("expiresAt") : "";
    // v14.5: Check session expiry
    if (!sess.expiresAt.empty()) {
        auto nowStr = db_.queryScalar("SELECT datetime('now')");
        if (!nowStr.empty() && nowStr > sess.expiresAt) {
            sess.valid = false;
            return sess;
        }
    }

    sess.valid     = true;

    // Update lastActivity on each authenticated request
    db_.exec("UPDATE sessions SET lastActivity = datetime('now') WHERE token = ?", {token});

    return sess;
}

// Rate limiting

bool Auth::checkRateLimit(const std::string& ip) {
    std::lock_guard<std::mutex> lk(rateMtx_);
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    auto it = rateLimits_.find(ip);
    if (it == rateLimits_.end() || now - it->second.second > (int64_t)Config::get().getInt("RATE_LIMIT_WINDOW_MS", 60000)) {
        rateLimits_[ip] = {1, now};
        return true;
    }
    if (it->second.first >= Config::get().getInt("RATE_LIMIT_MAX_ATTEMPTS", 10)) return false;
    it->second.first++;
    return true;
}

// Permissions

bool Auth::Permissions::has(const std::string& perm) const {
    auto it = perms.find(perm);
    return it != perms.end() && it->second;
}

Auth::Permissions Auth::getUserPermissions(int userId, const std::string& role) {
    Permissions result;

    if (role == "server_owner") {
        for (auto& p : ALL_PERMISSIONS) result.perms[p] = true;
        return result;
    }

    for (auto& p : DEFAULT_OP_PERMISSIONS) result.perms[p] = true;
    for (auto& p : ALL_PERMISSIONS) {
        if (result.perms.find(p) == result.perms.end()) result.perms[p] = false;
    }

    auto rows = db_.query(
        "SELECT permission, granted FROM user_permissions WHERE userId = ?",
        {std::to_string(userId)});
    for (auto& row : rows) {
        result.perms[row["permission"]] = (row["granted"] == "1");
    }

    return result;
}

void Auth::insertDefaultPermissions(int userId) {
    for (auto& p : DEFAULT_OP_PERMISSIONS) {
        db_.exec("INSERT OR IGNORE INTO user_permissions (userId, permission, granted) VALUES (?, ?, 1)",
                 {std::to_string(userId), p});
    }
}

} // namespace sp
