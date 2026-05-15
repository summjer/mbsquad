#include <set>
#include "core/database.h"
#include "core/core.h"

namespace sp {

Database::Database() = default;

Database::~Database() { close(); }

void Database::open(const std::string& path) {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    if (db_) { sqlite3_close(db_); db_ = nullptr; }
    int rc = sqlite3_open(path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        LOG_E("DB", std::string("open failed: ") + sqlite3_errmsg(db_));
        db_ = nullptr;
        return;
    }
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, ("PRAGMA busy_timeout=" + std::to_string(Config::get().getInt("SQLITE_BUSY_TIMEOUT", 5000))).c_str(), nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA foreign_keys=ON", nullptr, nullptr, nullptr);
    path_ = path;
    LOG_I("DB", "Opened: " + path);
}

void Database::close() {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
        LOG_I("DB", "Closed");
    }
}

void Database::bindParams(sqlite3_stmt* stmt, const std::vector<std::string>& params) {
    for (size_t i = 0; i < params.size(); i++) {
        sqlite3_bind_text(stmt, static_cast<int>(i + 1),
                          params[i].c_str(), static_cast<int>(params[i].size()),
                          SQLITE_TRANSIENT);
    }
}

std::unordered_map<std::string, std::string> Database::rowToMap(sqlite3_stmt* stmt) {
    std::unordered_map<std::string, std::string> row;
    int cols = sqlite3_column_count(stmt);
    row.reserve(static_cast<size_t>(cols));
    for (int i = 0; i < cols; i++) {
        const char* name = sqlite3_column_name(stmt, i);
        const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
        row[name] = val ? val : "";
    }
    return row;
}

void Database::exec(const std::string& sql) {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    if (!db_) return;
    char* err = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK && err) {
        LOG_E("DB", std::string("exec: ") + err);
        sqlite3_free(err);
    }
}

void Database::exec(const std::string& sql, const std::vector<std::string>& params) {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    if (!db_) return;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_E("DB", std::string("prepare: ") + sqlite3_errmsg(db_));
        return;
    }
    bindParams(stmt, params);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        LOG_E("DB", std::string("step: ") + sqlite3_errmsg(db_));
    }
    sqlite3_finalize(stmt);
}

std::unordered_map<std::string, std::string> Database::queryOne(
        const std::string& sql, const std::vector<std::string>& params) {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    if (!db_) return {};
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_E("DB", std::string("prepare: ") + sqlite3_errmsg(db_));
        return {};
    }
    bindParams(stmt, params);
    std::unordered_map<std::string, std::string> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = rowToMap(stmt);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<std::unordered_map<std::string, std::string>> Database::query(
        const std::string& sql, const std::vector<std::string>& params) {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    if (!db_) return {};
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_E("DB", std::string("prepare: ") + sqlite3_errmsg(db_));
        return {};
    }
    bindParams(stmt, params);
    std::vector<std::unordered_map<std::string, std::string>> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(rowToMap(stmt));
    }
    sqlite3_finalize(stmt);
    return results;
}

std::string Database::queryScalar(
        const std::string& sql, const std::vector<std::string>& params) {
    auto row = queryOne(sql, params);
    if (row.empty()) return "";
    return row.begin()->second;
}

int64_t Database::lastInsertId() {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    if (!db_) return 0;
    return static_cast<int64_t>(sqlite3_last_insert_rowid(db_));
}

void Database::begin()    { exec("BEGIN"); }
void Database::commit()   { exec("COMMIT"); }
void Database::rollback() { exec("ROLLBACK"); }

void Database::transaction(std::function<void()> fn) {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    if (!db_) return;
    sqlite3_exec(db_, "BEGIN", nullptr, nullptr, nullptr);
    fn();
    sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);
}

void Database::migrate() {
    LOG_I("DB", "Running migrations...");

    exec(R"(
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT UNIQUE NOT NULL,
            passwordHash TEXT NOT NULL,
            role TEXT DEFAULT 'member',
            status TEXT DEFAULT 'pending',
            createdAt TEXT DEFAULT (datetime('now')),
            lastLogin TEXT DEFAULT NULL,
            steamId TEXT DEFAULT NULL,
            rejectReason TEXT DEFAULT NULL
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS sessions (
            token TEXT PRIMARY KEY,
            userId INTEGER NOT NULL,
            createdAt TEXT DEFAULT (datetime('now')),
            expiresAt TEXT NOT NULL,
            FOREIGN KEY (userId) REFERENCES users(id)
        )
    )");



    // Migration: add lastActivity column to existing sessions table
    {
        auto cols = query("PRAGMA table_info(sessions)", {});
        bool hasLastActivity = false;
        for (auto& c : cols) {
            if (c.count("name") && c.at("name") == "lastActivity") {
                hasLastActivity = true;
                break;
            }
        }
        if (!hasLastActivity) {
            exec("ALTER TABLE sessions ADD COLUMN lastActivity TEXT DEFAULT (datetime('now'))");
            LOG_I("DB", "Added lastActivity column to sessions table");
        }
    }
    exec(R"(
        CREATE TABLE IF NOT EXISTS servers (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            userId INTEGER NOT NULL,
            name TEXT NOT NULL,
            host TEXT NOT NULL,
            rconPort INTEGER NOT NULL DEFAULT 27015,
            rconPassword TEXT NOT NULL,
            createdAt TEXT DEFAULT (datetime('now')),
            updatedAt TEXT DEFAULT (datetime('now')),
            serverApiKey TEXT,
            notes TEXT DEFAULT NULL,
            relayApiKey TEXT DEFAULT NULL,
            relayUrl TEXT DEFAULT NULL,
            gamePort INTEGER DEFAULT NULL,
            gameExePath TEXT DEFAULT NULL,
            launchParams TEXT DEFAULT NULL,
            autoRestart INTEGER DEFAULT 0,
            gameServerPid INTEGER DEFAULT NULL,
            lastStartedAt TEXT DEFAULT NULL,
            FOREIGN KEY (userId) REFERENCES users(id)
        )
    )");

    // Migration: add registerCode column to servers table
    {
        auto cols = query("PRAGMA table_info(servers)", {});
        bool hasRegisterCode = false;
        for (auto& c : cols) {
            if (c.count("name") && c.at("name") == "registerCode") {
                hasRegisterCode = true;
                break;
            }
        }
        if (!hasRegisterCode) {
            exec("ALTER TABLE servers ADD COLUMN registerCode TEXT DEFAULT NULL");
            LOG_I("DB", "Added registerCode column to servers table");
        }
    }

    // Migration: add connection mode columns to servers table
    {
        auto cols = query("PRAGMA table_info(servers)", {});
        std::set<std::string> colNames;
        for (auto& c : cols) {
            if (c.count("name")) colNames.insert(c.at("name"));
        }
        if (colNames.find("connectionMode") == colNames.end()) {
            exec("ALTER TABLE servers ADD COLUMN connectionMode TEXT DEFAULT 'relay'");
            LOG_I("DB", "Added connectionMode column to servers table");
        }
        if (colNames.find("logPath") == colNames.end()) {
            exec("ALTER TABLE servers ADD COLUMN logPath TEXT DEFAULT NULL");
            LOG_I("DB", "Added logPath column to servers table");
        }
        if (colNames.find("remoteApiUrl") == colNames.end()) {
            exec("ALTER TABLE servers ADD COLUMN remoteApiUrl TEXT DEFAULT NULL");
            LOG_I("DB", "Added remoteApiUrl column to servers table");
        }
        if (colNames.find("remoteApiToken") == colNames.end()) {
            exec("ALTER TABLE servers ADD COLUMN remoteApiToken TEXT DEFAULT NULL");
            LOG_I("DB", "Added remoteApiToken column to servers table");
        }
        if (colNames.find("apiType") == colNames.end()) {
            exec("ALTER TABLE servers ADD COLUMN apiType TEXT DEFAULT 'self'");
            LOG_I("DB", "Added apiType column to servers table");
        }
        if (colNames.find("apiConfig") == colNames.end()) {
            exec("ALTER TABLE servers ADD COLUMN apiConfig TEXT DEFAULT '{}'");
            LOG_I("DB", "Added apiConfig column to servers table");
        }
    }



    exec(R"(
        CREATE TABLE IF NOT EXISTS server_members (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            serverId INTEGER NOT NULL,
            userId INTEGER NOT NULL,
            role TEXT NOT NULL DEFAULT 'member',
            joinedAt TEXT DEFAULT (datetime('now')),
            UNIQUE(serverId, userId),
            FOREIGN KEY (serverId) REFERENCES servers(id) ON DELETE CASCADE,
            FOREIGN KEY (userId) REFERENCES users(id) ON DELETE CASCADE
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS players (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            serverId INTEGER,
            steamId TEXT,
            name TEXT,
            playtime INTEGER DEFAULT 0,
            firstSeen TEXT DEFAULT (datetime('now')),
            lastSeen TEXT DEFAULT (datetime('now')),
            lastIp TEXT DEFAULT NULL,
            FOREIGN KEY (serverId) REFERENCES servers(id)
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS kills (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            serverId INTEGER,
            killer TEXT,
            victim TEXT,
            weapon TEXT,
            timestamp TEXT DEFAULT (datetime('now')),
            FOREIGN KEY (serverId) REFERENCES servers(id)
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS bans (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            serverId INTEGER,
            steamId TEXT,
            playerName TEXT,
            reason TEXT,
            bannedBy TEXT,
            duration INTEGER DEFAULT 0,
            createdAt TEXT DEFAULT (datetime('now')),
            FOREIGN KEY (serverId) REFERENCES servers(id)
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS plugins (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            userId INTEGER NOT NULL,
            serverId INTEGER,
            name TEXT NOT NULL,
            version TEXT DEFAULT '1.0',
            enabled INTEGER DEFAULT 1,
            config TEXT DEFAULT '{}',
            installedAt TEXT DEFAULT (datetime('now')),
            FOREIGN KEY (userId) REFERENCES users(id),
            FOREIGN KEY (serverId) REFERENCES servers(id)
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS plugin_sources (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            userId INTEGER NOT NULL,
            filename TEXT NOT NULL,
            source TEXT,
            compiledPath TEXT,
            status TEXT DEFAULT 'pending',
            createdAt TEXT DEFAULT (datetime('now')),
            FOREIGN KEY (userId) REFERENCES users(id)
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS points (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            serverId INTEGER,
            steamId TEXT NOT NULL,
            playerName TEXT,
            balance INTEGER DEFAULT 0,
            lifetimeEarned INTEGER DEFAULT 0,
            lastUpdated TEXT DEFAULT (datetime('now')),
            FOREIGN KEY (serverId) REFERENCES servers(id)
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS point_logs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            serverId INTEGER,
            steamId TEXT NOT NULL,
            playerName TEXT,
            amount INTEGER NOT NULL,
            reason TEXT,
            operator TEXT,
            createdAt TEXT DEFAULT (datetime('now'))
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS settings (
            key TEXT PRIMARY KEY,
            value TEXT
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS reserved_slots (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            serverId INTEGER NOT NULL,
            steamId TEXT NOT NULL,
            playerName TEXT,
            addedBy TEXT,
            createdAt TEXT DEFAULT (datetime('now')),
            expiresAt TEXT DEFAULT NULL,
            FOREIGN KEY (serverId) REFERENCES servers(id)
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS chat_logs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            serverId INTEGER,
            playerName TEXT,
            steamId TEXT,
            message TEXT,
            type TEXT DEFAULT 'chat',
            timestamp TEXT DEFAULT (datetime('now')),
            roundId INTEGER DEFAULT NULL,
            channel TEXT DEFAULT NULL
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS player_events (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            serverId INTEGER,
            steamId TEXT,
            playerName TEXT,
            eventType TEXT,
            timestamp TEXT DEFAULT (datetime('now')),
            FOREIGN KEY (serverId) REFERENCES servers(id)
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS revives (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            serverId INTEGER,
            reviverSteamId TEXT,
            reviverName TEXT,
            revivedSteamId TEXT,
            revivedName TEXT,
            timestamp TEXT DEFAULT (datetime('now')),
            FOREIGN KEY (serverId) REFERENCES servers(id)
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS squad_events (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            serverId INTEGER,
            squadName TEXT NOT NULL,
            creatorName TEXT NOT NULL,
            creatorSteamId TEXT,
            teamIndex INTEGER,
            timestamp TEXT DEFAULT (datetime('now')),
            FOREIGN KEY (serverId) REFERENCES servers(id)
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS game_rounds (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            serverId INTEGER,
            map TEXT,
            timestamp TEXT DEFAULT (datetime('now')),
            FOREIGN KEY (serverId) REFERENCES servers(id)
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS tk_forgive (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            serverId INTEGER NOT NULL,
            killerSteamId TEXT NOT NULL,
            killerName TEXT,
            victimSteamId TEXT,
            victimName TEXT,
            forgiven INTEGER DEFAULT 0,
            kicked INTEGER DEFAULT 0,
            createdAt TEXT DEFAULT (datetime('now')),
            expiresAt TEXT NOT NULL,
            FOREIGN KEY (serverId) REFERENCES servers(id)
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS op_logs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            operator TEXT NOT NULL,
            action TEXT NOT NULL DEFAULT 'other',
            target TEXT,
            details TEXT,
            serverId INTEGER,
            createdAt TEXT DEFAULT (datetime('now'))
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS user_permissions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            userId INTEGER NOT NULL,
            permission TEXT NOT NULL,
            granted INTEGER DEFAULT 1,
            UNIQUE(userId, permission),
            FOREIGN KEY (userId) REFERENCES users(id)
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS server_ticks (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            serverId INTEGER NOT NULL,
            tickRate REAL NOT NULL,
            timestamp TEXT NOT NULL
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS squad_claims (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            serverId INTEGER NOT NULL,
            teamId INTEGER,
            squadId INTEGER,
            squadName TEXT,
            creatorSteamId TEXT,
            creatorName TEXT,
            createdAt TEXT DEFAULT (datetime('now'))
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS switch_locks (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            serverId INTEGER NOT NULL,
            steamId TEXT NOT NULL,
            lockedUntil TEXT,
            reason TEXT DEFAULT 'scramble',
            createdAt TEXT DEFAULT (datetime('now'))
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS cdk_codes (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            code TEXT NOT NULL UNIQUE,
            rewardType TEXT NOT NULL DEFAULT 'points',
            rewardValue INTEGER NOT NULL DEFAULT 10,
            serverId INTEGER,
            maxUses INTEGER NOT NULL DEFAULT 1,
            usedCount INTEGER NOT NULL DEFAULT 0,
            expiresAt TEXT,
            createdAt TEXT DEFAULT (datetime('now')),
            createdBy TEXT
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS cdk_logs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            code TEXT NOT NULL,
            serverId INTEGER,
            steamId TEXT NOT NULL,
            playerName TEXT,
            rewardType TEXT,
            rewardValue INTEGER,
            usedAt TEXT DEFAULT (datetime('now'))
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS server_presets (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            userId INTEGER NOT NULL,
            name TEXT NOT NULL,
            config TEXT NOT NULL,
            createdAt TEXT DEFAULT (datetime('now')),
            FOREIGN KEY (userId) REFERENCES users(id) ON DELETE CASCADE
        )
    )");

    exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_players_server_steam ON players(serverId, steamId)");
    exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_points_server_steam ON points(serverId, steamId)");
    exec("CREATE INDEX IF NOT EXISTS idx_kills_server ON kills(serverId)");
    exec("CREATE INDEX IF NOT EXISTS idx_bans_server ON bans(serverId)");
    exec("CREATE INDEX IF NOT EXISTS idx_chat_logs_server ON chat_logs(serverId)");
    exec("CREATE INDEX IF NOT EXISTS idx_point_logs_server_steam ON point_logs(serverId, steamId)");
    exec("CREATE INDEX IF NOT EXISTS idx_ticks_time ON server_ticks(timestamp)");

    // Composite indexes for common query patterns
    exec("CREATE INDEX IF NOT EXISTS idx_kills_server_killer ON kills(serverId, killer)");
    exec("CREATE INDEX IF NOT EXISTS idx_kills_server_victim ON kills(serverId, victim)");
    exec("CREATE INDEX IF NOT EXISTS idx_kills_timestamp ON kills(timestamp)");
    exec("CREATE INDEX IF NOT EXISTS idx_chat_logs_server_steam ON chat_logs(serverId, steamId)");
    exec("CREATE INDEX IF NOT EXISTS idx_chat_logs_timestamp ON chat_logs(timestamp)");
    exec("CREATE INDEX IF NOT EXISTS idx_player_events_steam_time ON player_events(steamId, timestamp)");
    exec("CREATE INDEX IF NOT EXISTS idx_tk_forgive_pending ON tk_forgive(serverId, forgiven, kicked)");
    exec("CREATE INDEX IF NOT EXISTS idx_sessions_expires ON sessions(expiresAt)");
    exec("CREATE INDEX IF NOT EXISTS idx_players_lastseen ON players(lastSeen)");
    exec("CREATE INDEX IF NOT EXISTS idx_point_logs_steam ON point_logs(steamId)");
    exec("CREATE INDEX IF NOT EXISTS idx_reserved_slots_server ON reserved_slots(serverId)");
    exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_reserved_slots_server_steam ON reserved_slots(serverId, steamId)");
    exec("CREATE INDEX IF NOT EXISTS idx_game_rounds_server ON game_rounds(serverId)");
    exec("CREATE INDEX IF NOT EXISTS idx_revives_server ON revives(serverId)");
    exec("CREATE INDEX IF NOT EXISTS idx_op_logs_action ON op_logs(action)");
    exec("CREATE INDEX IF NOT EXISTS idx_cdk_logs_server ON cdk_logs(serverId)");
    exec("CREATE INDEX IF NOT EXISTS idx_server_members_user ON server_members(userId)");
    exec("CREATE INDEX IF NOT EXISTS idx_server_members_server ON server_members(serverId)");

    auto servers = query("SELECT id FROM servers WHERE serverApiKey IS NULL OR serverApiKey = ''");
    for (auto& s : servers) {
        auto key = "srv_" + randomToken(40);
        exec("UPDATE servers SET serverApiKey=? WHERE id=?", {key, s["id"]});
    }

    struct DefaultSetting { const char* key; const char* value; };
    DefaultSetting defaults[] = {
        {"tb_code",           "\"tb\""},
        {"redeem_code",       "\"duihuan\""},
        {"redeem_cost",       "50"},
        {"tb_cost",           "20"},
        {"tk_forgive_seconds","180"},
        {"tk_forgive_keywords","\"sor,sorry,soy\""},
        {"tk_forgive_enabled","1"},
        {"afk_kick_enabled",  "1"},
        {"afk_kick_seconds",  "300"},
        {"sign_in_cooldown",  "86400"},
        {"lottery_enabled",   "1"},
        {"lottery_cooldown",  "3600"},
        {"lottery_min",       "5"},
        {"lottery_max",       "30"},
        {"lottery_cost",      "0"},
        {"points_share_group","\"[]\""},
    };
    for (auto& d : defaults) {
        auto existing = queryScalar("SELECT key FROM settings WHERE key=?", {d.key});
        if (existing.empty()) {
            exec("INSERT INTO settings (key,value) VALUES(?,?)", {d.key, d.value});
        }
    }

    // relay_token
    auto relayToken = queryScalar("SELECT value FROM settings WHERE key='relay_token'");
    if (relayToken.empty()) {
        auto token = randomToken(48);
        exec("INSERT INTO settings (key,value) VALUES('relay_token',?)", {"\"" + token + "\""});
    }

    LOG_I("DB", "Migrations complete");
}

} // namespace sp
