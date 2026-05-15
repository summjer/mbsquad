#pragma once
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <memory>
#include <mutex>
#include <functional>

#include "../../vendor/json.hpp"

namespace sp {

class Database;
class WsHandler;

class RemoteApiAdapter {
public:
    virtual ~RemoteApiAdapter() = default;
    
    struct PlayerInfo {
        std::string steamId;
        std::string name;
        int points = 0;
        int teamId = 0;
        int squadId = 0;
    };
    
    struct EventInfo {
        std::string type;
        std::string steamId;
        std::string playerName;
        std::string detail;
        std::string timestamp;
    };
    
    virtual std::vector<PlayerInfo> fetchPlayers() = 0;
    virtual std::vector<EventInfo> fetchEvents(const std::string& since) = 0;
    virtual int fetchPoints(const std::string& steamId) = 0;
    virtual std::vector<PlayerInfo> fetchLeaderboard(int limit = 20) = 0;
    virtual bool ping() = 0;
};

class SelfPanelAdapter : public RemoteApiAdapter {
public:
    SelfPanelAdapter(const std::string& baseUrl, const std::string& token);
    std::vector<PlayerInfo> fetchPlayers() override;
    std::vector<EventInfo> fetchEvents(const std::string& since) override;
    int fetchPoints(const std::string& steamId) override;
    std::vector<PlayerInfo> fetchLeaderboard(int limit = 20) override;
    bool ping() override;
private:
    std::string baseUrl_;
    std::string token_;
    std::string httpGet(const std::string& path);
    std::string httpPost(const std::string& path, const std::string& body);
};

/**
 * GenericApiAdapter — 通用 REST API 适配器
 * 
 * apiConfig JSON 格式：
 * {
 *   "authHeader": "X-API-KEY",        // 认证 header 名，默认 "Authorization"
 *   "authPrefix": "",                  // 值前缀，默认 "Bearer "
 *   "players": {
 *     "endpoint": "/api/players",
 *     "arrayField": "players",
 *     "steamIdField": "steamId",
 *     "nameField": "name",
 *     "pointsField": "points",
 *     "teamIdField": "teamId",
 *     "squadIdField": "squadId"
 *   },
 *   "events": {
 *     "endpoint": "/api/events",
 *     "arrayField": "events",
 *     "typeField": "type",
 *     "steamIdField": "steamId",
 *     "playerNameField": "playerName",
 *     "detailField": "detail",
 *     "timestampField": "timestamp"
 *   },
 *   "points": {
 *     "endpoint": "/api/points?steamId={steamId}",
 *     "balanceField": "balance"
 *   },
 *   "leaderboard": {
 *     "endpoint": "/api/leaderboard?limit={limit}",
 *     "arrayField": "leaderboard",
 *     "steamIdField": "steamId",
 *     "playerNameField": "playerName",
 *     "pointsField": "balance"
 *   },
 *   "health": {
 *     "endpoint": "/api/health"
 *   }
 * }
 */
class GenericApiAdapter : public RemoteApiAdapter {
public:
    GenericApiAdapter(const std::string& baseUrl, const std::string& token,
                      const nlohmann::json& apiConfig);
    std::vector<PlayerInfo> fetchPlayers() override;
    std::vector<EventInfo> fetchEvents(const std::string& since) override;
    int fetchPoints(const std::string& steamId) override;
    std::vector<PlayerInfo> fetchLeaderboard(int limit = 20) override;
    bool ping() override;
private:
    std::string baseUrl_;
    std::string token_;
    nlohmann::json config_;
    std::string authHeader_;
    std::string authPrefix_;
    std::string httpGet(const std::string& path);
    static nlohmann::json resolveField(const nlohmann::json& j, const std::string& path);
    static std::string resolveString(const nlohmann::json& j, const std::string& path, const std::string& def = "");
    static int resolveInt(const nlohmann::json& j, const std::string& path, int def = 0);
};

/**
 * PluginSquadAdapter -- 对接 plugin.squad.cyou 积分云服务
 *
 * 使用 dev_* 接口 (X-API-KEY 认证, GET/POST form-encoded)
 * 提供: 积分查询/排行榜/服务器列表
 */
class PluginSquadAdapter : public RemoteApiAdapter {
public:
    PluginSquadAdapter(const std::string& apiKey, const std::string& serverId);
    std::vector<PlayerInfo> fetchPlayers() override;
    std::vector<EventInfo> fetchEvents(const std::string& since) override;
    int fetchPoints(const std::string& steamId) override;
    std::vector<PlayerInfo> fetchLeaderboard(int limit = 20) override;
    bool ping() override;
private:
    std::string apiKey_;
    std::string serverId_;
    std::string baseUrl_;
    std::string httpGet(const std::string& path);
};

class RemoteApiPoller {
public:
    RemoteApiPoller(int serverId, std::unique_ptr<RemoteApiAdapter> adapter, Database& db, WsHandler& ws);
    ~RemoteApiPoller();
    void start();
    void stop();
    bool isRunning() const { return running_; }
    int serverId() const { return serverId_; }
private:
    int serverId_;
    std::unique_ptr<RemoteApiAdapter> adapter_;
    Database& db_;
    WsHandler& ws_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::string lastEventTime_;
    void pollLoop();
    void processPlayers(const std::vector<RemoteApiAdapter::PlayerInfo>& players);
    void processEvents(const std::vector<RemoteApiAdapter::EventInfo>& events);
};

class RemoteApiPollerManager {
public:
    void startAll(Database& db, WsHandler& ws);
    void stopAll();
    void startServer(int serverId, Database& db, WsHandler& ws);
    void stopServer(int serverId);
private:
    std::vector<std::unique_ptr<RemoteApiPoller>> pollers_;
    std::mutex mtx_;
};

} // namespace sp
