#include "net/rcon.h"
#include "core/core.h"
#include "core/database.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <netdb.h>
#include <cstring>
#include <algorithm>

namespace sp {

// RconClient

RconClient::RconClient(const std::string& host, int port, const std::string& password)
    : host_(host), port_(port), password_(password) {}

RconClient::~RconClient() { disconnect(); }

// connect() delegates to connectInternal() under lock.
// NOTE: connectInternal() MUST be called with mtx_ held.
bool RconClient::connect(int timeoutMs) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (connected_) return true;
    return connectInternal(timeoutMs);
}

void RconClient::disconnect() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (sock_ >= 0) {
        ::close(sock_);
        sock_ = -1;
    }
    connected_ = false;
}

bool RconClient::sendPacket(const Packet& pkt) {
    // [size:4][id:4][type:4][body...NUL][NUL]
    auto bodyLen = static_cast<uint32_t>(pkt.body.size());
    uint32_t size = 4 + 4 + bodyLen + 1 + 1;  // id + type + body + 2 NULs

    std::vector<uint8_t> buf(4 + size);
    std::memcpy(buf.data(), &size, 4);
    std::memcpy(buf.data() + 4, &pkt.id, 4);
    std::memcpy(buf.data() + 8, &pkt.type, 4);
    std::memcpy(buf.data() + 12, pkt.body.c_str(), bodyLen);
    buf[12 + bodyLen]     = 0;  // body NUL terminator
    buf[12 + bodyLen + 1] = 0;  // padding NUL

    auto total = static_cast<ssize_t>(buf.size());
    ssize_t sent = ::send(sock_, buf.data(), static_cast<size_t>(total), MSG_NOSIGNAL);
    if (sent != total) {
        connected_ = false;
        LOG_E("RCON", "Send failed");
        return false;
    }
    return true;
}

std::optional<RconClient::Packet> RconClient::recvPacket(int timeoutMs) {
    // Read size first
    uint32_t size = 0;
    struct pollfd pfd{};
    pfd.fd     = sock_;
    pfd.events = POLLIN;

    if (poll(&pfd, 1, timeoutMs) <= 0) return std::nullopt;

    ssize_t n = recv(sock_, &size, 4, MSG_WAITALL);
    if (n != 4 || size < 10 || size > 65536) return std::nullopt;

    // Read rest of packet
    std::vector<uint8_t> buf(size);
    if (poll(&pfd, 1, timeoutMs) <= 0) return std::nullopt;

    n = recv(sock_, buf.data(), size, MSG_WAITALL);
    if (static_cast<uint32_t>(n) != size) return std::nullopt;

    Packet pkt;
    std::memcpy(&pkt.id,   buf.data(), 4);
    std::memcpy(&pkt.type, buf.data() + 4, 4);
    // Body is between offset 8 and size-2 (two trailing NULs)
    auto bodyLen = static_cast<size_t>(size - 8 - 2);
    pkt.body.assign(reinterpret_cast<char*>(buf.data() + 8), bodyLen);
    return pkt;
}

// setSocketTimeout() removed -- using poll-based timeouts throughout

std::string RconClient::execute(const std::string& cmd, int timeoutMs) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!connected_ || sock_ < 0) {
        LOG_W("RCON", "Not connected, attempting reconnect to " + host_ + ":" + std::to_string(port_));
        if (!connectInternal(timeoutMs)) {
            LOG_E("RCON", "Reconnect failed for " + host_ + ":" + std::to_string(port_));
            return "";
        }
    }

    int32_t cmdId = nextId_++;
    int32_t termId = nextId_++;

    // Send command packet
    if (!sendPacket({cmdId, RCON_EXEC_COMMAND, cmd})) {
        connected_ = false;
        return "";
    }
    // Send terminator packet
    if (!sendPacket({termId, RCON_EXEC_COMMAND, ""})) {
        connected_ = false;
        return "";
    }

    // Collect response until we see the terminator ID
    std::string response;
    for (int i = 0; i < 64; i++) {  // max 64 packets
        auto pkt = recvPacket(timeoutMs);
        if (!pkt) {
            connected_ = false;
            break;
        }
        if (pkt->id == termId) break;
        if (pkt->id == cmdId) {
            response += pkt->body;
        }
    }

    return response;
}


// --- connectInternal: lock-free internal connect (v14.1) ---
bool RconClient::connectInternal(int timeoutMs) {
    if (sock_ >= 0) { ::close(sock_); sock_ = -1; }
    connected_ = false;
    struct addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* result = nullptr;
    std::string portStr = std::to_string(port_);
    if (getaddrinfo(host_.c_str(), portStr.c_str(), &hints, &result) != 0 || !result) {
        LOG_E("RCON", "Cannot resolve host: " + host_);
        return false;
    }
    sock_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_ < 0) { freeaddrinfo(result); LOG_E("RCON", "Socket creation failed"); return false; }
    int flags = fcntl(sock_, F_GETFL, 0);
    fcntl(sock_, F_SETFL, flags | O_NONBLOCK);
    int ret = ::connect(sock_, result->ai_addr, result->ai_addrlen);
    freeaddrinfo(result);
    if (ret < 0 && errno != EINPROGRESS) {
        ::close(sock_); sock_ = -1;
        LOG_E("RCON", "Connect failed to " + host_ + ":" + portStr);
        return false;
    }
    if (ret != 0) {
        struct pollfd pfd{}; pfd.fd = sock_; pfd.events = POLLOUT;
        if (poll(&pfd, 1, timeoutMs) <= 0) {
            ::close(sock_); sock_ = -1;
            LOG_E("RCON", "Connect timeout to " + host_ + ":" + portStr);
            return false;
        }
        int err = 0; socklen_t len = sizeof(err);
        getsockopt(sock_, SOL_SOCKET, SO_ERROR, &err, &len);
        if (err != 0) {
            ::close(sock_); sock_ = -1;
            LOG_E("RCON", "Connect error: " + std::string(strerror(err)));
            return false;
        }
    }
    fcntl(sock_, F_SETFL, flags);
    Packet authPkt{nextId_++, RCON_AUTH, password_};
    if (!sendPacket(authPkt)) { ::close(sock_); sock_ = -1; return false; }
    auto resp1 = recvPacket(timeoutMs);
    if (!resp1) { ::close(sock_); sock_ = -1; LOG_E("RCON", "Auth timeout"); return false; }
    auto resp2 = recvPacket(timeoutMs);
    if (!resp2) { ::close(sock_); sock_ = -1; LOG_E("RCON", "Auth timeout (2)"); return false; }
    if (resp2->id == -1) { ::close(sock_); sock_ = -1; LOG_E("RCON", "Auth failed for " + host_); return false; }
    connected_ = true;
    LOG_I("RCON", "Connected to " + host_ + ":" + portStr);
    return true;
}

// --- ping: heartbeat check (v14.1) ---
bool RconClient::ping(int timeoutMs) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!connected_ || sock_ < 0) return false;
    int32_t cmdId = nextId_++;
    int32_t termId = nextId_++;
    if (!sendPacket({cmdId, RCON_EXEC_COMMAND, ""})) { connected_ = false; return false; }
    if (!sendPacket({termId, RCON_EXEC_COMMAND, ""})) { connected_ = false; return false; }
    for (int i = 0; i < 4; i++) {
        auto pkt = recvPacket(timeoutMs);
        if (!pkt) { connected_ = false; return false; }
        if (pkt->id == termId) return true;
    }
    return true;
}

// RconPool

RconPool::RconPool(Database& db) : db_(db) {}

RconPool::~RconPool() { clear(); }

std::shared_ptr<RconClient> RconPool::getOrCreate(int serverId) {
    auto row = db_.queryOne(
        "SELECT host, rconPort, rconPassword FROM servers WHERE id=?",
        {std::to_string(serverId)});
    if (row.empty()) {
        LOG_E("RCON", "Server not found: " + std::to_string(serverId));
        return nullptr;
    }
    auto& host = row.at("host");
    auto& port = row.at("rconPort");
    auto& pass = row.at("rconPassword");
    if (host.empty() || pass.empty()) {
        LOG_E("RCON", "Server RCON not configured: " + std::to_string(serverId));
        return nullptr;
    }

    auto client = std::make_shared<RconClient>(
        host, port.empty() ? 27015 : std::stoi(port), pass);

    if (!client->connect()) {
        LOG_E("RCON", "Failed to connect to server " + std::to_string(serverId));
        return nullptr;
    }

    return client;
}

std::string RconPool::send(int serverId, const std::string& cmd, int timeoutMs) {
    std::lock_guard<std::mutex> lk(poolMtx_);

    // Try existing connection
    auto it = clients_.find(serverId);
    if (it != clients_.end() && it->second->isConnected()) {
        auto result = it->second->execute(cmd, timeoutMs);
        if (!result.empty() || it->second->isConnected()) return result;
        // Connection died, remove and recreate
        clients_.erase(it);
    }

    // Create new connection
    auto client = getOrCreate(serverId);
    if (!client) return "";

    clients_[serverId] = client;
    return client->execute(cmd, timeoutMs);
}

std::string RconPool::test(int serverId, int timeoutMs) {
    std::lock_guard<std::mutex> lk(poolMtx_);

    // Remove old connection if any
    clients_.erase(serverId);

    auto client = getOrCreate(serverId);
    if (!client) return "";

    auto result = client->execute("ListPlayers", timeoutMs);
    clients_[serverId] = client;
    return result;
}

void RconPool::remove(int serverId) {
    std::lock_guard<std::mutex> lk(poolMtx_);
    clients_.erase(serverId);
}

void RconPool::clear() {
    std::lock_guard<std::mutex> lk(poolMtx_);
    clients_.clear();
}

void RconPool::healthCheck() {
    std::lock_guard<std::mutex> lk(poolMtx_);
    std::vector<int> deadIds;
    for (auto& [id, client] : clients_) {
        if (!client->ping(Config::get().getInt("RCON_HEALTHCHECK_TIMEOUT", 2000))) deadIds.push_back(id);
    }
    for (int id : deadIds) {
        LOG_W("RCON", "Health check failed for server " + std::to_string(id) + ", removing");
        clients_.erase(id);
    }
}


} // namespace sp
