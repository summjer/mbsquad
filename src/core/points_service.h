#pragma once

#include <string>
#include <mutex>

namespace sp {

class Database;

// Points service

class PointsService {
public:
    explicit PointsService(Database& db);

    // Add/deduct points (unconditionally, can go negative)
    bool addPoints(int serverId, const std::string& steamId,
                   const std::string& playerName, int amount,
                   const std::string& reason, const std::string& op = "系统");

    // Get balance for a player (returns balance, or 0 if not found)
    int getBalance(const std::string& steamId);

    // Deduct points (fails if insufficient balance)
    bool deductPoints(int serverId, const std::string& steamId,
                      const std::string& playerName, int amount,
                      const std::string& reason, const std::string& op = "系统");

    // Auto-score for kills (returns points awarded)
    void scoreKill(int serverId, const std::string& killerSteamId,
                   const std::string& killerName, bool isTeamKill);

    // Auto-score for suicide
    void scoreSuicide(int serverId, const std::string& victimSteamId,
                      const std::string& victimName);

    // Auto-score for revive
    void scoreRevive(int serverId, const std::string& reviverSteamId,
                     const std::string& reviverName);

private:
    Database& db_;
    std::mutex mtx_;
};

} // namespace sp
