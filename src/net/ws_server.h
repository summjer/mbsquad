#pragma once

#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <unordered_map>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace sp {

// Lightweight WebSocket server (single-threaded, epoll-free)
// Uses httplib for HTTP, this handles only /ws upgrades on a separate port.
// Designed for low-connection-count admin panel (typically 1-5 OPs).

struct WsFrame {
    bool fin = true;
    uint8_t opcode = 0;  // 1=text, 2=binary, 8=close, 9=ping, 10=pong
    std::string payload;
};

class WsServer {
public:
    using MessageHandler = std::function<void(int fd, const std::string& msg)>;
    using ConnectHandler = std::function<void(int fd)>;
    using DisconnectHandler = std::function<void(int fd)>;

    WsServer();
    ~WsServer();

    // Start listening on wsHost:wsPort
    void listen(const std::string& host, int port);

    // Stop the server
    void stop();

    // Register handlers
    void onMessage(MessageHandler fn)  { msgHandler_ = std::move(fn); }
    void onConnect(ConnectHandler fn)  { connectHandler_ = std::move(fn); }
    void onDisconnect(DisconnectHandler fn) { disconnectHandler_ = std::move(fn); }

    // Send a message to a specific client
    bool send(int fd, const std::string& msg);

    // Broadcast to all connected clients
    void broadcast(const std::string& msg);

    // Get connected client count
    size_t clientCount() const;

    // Check if running
    bool isRunning() const { return running_; }

private:
    int listenFd_ = -1;
    std::atomic<bool> running_{false};
    std::thread acceptThread_;
    std::unordered_map<int, std::thread> clientThreads_;
    mutable std::mutex clientsMtx_;

    MessageHandler msgHandler_;
    ConnectHandler connectHandler_;
    DisconnectHandler disconnectHandler_;

    void acceptLoop();
    void clientLoop(int fd);

    // WebSocket framing
    bool readFrame(int fd, WsFrame& frame);
    bool writeFrame(int fd, uint8_t opcode, const std::string& payload);
    bool doHandshake(int fd);

    // Low-level I/O
    bool readExact(int fd, void* buf, size_t len);
    ssize_t readSome(int fd, void* buf, size_t len);

    void closeClient(int fd);
};

} // namespace sp
