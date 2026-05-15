#include "core/remote_api_poller.h"
#include "core/database.h"
#include "core/core.h"
#include "net/ws_handler.h"

#include <chrono>
#include <sstream>
#include <regex>

#include "../../vendor/httplib.h"

namespace sp {

// Shared HTTP helpers -- used by both SelfPanelAdapter and GenericApiAdapter
static std::string httpGetHelper(const std::string& baseUrl, const std::string& token,
                                  const std::string& path, const std::string& authHeader = "Authorization",
                                  const std::string& authPrefix = "Bearer ") {
    std::string host = baseUrl;
    int port = 443;
    if (host.substr(0, 8) == "https://") { host = host.substr(8); }
    else if (host.substr(0, 7) == "http://") { host = host.substr(7); port = 80; }
    auto cp = host.find(':');
    if (cp != std::string::npos) { port = std::stoi(host.substr(cp + 1)); host = host.substr(0, cp); }
    httplib::Client cli(host, port);
    cli.set_connection_timeout(Config::get().getInt("HTTP_CONNECT_TIMEOUT", 10));
    cli.set_read_timeout(Config::get().getInt("HTTP_READ_TIMEOUT", 15));
    cli.enable_server_certificate_verification(true);
    httplib::Headers headers;
    if (!token.empty()) headers.insert({authHeader, authPrefix + token});
    auto res = cli.Get(path, headers);
    if (res && res->status == 200) return res->body;
    return "";
}

static std::string httpPostHelper(const std::string& baseUrl, const std::string& token,
                                   const std::string& path, const std::string& body,
                                   const std::string& authHeader = "Authorization",
                                   const std::string& authPrefix = "Bearer ") {
    std::string host = baseUrl;
    int port = 443;
    if (host.substr(0, 8) == "https://") { host = host.substr(8); }
    else if (host.substr(0, 7) == "http://") { host = host.substr(7); port = 80; }
    auto cp = host.find(':');
    if (cp != std::string::npos) { port = std::stoi(host.substr(cp + 1)); host = host.substr(0, cp); }
    httplib::Client cli(host, port);
    cli.set_connection_timeout(Config::get().getInt("HTTP_CONNECT_TIMEOUT", 10));
    cli.set_read_timeout(Config::get().getInt("HTTP_READ_TIMEOUT", 15));
    cli.enable_server_certificate_verification(true);
    httplib::Headers headers;
    if (!token.empty()) headers.insert({authHeader, authPrefix + token});
    headers.insert({"Content-Type", "application/json"});
    auto res = cli.Post(path, headers, body, "application/json");
    if (res && res->status == 200) return res->body;
    return "";
}

// SelfPanelAdapter

SelfPanelAdapter::SelfPanelAdapter(const std::string& baseUrl, const std::string& token)
    : baseUrl_(baseUrl), token_(token) {
    if (!baseUrl_.empty() && baseUrl_.back() == '/')
        baseUrl_.pop_back();
}

std::string SelfPanelAdapter::httpGet(const std::string& path) {
    return httpGetHelper(baseUrl_, token_, path);
}

std::string SelfPanelAdapter::httpPost(const std::string& path, const std::string& body) {
    return httpPostHelper(baseUrl_, token_, path, body);
}

std::vector<RemoteApiAdapter::PlayerInfo> SelfPanelAdapter::fetchPlayers() {
    std::vector<PlayerInfo> players;
    auto resp = httpGet("/api/players/online");
    if (resp.empty()) return players;
    
    try {
        auto j = nlohmann::json::parse(resp);
        if (j.contains("players") && j["players"].is_array()) {
            for (auto& p : j["players"]) {
                PlayerInfo info;
                info.steamId = p.value("steamId", "");
                info.name = p.value("name", "");
                info.teamId = p.value("teamId", 0);
                info.squadId = p.value("squadId", 0);
                if (!info.steamId.empty()) players.push_back(info);
            }
        }
    } catch (...) {}
    
    return players;
}

std::vector<RemoteApiAdapter::EventInfo> SelfPanelAdapter::fetchEvents(const std::string& since) {
    std::vector<EventInfo> events;
    std::string path = "/api/events/log?page_size=100";
    if (!since.empty()) path += "&since=" + since;
    
    auto resp = httpGet(path);
    if (resp.empty()) return events;
    
    try {
        auto j = nlohmann::json::parse(resp);
        if (j.contains("events") && j["events"].is_array()) {
            for (auto& e : j["events"]) {
                EventInfo ev;
                ev.type = e.value("type", "");
                ev.steamId = e.value("steamId", "");
                ev.playerName = e.value("playerName", "");
                ev.detail = e.value("detail", "");
                ev.timestamp = e.value("timestamp", "");
                events.push_back(ev);
            }
        }
    } catch (...) {}
    
    return events;
}

int SelfPanelAdapter::fetchPoints(const std::string& steamId) {
    auto resp = httpGet("/api/points?steamId=" + steamId);
    if (resp.empty()) return 0;
    
    try {
        auto j = nlohmann::json::parse(resp);
        return j.value("balance", 0);
    } catch (...) {}
    return 0;
}

std::vector<RemoteApiAdapter::PlayerInfo> SelfPanelAdapter::fetchLeaderboard(int limit) {
    std::vector<PlayerInfo> players;
    auto resp = httpGet("/api/points/leaderboard?limit=" + std::to_string(limit));
    if (resp.empty()) return players;
    
    try {
        auto j = nlohmann::json::parse(resp);
        if (j.contains("leaderboard") && j["leaderboard"].is_array()) {
            for (auto& p : j["leaderboard"]) {
                PlayerInfo info;
                info.steamId = p.value("steamId", "");
                info.name = p.value("playerName", "");
                info.points = p.value("balance", 0);
                if (!info.steamId.empty()) players.push_back(info);
            }
        }
    } catch (...) {}
    
    return players;
}

bool SelfPanelAdapter::ping() {
    auto resp = httpGet("/api/health");
    return !resp.empty();
}

// GenericApiAdapter

GenericApiAdapter::GenericApiAdapter(const std::string& baseUrl, const std::string& token,
                                     const nlohmann::json& apiConfig)
    : baseUrl_(baseUrl), token_(token), config_(apiConfig) {
    if (!baseUrl_.empty() && baseUrl_.back() == '/')
        baseUrl_.pop_back();
    authHeader_ = config_.value("authHeader", "Authorization");
    authPrefix_ = config_.value("authPrefix", "Bearer ");
}

nlohmann::json GenericApiAdapter::resolveField(const nlohmann::json& j, const std::string& path) {
    if (path.empty()) return j;
    nlohmann::json current = j;
    std::string remaining = path;
    while (!remaining.empty()) {
        auto dotPos = remaining.find('.');
        std::string key = (dotPos != std::string::npos) ? remaining.substr(0, dotPos) : remaining;
        if (current.is_object() && current.contains(key)) {
            current = current[key];
        } else {
            return nlohmann::json();
        }
        if (dotPos != std::string::npos) remaining = remaining.substr(dotPos + 1);
        else break;
    }
    return current;
}

std::string GenericApiAdapter::resolveString(const nlohmann::json& j, const std::string& path, const std::string& def) {
    auto val = resolveField(j, path);
    if (val.is_string()) return val.get<std::string>();
    if (val.is_number()) return std::to_string(val.get<int>());
    if (val.is_null()) return def;
    return val.dump();
}

int GenericApiAdapter::resolveInt(const nlohmann::json& j, const std::string& path, int def) {
    auto val = resolveField(j, path);
    if (val.is_number()) return val.get<int>();
    if (val.is_string()) {
        try { return std::stoi(val.get<std::string>()); } catch (...) {}
    }
    return def;
}

std::string GenericApiAdapter::httpGet(const std::string& path) {
    auto result = httpGetHelper(baseUrl_, token_, path, authHeader_, authPrefix_);
    if (result.empty()) LOG_W("GenericApi", "GET " + path + " failed");
    return result;
}

std::vector<RemoteApiAdapter::PlayerInfo> GenericApiAdapter::fetchPlayers() {
    std::vector<PlayerInfo> players;
    if (!config_.contains("players")) return players;
    auto& pc = config_["players"];
    
    std::string endpoint = pc.value("endpoint", "/api/players");
    std::string arrayField = pc.value("arrayField", "players");
    
    auto resp = httpGet(endpoint);
    if (resp.empty()) return players;
    
    try {
        auto j = nlohmann::json::parse(resp);
        auto arr = resolveField(j, arrayField);
        if (!arr.is_array()) return players;
        
        for (auto& p : arr) {
            PlayerInfo info;
            info.steamId = resolveString(p, pc.value("steamIdField", "steamId"));
            info.name = resolveString(p, pc.value("nameField", "name"));
            if (pc.contains("pointsField")) info.points = resolveInt(p, pc["pointsField"]);
            if (pc.contains("teamIdField")) info.teamId = resolveInt(p, pc["teamIdField"]);
            if (pc.contains("squadIdField")) info.squadId = resolveInt(p, pc["squadIdField"]);
            if (!info.steamId.empty()) players.push_back(info);
        }
    } catch (const std::exception& e) {
        LOG_W("GenericApi", "Parse players error: " + std::string(e.what()));
    }
    return players;
}

std::vector<RemoteApiAdapter::EventInfo> GenericApiAdapter::fetchEvents(const std::string& since) {
    std::vector<EventInfo> events;
    if (!config_.contains("events")) return events;
    auto& ec = config_["events"];
    
    std::string endpoint = ec.value("endpoint", "/api/events");
    std::string arrayField = ec.value("arrayField", "events");
    
    if (!since.empty()) {
        endpoint += (endpoint.find('?') != std::string::npos ? "&" : "?") + std::string("since=") + since;
    }
    
    auto resp = httpGet(endpoint);
    if (resp.empty()) return events;
    
    try {
        auto j = nlohmann::json::parse(resp);
        auto arr = resolveField(j, arrayField);
        if (!arr.is_array()) return events;
        
        for (auto& e : arr) {
            EventInfo ev;
            ev.type = resolveString(e, ec.value("typeField", "type"));
            ev.steamId = resolveString(e, ec.value("steamIdField", "steamId"));
            ev.playerName = resolveString(e, ec.value("playerNameField", "playerName"));
            ev.detail = resolveString(e, ec.value("detailField", "detail"));
            ev.timestamp = resolveString(e, ec.value("timestampField", "timestamp"));
            if (!ev.type.empty()) events.push_back(ev);
        }
    } catch (const std::exception& e) {
        LOG_W("GenericApi", "Parse events error: " + std::string(e.what()));
    }
    return events;
}

int GenericApiAdapter::fetchPoints(const std::string& steamId) {
    if (!config_.contains("points")) return 0;
    auto& pc = config_["points"];
    
    std::string endpoint = pc.value("endpoint", "/api/points?steamId={steamId}");
    auto pos = endpoint.find("{steamId}");
    if (pos != std::string::npos) endpoint.replace(pos, 9, steamId);
    
    auto resp = httpGet(endpoint);
    if (resp.empty()) return 0;
    
    try {
        auto j = nlohmann::json::parse(resp);
        return resolveInt(j, pc.value("balanceField", "balance"), 0);
    } catch (...) {}
    return 0;
}

std::vector<RemoteApiAdapter::PlayerInfo> GenericApiAdapter::fetchLeaderboard(int limit) {
    std::vector<PlayerInfo> players;
    if (!config_.contains("leaderboard")) return players;
    auto& lc = config_["leaderboard"];
    
    std::string endpoint = lc.value("endpoint", "/api/leaderboard?limit={limit}");
    auto pos = endpoint.find("{limit}");
    if (pos != std::string::npos) endpoint.replace(pos, 7, std::to_string(limit));
    
    std::string arrayField = lc.value("arrayField", "leaderboard");
    
    auto resp = httpGet(endpoint);
    if (resp.empty()) return players;
    
    try {
        auto j = nlohmann::json::parse(resp);
        auto arr = resolveField(j, arrayField);
        if (!arr.is_array()) return players;
        
        for (auto& p : arr) {
            PlayerInfo info;
            info.steamId = resolveString(p, lc.value("steamIdField", "steamId"));
            info.name = resolveString(p, lc.value("playerNameField", "playerName"));
            info.points = resolveInt(p, lc.value("pointsField", "balance"));
            if (!info.steamId.empty()) players.push_back(info);
        }
    } catch (...) {}
    return players;
}

bool GenericApiAdapter::ping() {
    if (!config_.contains("health")) return true;
    auto& hc = config_["health"];
    std::string endpoint = hc.value("endpoint", "/api/health");
    auto resp = httpGet(endpoint);
    return !resp.empty();
}

// PluginSquadAdapter

PluginSquadAdapter::PluginSquadAdapter(const std::string& apiKey, const std::string& serverId)
    : apiKey_(apiKey), serverId_(serverId), baseUrl_("https://plugin.squad.cyou/api.php") {}

std::string PluginSquadAdapter::httpGet(const std::string& url) {
    // Extract path from full URL (httplib needs path only, not full URL)
    std::string path = url;
    auto schemeEnd = url.find("://");
    if (schemeEnd != std::string::npos) {
        auto hostEnd = url.find('/', schemeEnd + 3);
        if (hostEnd != std::string::npos) path = url.substr(hostEnd);
    }
    
    std::string apiHost = Config::get().get("PLUGIN_API_HOST", "plugin.squad.cyou");
    httplib::Client cli(apiHost.c_str(), Config::get().getInt("PLUGIN_API_PORT", 443));
    cli.set_connection_timeout(Config::get().getInt("HTTP_CONNECT_TIMEOUT", 10));
    cli.set_read_timeout(Config::get().getInt("HTTP_READ_TIMEOUT", 15));
    cli.enable_server_certificate_verification(true);
    
    auto res = cli.Get(path);
    if (res && res->status == 200) return res->body;
    LOG_W("PluginSquad", "GET " + path + " returned " + (res ? std::to_string(res->status) : "no response"));
    return "";
}

std::vector<RemoteApiAdapter::PlayerInfo> PluginSquadAdapter::fetchPlayers() {
    // plugin.squad.cyou 没有在线玩家列表接口，返回空
    return {};
}

std::vector<RemoteApiAdapter::EventInfo> PluginSquadAdapter::fetchEvents(const std::string& since) {
    // plugin.squad.cyou 没有事件接口，返回空
    return {};
}

int PluginSquadAdapter::fetchPoints(const std::string& steamId) {
    std::string path = baseUrl_ + "?action=dev_get_points&api_key=" + apiKey_
        + "&server_id=" + serverId_ + "&steamid=" + steamId;
    auto resp = httpGet(path);
    if (resp.empty()) return 0;
    try {
        auto j = nlohmann::json::parse(resp);
        if (j.value("success", false)) return j.value("points", 0);
    } catch (...) {}
    return 0;
}

std::vector<RemoteApiAdapter::PlayerInfo> PluginSquadAdapter::fetchLeaderboard(int limit) {
    std::vector<PlayerInfo> players;
    int page = 1;
    int remaining = limit;
    while (remaining > 0) {
        int pageSize = std::min(remaining, Config::get().getInt("LEADERBOARD_PAGE_SIZE", 100));
        std::string path = baseUrl_ + "?action=dev_get_leaderboard&api_key=" + apiKey_
            + "&server_id=" + serverId_
            + "&page=" + std::to_string(page)
            + "&page_size=" + std::to_string(pageSize);
        auto resp = httpGet(path);
        if (resp.empty()) break;
        try {
            auto j = nlohmann::json::parse(resp);
            if (!j.value("success", false)) break;
            auto& lb = j["leaderboard"];
            if (!lb.is_array() || lb.empty()) break;
            for (auto& entry : lb) {
                PlayerInfo info;
                info.steamId = std::to_string(entry.value("steamid", (int64_t)0));
                info.name = entry.value("name", "");
                info.points = entry.value("points", 0);
                if (!info.steamId.empty() && info.steamId != "0") players.push_back(info);
            }
            int total = j.value("total", 0);
            int totalPages = j.value("total_pages", 1);
            remaining -= (int)lb.size();
            if (page >= totalPages || remaining <= 0) break;
            page++;
        } catch (...) { break; }
    }
    return players;
}

bool PluginSquadAdapter::ping() {
    std::string path = baseUrl_ + "?action=dev_get_servers&api_key=" + apiKey_;
    auto resp = httpGet(path);
    if (resp.empty()) return false;
    try {
        auto j = nlohmann::json::parse(resp);
        return j.value("success", false);
    } catch (...) {}
    return false;
}

// RemoteApiPoller

RemoteApiPoller::RemoteApiPoller(int serverId, std::unique_ptr<RemoteApiAdapter> adapter,
                                 Database& db, WsHandler& ws)
    : serverId_(serverId), adapter_(std::move(adapter)), db_(db), ws_(ws) {}

RemoteApiPoller::~RemoteApiPoller() {
    stop();
}

void RemoteApiPoller::start() {
    if (running_) return;
    running_ = true;
    thread_ = std::thread(&RemoteApiPoller::pollLoop, this);
    LOG_I("RemoteApi", "Started polling for server " + std::to_string(serverId_));
}

void RemoteApiPoller::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void RemoteApiPoller::pollLoop() {
    int failCount = 0;
    
    while (running_) {
        try {
            if (failCount > 0 && failCount % 5 == 0) {
                if (!adapter_->ping()) {
                    LOG_W("RemoteApi", "Server " + std::to_string(serverId_) + " unreachable, retrying...");
                    std::this_thread::sleep_for(std::chrono::seconds(30));
                    continue;
                }
            }
            
            try {
                auto players = adapter_->fetchPlayers();
                processPlayers(players);
            } catch (const std::exception& e) {
                LOG_W("RemoteApi", "Fetch players failed for server " + std::to_string(serverId_) + ": " + e.what());
            }
            
            try {
                auto events = adapter_->fetchEvents(lastEventTime_);
                processEvents(events);
            } catch (const std::exception& e) {
                LOG_W("RemoteApi", "Fetch events failed for server " + std::to_string(serverId_) + ": " + e.what());
            }
            
            // Sync leaderboard (for adapters like PluginSquad that get player data from leaderboard)
            try {
                auto leaderboard = adapter_->fetchLeaderboard(Config::get().getInt("LEADERBOARD_PAGE_SIZE", 100));
                if (!leaderboard.empty()) processPlayers(leaderboard);
            } catch (const std::exception& e) {
                LOG_W("RemoteApi", "Fetch leaderboard failed for server " + std::to_string(serverId_) + ": " + e.what());
            }
            
            failCount = 0;
        } catch (const std::exception& e) {
            failCount++;
            LOG_W("RemoteApi", "Poll error for server " + std::to_string(serverId_) + ": " + e.what());
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(30));
    }
    
    LOG_I("RemoteApi", "Stopped polling for server " + std::to_string(serverId_));
}

void RemoteApiPoller::processPlayers(const std::vector<RemoteApiAdapter::PlayerInfo>& players) {
    for (auto& p : players) {
        try {
            db_.exec("INSERT OR REPLACE INTO players "
                     "(serverId,steamId,name,playtime,firstSeen,lastSeen) "
                     "VALUES(?,?,?,COALESCE((SELECT playtime FROM players WHERE serverId=? AND steamId=?),0),"
                     "COALESCE((SELECT firstSeen FROM players WHERE serverId=? AND steamId=?),datetime('now')),"
                     "datetime('now'))",
                     {std::to_string(serverId_), p.steamId, p.name,
                      std::to_string(serverId_), p.steamId,
                      std::to_string(serverId_), p.steamId});
            
            if (p.points > 0) {
                db_.exec("INSERT OR REPLACE INTO points "
                         "(serverId,steamId,playerName,balance,lifetimeEarned,lastUpdated) "
                         "VALUES(?,?,?,?,?,datetime('now'))",
                         {std::to_string(serverId_), p.steamId, p.name,
                          std::to_string(p.points), std::to_string(p.points)});
            }
        } catch (...) {}
    }
    
    if (!players.empty()) {
        nlohmann::json msg = {
            {"type", "remote_players_sync"},
            {"serverId", serverId_},
            {"count", (int)players.size()}
        };
        ws_.broadcastAll("remote_sync", msg.dump());
    }
}

void RemoteApiPoller::processEvents(const std::vector<RemoteApiAdapter::EventInfo>& events) {
    for (auto& ev : events) {
        if (ev.type == "kill") {
            try {
                db_.exec("INSERT INTO kills (serverId,killer,victim,weapon,timestamp) VALUES(?,?,?,?,?)",
                         {std::to_string(serverId_), ev.playerName, ev.detail, "", ev.timestamp});
            } catch (...) {}
        } else if (ev.type == "chat") {
            try {
                db_.exec("INSERT INTO chat_logs (serverId,steamId,playerName,message,type,timestamp) VALUES(?,?,?,?,?,?)",
                         {std::to_string(serverId_), ev.steamId, ev.playerName, ev.detail, "all", ev.timestamp});
            } catch (...) {}
        } else if (ev.type == "join") {
            try {
                db_.exec("INSERT INTO player_events (serverId,steamId,playerName,eventType,timestamp) VALUES(?,?,?,?,?)",
                         {std::to_string(serverId_), ev.steamId, ev.playerName, "join", ev.timestamp});
            } catch (...) {}
        } else if (ev.type == "leave") {
            try {
                db_.exec("INSERT INTO player_events (serverId,steamId,playerName,eventType,timestamp) VALUES(?,?,?,?,?)",
                         {std::to_string(serverId_), ev.steamId, ev.playerName, "leave", ev.timestamp});
            } catch (...) {}
        }
        
        if (!ev.timestamp.empty()) lastEventTime_ = ev.timestamp;
    }
    
    if (!events.empty()) {
        nlohmann::json msg = {
            {"type", "remote_events_sync"},
            {"serverId", serverId_},
            {"count", (int)events.size()}
        };
        ws_.broadcastAll("remote_sync", msg.dump());
    }
}

// RemoteApiPollerManager

void RemoteApiPollerManager::startAll(Database& db, WsHandler& ws) {
    auto rows = db.query(
        "SELECT id,connectionMode,remoteApiUrl,remoteApiToken,apiType,apiConfig FROM servers "
        "WHERE connectionMode IN ('remote_api','external_api') AND remoteApiUrl IS NOT NULL AND remoteApiUrl != ''",
        {});
    
    for (auto& row : rows) {
        int sid = std::stoi(row["id"]);
        std::string apiUrl = row["remoteApiUrl"];
        std::string apiToken = row.count("remoteApiToken") ? row["remoteApiToken"] : "";
        std::string apiType = row.count("apiType") ? row["apiType"] : "self";
        std::string apiConfigStr = row.count("apiConfig") ? row["apiConfig"] : "{}";
        
        std::unique_ptr<RemoteApiAdapter> adapter;
        
        if (apiType == "plugin_squad") {
            // Extract server_id from apiConfig or use empty
            std::string psServerId;
            try { auto cfg = nlohmann::json::parse(apiConfigStr); psServerId = cfg.value("server_id", ""); } catch (...) {}
            adapter = std::make_unique<PluginSquadAdapter>(apiToken, psServerId);
            LOG_I("RemoteApi", "Server " + std::to_string(sid) + " using PluginSquadAdapter");
        } else if (apiType == "generic") {
            nlohmann::json apiConfig;
            try { apiConfig = nlohmann::json::parse(apiConfigStr); } catch (...) { apiConfig = {}; }
            adapter = std::make_unique<GenericApiAdapter>(apiUrl, apiToken, apiConfig);
            LOG_I("RemoteApi", "Server " + std::to_string(sid) + " using GenericApiAdapter");
        } else {
            adapter = std::make_unique<SelfPanelAdapter>(apiUrl, apiToken);
            LOG_I("RemoteApi", "Server " + std::to_string(sid) + " using SelfPanelAdapter");
        }
        
        auto poller = std::make_unique<RemoteApiPoller>(sid, std::move(adapter), db, ws);
        poller->start();
        pollers_.push_back(std::move(poller));
    }
    
    LOG_I("RemoteApi", "Started " + std::to_string(pollers_.size()) + " remote API pollers");
}

void RemoteApiPollerManager::stopAll() {
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto& p : pollers_) p->stop();
    pollers_.clear();
    LOG_I("RemoteApi", "Stopped all remote API pollers");
}

void RemoteApiPollerManager::startServer(int serverId, Database& db, WsHandler& ws) {
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto& p : pollers_) {
        if (p->serverId() == serverId) return;
    }
    
    auto row = db.queryOne(
        "SELECT remoteApiUrl,remoteApiToken,apiType,apiConfig FROM servers WHERE id=? AND connectionMode IN ('remote_api','external_api')",
        {std::to_string(serverId)});
    if (row.empty()) return;
    
    std::string apiUrl = row["remoteApiUrl"];
    std::string apiToken = row.count("remoteApiToken") ? row["remoteApiToken"] : "";
    std::string apiType = row.count("apiType") ? row["apiType"] : "self";
    std::string apiConfigStr = row.count("apiConfig") ? row["apiConfig"] : "{}";
    
    std::unique_ptr<RemoteApiAdapter> adapter;
    if (apiType == "plugin_squad") {
        std::string psServerId;
        try { auto cfg = nlohmann::json::parse(apiConfigStr); psServerId = cfg.value("server_id", ""); } catch (...) {}
        adapter = std::make_unique<PluginSquadAdapter>(apiToken, psServerId);
    } else if (apiType == "generic") {
        nlohmann::json apiConfig;
        try { apiConfig = nlohmann::json::parse(apiConfigStr); } catch (...) { apiConfig = {}; }
        adapter = std::make_unique<GenericApiAdapter>(apiUrl, apiToken, apiConfig);
    } else {
        adapter = std::make_unique<SelfPanelAdapter>(apiUrl, apiToken);
    }
    
    auto poller = std::make_unique<RemoteApiPoller>(serverId, std::move(adapter), db, ws);
    poller->start();
    pollers_.push_back(std::move(poller));
}

void RemoteApiPollerManager::stopServer(int serverId) {
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto it = pollers_.begin(); it != pollers_.end(); ++it) {
        if ((*it)->serverId() == serverId) {
            (*it)->stop();
            pollers_.erase(it);
            return;
        }
    }
}

} // namespace sp
