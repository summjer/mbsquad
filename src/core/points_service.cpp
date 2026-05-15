#include "core/points_service.h"
#include "core/shared_utils.h"
#include "core/database.h"
#include "core/core.h"

namespace sp {

PointsService::PointsService(Database& db) : db_(db) {}

bool PointsService::addPoints(int serverId, const std::string& steamId,
                              const std::string& playerName, int amount,
                              const std::string& reason, const std::string& op) {
    std::lock_guard<std::mutex> lk(mtx_);
    try {
        auto existing = db_.queryOne(
            "SELECT id, balance FROM points WHERE steamId=? AND (serverId=? OR serverId IS NULL) ORDER BY serverId DESC LIMIT 1",
            {steamId, std::to_string(serverId)});

        if (!existing.empty()) {
            int newBal = std::stoi(existing["balance"]) + amount;
            int earned = (amount > 0) ? amount : 0;
            db_.exec(
                "UPDATE points SET balance=?, lifetimeEarned=lifetimeEarned+?, "
                "playerName=COALESCE(?,playerName), lastUpdated=datetime('now') WHERE id=?",
                {std::to_string(newBal), std::to_string(earned),
                 playerName.empty() ? "" : playerName, existing["id"]});
        } else {
            int earned = (amount > 0) ? amount : 0;
            db_.exec(
                "INSERT INTO points (serverId,steamId,playerName,balance,lifetimeEarned) "
                "VALUES(?,?,?,?,?)",
                {std::to_string(serverId), steamId,
                 playerName.empty() ? "" : playerName,
                 std::to_string(amount), std::to_string(earned)});
        }

        db_.exec(
            "INSERT INTO point_logs (serverId,steamId,playerName,amount,reason,operator) "
            "VALUES(?,?,?,?,?,?)",
            {std::to_string(serverId), steamId,
             playerName.empty() ? "" : playerName,
             std::to_string(amount), reason, op});

        return true;
    } catch (const std::exception& e) {
        LOG_E("PointsService", "addPoints: " + std::string(e.what()));
        return false;
    }
}

int PointsService::getBalance(const std::string& steamId) {
    std::lock_guard<std::mutex> lk(mtx_);
    try {
        auto row = db_.queryOne(
            "SELECT balance FROM points WHERE steamId=? ORDER BY serverId DESC LIMIT 1",
            {steamId});
        if (row.empty()) return 0;
        return std::stoi(row["balance"]);
    } catch (const std::exception& e) {
        LOG_E("PointsService", "getBalance: " + std::string(e.what()));
        return 0;
    }
}

bool PointsService::deductPoints(int serverId, const std::string& steamId,
                                 const std::string& playerName, int amount,
                                 const std::string& reason, const std::string& op) {
    std::lock_guard<std::mutex> lk(mtx_);
    try {
        auto existing = db_.queryOne(
            "SELECT id, balance FROM points WHERE steamId=? AND (serverId=? OR serverId IS NULL) ORDER BY serverId DESC LIMIT 1",
            {steamId, std::to_string(serverId)});
        if (existing.empty()) return false;

        int balance = std::stoi(existing["balance"]);
        if (balance < amount) return false;

        int newBal = balance - amount;
        db_.exec(
            "UPDATE points SET balance=?, lastUpdated=datetime('now') WHERE id=?",
            {std::to_string(newBal), existing["id"]});

        db_.exec(
            "INSERT INTO point_logs (serverId,steamId,playerName,amount,reason,operator) "
            "VALUES(?,?,?,?,?,?)",
            {std::to_string(serverId), steamId,
             playerName.empty() ? "" : playerName,
             std::to_string(-amount), reason, op});

        return true;
    } catch (const std::exception& e) {
        LOG_E("PointsService", "deductPoints: " + std::string(e.what()));
        return false;
    }
}

void PointsService::scoreKill(int serverId, const std::string& killerSteamId,
                              const std::string& killerName, bool isTeamKill) {
    if (killerSteamId.empty()) return;
    if (isTeamKill) {
        addPoints(serverId, killerSteamId, killerName, getSettingInt(db_, "points_tk", -5), "TK击杀", "系统");
    } else {
        addPoints(serverId, killerSteamId, killerName, getSettingInt(db_, "points_kill", 2), "击杀", "系统");
    }
}

void PointsService::scoreSuicide(int serverId, const std::string& victimSteamId,
                                 const std::string& victimName) {
    if (victimSteamId.empty()) return;
    addPoints(serverId, victimSteamId, victimName, getSettingInt(db_, "points_suicide", -2), "自杀", "系统");
}

void PointsService::scoreRevive(int serverId, const std::string& reviverSteamId,
                                const std::string& reviverName) {
    if (reviverSteamId.empty()) return;
    addPoints(serverId, reviverSteamId, reviverName, getSettingInt(db_, "points_revive", 1), "救援", "系统");
}

} // namespace sp
