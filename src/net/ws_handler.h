#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <thread>
#include <chrono>
#include <atomic>
#include <functional>

namespace httplib {}  // forward ref (defined in vendor/httplib.h)
struct cJSON;

namespace sp {

class Database;
class Auth;

// Player snapshot for diff
struct PlayerSnapshot {
    std::string steamId;
    std::string name;
    int teamId = 0;
    std::string squadId;
    std::string rconId;
    bool operator==(const PlayerSnapshot& o) const {
        return steamId == o.steamId && name == o.name && teamId == o.teamId && squadId == o.squadId;
    }
    bool operator!=(const PlayerSnapshot& o) const { return !(*this == o); }
};

// Incremental diff result
struct PlayerDiff {
    std::vector<PlayerSnapshot> joined;
    std::vector<PlayerSnapshot> left;
    std::vector<PlayerSnapshot> changed;  // team/squad/name change
    int totalCount = 0;
    std::string toJson() const;
};

// Throttled broadcast config
struct BroadcastConfig {
    int playerListIntervalMs = 3000;   // Player list: 3s throttle
    int chatIntervalMs = 0;            // Chat: immediate
    int killIntervalMs = 0;            // Kills: immediate
    int reviveIntervalMs = 0;          // Revives: immediate
    int maxPayloadBytes = 65536;       // Max single message size
};

// WebSocket client
struct WsClient {
    std::string id;
    std::unordered_set<int> subscribedServers;  // 0 = all
    int64_t lastPing = 0;
    std::function<void(const std::string&)> sendFn;  // Actual send callback
};

class WsHandler {
public:
    WsHandler(Database& db, Auth& auth);
    ~WsHandler();

    // Client management
    void registerClient(const std::string& clientId, int serverId,
                        std::function<void(const std::string&)> sendFn);
    void unregisterClient(const std::string& clientId);
    void subscribe(int serverId, const std::string& clientId);
    void unsubscribe(const std::string& clientId);
    size_t connectionCount() const;
    void cleanup();  // Remove stale clients

    // Broadcasting
    // Immediate broadcast (chat, kills, revives)
    void broadcast(int serverId, const std::string& eventType,
                   const std::string& dataJson);

    // Incremental player list update with throttling
    void broadcastPlayerUpdate(int serverId,
                               const std::vector<PlayerSnapshot>& currentPlayers);

    // Broadcast to all clients regardless of server subscription
    void broadcastAll(const std::string& eventType, const std::string& dataJson);

    // Configuration
    BroadcastConfig& config() { return config_; }

    // Full state snapshot (for new client connect)
    std::vector<PlayerSnapshot> getPlayerSnapshot(int serverId);

private:
    Database& db_;
    Auth& auth_;
    mutable std::mutex mtx_;
    std::unordered_map<std::string, WsClient> clients_;

    // Player list cache per server for diff calculation
    std::unordered_map<int, std::vector<PlayerSnapshot>> playerCache_;

    // Throttle timers per server per event type
    std::unordered_map<int, int64_t> lastPlayerBroadcast_;

    // Background flush thread
    std::atomic<bool> running_{false};
    std::thread flushThread_;
    void flushLoop();
    std::mutex flushMtx_;
    std::vector<std::pair<int, std::string>> flushQueue_;  // (serverId, json)

    // Calculate diff between cached and current player list
    PlayerDiff calculateDiff(int serverId,
                             const std::vector<PlayerSnapshot>& current);

    // Internal send
    void sendToSubscribers(int serverId, const std::string& message);

    BroadcastConfig config_;
};

} // namespace sp
