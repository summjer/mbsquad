#include "core/core.h"
#include "net/ws_handler.h"
#include "core/database.h"
#include "core/auth.h"
#include "core/core.h"
#include <algorithm>
#include <cstring>

namespace sp {

// PlayerDiff serialization
std::string PlayerDiff::toJson() const {
    nlohmann::json j;
    j["type"] = "player_diff";
    j["totalCount"] = totalCount;
    auto toJ = [](const PlayerSnapshot& p) -> nlohmann::json {
        return {{"steamId", p.steamId}, {"name", p.name}, {"teamId", p.teamId}, {"squadId", p.squadId}};
    };
    j["joined"] = nlohmann::json::array();
    for (auto& p : joined) j["joined"].push_back(toJ(p));
    j["left"] = nlohmann::json::array();
    for (auto& p : left) j["left"].push_back(toJ(p));
    j["changed"] = nlohmann::json::array();
    for (auto& p : changed) j["changed"].push_back(toJ(p));
    return j.dump();
}

// WsHandler implementation

WsHandler::WsHandler(Database& db, Auth& auth)
    : db_(db), auth_(auth) {
    running_ = true;
    flushThread_ = std::thread(&WsHandler::flushLoop, this);
    LOG_I("WS", "Handler initialized with incremental push + throttling");
}

WsHandler::~WsHandler() {
    running_ = false;
    if (flushThread_.joinable()) flushThread_.join();
}

void WsHandler::registerClient(const std::string& clientId, int serverId,
                                std::function<void(const std::string&)> sendFn) {
    std::lock_guard<std::mutex> lk(mtx_);
    WsClient client;
    client.id = clientId;
    client.subscribedServers.insert(serverId);
    client.sendFn = std::move(sendFn);
    client.lastPing = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    clients_[clientId] = std::move(client);
    LOG_D("WS", "Client registered: " + clientId + " server=" + std::to_string(serverId));
}

void WsHandler::unregisterClient(const std::string& clientId) {
    std::lock_guard<std::mutex> lk(mtx_);
    clients_.erase(clientId);
}

void WsHandler::subscribe(int serverId, const std::string& clientId) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = clients_.find(clientId);
    if (it != clients_.end()) {
        it->second.subscribedServers.insert(serverId);
    }
}

void WsHandler::unsubscribe(const std::string& clientId) {
    std::lock_guard<std::mutex> lk(mtx_);
    clients_.erase(clientId);
}

size_t WsHandler::connectionCount() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return clients_.size();
}

void WsHandler::cleanup() {
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<std::string> stale;
    for (auto& [id, c] : clients_) {
        if (now - c.lastPing > 120) stale.push_back(id);  // 2 min timeout
    }
    for (auto& id : stale) clients_.erase(id);
    if (!stale.empty()) {
        LOG_I("WS", "Cleaned up " + std::to_string(stale.size()) + " stale clients");
    }
}

// Immediate broadcast (chat, kills, revives)
void WsHandler::broadcast(int serverId, const std::string& eventType,
                          const std::string& dataJson) {
    int intervalMs = 0;
    if (eventType == "chat") intervalMs = config_.chatIntervalMs;
    else if (eventType == "kill") intervalMs = config_.killIntervalMs;
    else if (eventType == "revive") intervalMs = config_.reviveIntervalMs;

    if (intervalMs <= 0) {
        // Immediate
        std::string msg = "{\"type\":\"" + eventType + "\",\"data\":" + dataJson + "}";
        sendToSubscribers(serverId, msg);
    } else {
        // Throttled via flush queue
        std::lock_guard<std::mutex> lk(flushMtx_);
        flushQueue_.push_back({serverId, "{\"type\":\"" + eventType + "\",\"data\":" + dataJson + "}"});
    }
}

// Incremental player list with throttling
void WsHandler::broadcastPlayerUpdate(int serverId,
                                       const std::vector<PlayerSnapshot>& currentPlayers) {
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    std::lock_guard<std::mutex> lk(mtx_);
    auto lastIt = lastPlayerBroadcast_.find(serverId);
    if (lastIt != lastPlayerBroadcast_.end()) {
        if (now - lastIt->second < config_.playerListIntervalMs) {
            // Still update cache for diff calculation, but don't broadcast yet
            playerCache_[serverId] = currentPlayers;
            return;
        }
    }
    lastPlayerBroadcast_[serverId] = now;

    // Calculate diff
    auto diff = calculateDiff(serverId, currentPlayers);
    playerCache_[serverId] = currentPlayers;

    // Send diff
    std::string msg = diff.toJson();
    sendToSubscribers(serverId, msg);
}

// Full snapshot for new client
std::vector<PlayerSnapshot> WsHandler::getPlayerSnapshot(int serverId) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = playerCache_.find(serverId);
    if (it != playerCache_.end()) return it->second;
    return {};
}

// Broadcast to all
void WsHandler::broadcastAll(const std::string& eventType, const std::string& dataJson) {
    std::string msg = "{\"type\":\"" + eventType + "\",\"data\":" + dataJson + "}";
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& [id, client] : clients_) {
        if (client.sendFn) {
            try { client.sendFn(msg); }
            catch (...) {}
        }
    }
}

// Diff calculation
PlayerDiff WsHandler::calculateDiff(int serverId,
                                     const std::vector<PlayerSnapshot>& current) {
    PlayerDiff diff;
    diff.totalCount = current.size();

    auto& cached = playerCache_[serverId];

    // Build lookup from cached
    std::unordered_map<std::string, const PlayerSnapshot*> cachedMap;
    for (auto& p : cached) cachedMap[p.steamId] = &p;

    // Build lookup from current
    std::unordered_map<std::string, const PlayerSnapshot*> currentMap;
    for (auto& p : current) currentMap[p.steamId] = &p;

    // Find joined and changed
    for (auto& p : current) {
        auto it = cachedMap.find(p.steamId);
        if (it == cachedMap.end()) {
            diff.joined.push_back(p);
        } else if (*it->second != p) {
            diff.changed.push_back(p);
        }
    }

    // Find left
    for (auto& p : cached) {
        if (currentMap.find(p.steamId) == currentMap.end()) {
            diff.left.push_back(p);
        }
    }

    // If first time (no cache), send as full list
    if (cached.empty() && !current.empty()) {
        diff.joined.clear();
        diff.left.clear();
        diff.changed.clear();
        // Mark all as joined for initial load
        diff.joined = current;
    }

    return diff;
}

// Internal send
void WsHandler::sendToSubscribers(int serverId, const std::string& message) {
    // Copy clients under lock, send outside lock
    std::vector<std::function<void(const std::string&)>> targets;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        for (auto& [id, client] : clients_) {
            if (client.subscribedServers.count(0) ||
                client.subscribedServers.count(serverId)) {
                if (client.sendFn) targets.push_back(client.sendFn);
            }
        }
    }
    for (auto& fn : targets) {
        try { fn(message); }
        catch (...) {}
    }
}

// Flush loop for throttled events
void WsHandler::flushLoop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(Config::get().getInt("WS_CLEANUP_INTERVAL_MS", 100)));
        std::vector<std::pair<int, std::string>> toSend;
        {
            std::lock_guard<std::mutex> lk(flushMtx_);
            toSend.swap(flushQueue_);
        }
        for (auto& [serverId, msg] : toSend) {
            sendToSubscribers(serverId, msg);
        }
    }
}

} // namespace sp
