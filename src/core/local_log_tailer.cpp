#include "core/local_log_tailer.h"
#include "core/database.h"
#include "core/core.h"

#include <fstream>
#include <chrono>
#include <filesystem>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define closesocket ::close
#endif

#include "../../vendor/json.hpp"
#include "../../vendor/httplib.h"

namespace sp {

// LocalLogTailer

LocalLogTailer::LocalLogTailer(int serverId, const std::string& logPath,
                               Database& db, const std::string& apiKey, int panelPort)
    : serverId_(serverId), logPath_(logPath), db_(db), apiKey_(apiKey), panelPort_(panelPort) {}

LocalLogTailer::~LocalLogTailer() {
    stop();
}

void LocalLogTailer::start() {
    if (running_) return;
    running_ = true;
    thread_ = std::thread(&LocalLogTailer::tailLoop, this);
    LOG_I("LocalTail", "Started tailing server " + std::to_string(serverId_) + ": " + logPath_);
}

void LocalLogTailer::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void LocalLogTailer::tailLoop() {
    // Start from end of file — only want new lines
    std::error_code ec;
    if (std::filesystem::exists(logPath_, ec)) {
        auto sz = std::filesystem::file_size(logPath_, ec);
        if (!ec && sz > 0) {
            lastFilePos_ = sz;
            LOG_I("LocalTail", "Server " + std::to_string(serverId_) + ": starting from end, size=" + std::to_string(sz));
        }
    }

    while (running_) {
        try {
            std::vector<std::string> lines;
            if (readNewLines(lines)) {
                postRawLines(lines);
            }
        } catch (const std::exception& e) {
            LOG_W("LocalTail", "Server " + std::to_string(serverId_) + " error: " + e.what());
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    LOG_I("LocalTail", "Stopped tailing server " + std::to_string(serverId_));
}

bool LocalLogTailer::readNewLines(std::vector<std::string>& lines) {
    std::error_code ec;
    if (!std::filesystem::exists(logPath_, ec)) return false;

    auto currentSize = std::filesystem::file_size(logPath_, ec);
    if (ec || currentSize <= lastFilePos_) return false;

    std::ifstream file(logPath_, std::ios::binary);
    if (!file.is_open()) return false;

    file.seekg(lastFilePos_);
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) lines.push_back(line);
    }

    lastFilePos_ = file.tellg();
    if (lastFilePos_ == (uint64_t)-1) {
        file.seekg(0, std::ios::end);
        lastFilePos_ = file.tellg();
    }

    return !lines.empty();
}

void LocalLogTailer::postRawLines(const std::vector<std::string>& lines) {
    if (lines.empty() || apiKey_.empty()) return;
    nlohmann::json body = {{"serverId", serverId_}, {"lines", lines}};
    std::string payload = body.dump();
    httplib::Client cli("127.0.0.1", panelPort_);
    cli.set_connection_timeout(Config::get().getInt("HTTP_CONNECT_TIMEOUT", 5));
    cli.set_read_timeout(Config::get().getInt("HTTP_READ_TIMEOUT", 10));
    httplib::Headers headers = {{"X-API-Key", apiKey_}};
    auto res = cli.Post("/api/events/raw", headers, payload, "application/json");
    if (!res) LOG_W("LocalTail", "POST failed for server " + std::to_string(serverId_));
}

// LocalLogTailerManager

void LocalLogTailerManager::startAll(Database& db, int panelPort) {
    auto rows = db.query(
        "SELECT id,logPath,serverApiKey FROM servers WHERE connectionMode='local' "
        "AND logPath IS NOT NULL AND logPath != ''",
        {});

    for (auto& row : rows) {
        int sid = std::stoi(row["id"]);
        std::string path = row["logPath"];
        std::string apiKey = row.count("serverApiKey") ? row["serverApiKey"] : "";
        startServer(sid, path, db, apiKey, panelPort);
    }

    LOG_I("LocalTail", "Started " + std::to_string(tailers_.size()) + " local log tailers");
}

void LocalLogTailerManager::stopAll() {
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto& t : tailers_) t->stop();
    tailers_.clear();
    LOG_I("LocalTail", "Stopped all local log tailers");
}

void LocalLogTailerManager::startServer(int serverId, const std::string& logPath,
                                         Database& db, const std::string& apiKey, int panelPort) {
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto& t : tailers_) {
        if (t->serverId() == serverId) return;
    }
    auto tailer = std::make_unique<LocalLogTailer>(serverId, logPath, db, apiKey, panelPort);
    tailer->start();
    tailers_.push_back(std::move(tailer));
}

void LocalLogTailerManager::stopServer(int serverId) {
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto it = tailers_.begin(); it != tailers_.end(); ++it) {
        if ((*it)->serverId() == serverId) {
            (*it)->stop();
            tailers_.erase(it);
            return;
        }
    }
}

} // namespace sp
