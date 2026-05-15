#include "core/core.h"
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <optional>
#include <cstdint>

namespace sp {

class Database;

// Source RCON Protocol Constants
enum RconPacketType : int32_t {
    RCON_RESPONSE_VALUE  = 0,
    RCON_EXEC_COMMAND    = 2,
    RCON_AUTH_RESPONSE   = 2,
    RCON_AUTH            = 3
};

// Single RCON connection
class RconClient {
public:
    RconClient(const std::string& host, int port, const std::string& password);
    ~RconClient();

    bool connect(int timeoutMs = Config::get().getInt("RCON_CONNECT_TIMEOUT", 10000));
    void disconnect();
    std::string execute(const std::string& cmd, int timeoutMs = Config::get().getInt("RCON_EXECUTE_TIMEOUT", 10000));

    bool isConnected() const { return connected_; }
    bool ping(int timeoutMs = Config::get().getInt("RCON_PING_TIMEOUT", 3000));

private:
    std::string host_;
    int port_;
    std::string password_;
    int sock_ = -1;
    bool connected_ = false;
    int32_t nextId_ = 1;
    std::mutex mtx_;

    struct Packet {
        int32_t id   = 0;
        int32_t type = 0;
        std::string body;
    };

    bool sendPacket(const Packet& pkt);
    std::optional<Packet> recvPacket(int timeoutMs);
    bool setSocketTimeout(int timeoutMs);
    bool connectInternal(int timeoutMs);
};

// Connection pool
class RconPool {
public:
    explicit RconPool(Database& db);
    ~RconPool();

    // Send command to a server (auto-creates connection)
    std::string send(int serverId, const std::string& cmd, int timeoutMs = Config::get().getInt("RCON_SEND_TIMEOUT", 10000));

    // Test connection to a server
    std::string test(int serverId, int timeoutMs = Config::get().getInt("RCON_SEND_TIMEOUT", 5000));

    // Remove a connection
    void remove(int serverId);

    // Remove all connections
    void clear();
    void healthCheck();

private:
    Database& db_;
    std::unordered_map<int, std::shared_ptr<RconClient>> clients_;
    std::mutex poolMtx_;

    std::shared_ptr<RconClient> getOrCreate(int serverId);
};

} // namespace sp
