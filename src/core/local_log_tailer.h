#pragma once
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>

namespace sp {

class Database;

/**
 * LocalLogTailer — 本地日志尾随器
 * 
 * 当 connectionMode=local 时，面板直接读取游戏服务器日志文件。
 * 读取到的原始日志行 POST 到本机 /api/events/raw，由 event_handler 统一处理。
 */
class LocalLogTailer {
public:
    LocalLogTailer(int serverId, const std::string& logPath,
                   Database& db, const std::string& apiKey, int panelPort);
    ~LocalLogTailer();

    void start();
    void stop();
    bool isRunning() const { return running_; }
    int serverId() const { return serverId_; }

private:
    int serverId_;
    std::string logPath_;
    Database& db_;
    std::string apiKey_;
    int panelPort_;
    
    std::atomic<bool> running_{false};
    std::thread thread_;
    uint64_t lastFilePos_ = 0;
    
    void tailLoop();
    bool readNewLines(std::vector<std::string>& lines);
    void postRawLines(const std::vector<std::string>& lines);
};

class LocalLogTailerManager {
public:
    void startAll(Database& db, int panelPort);
    void stopAll();
    void startServer(int serverId, const std::string& logPath,
                     Database& db, const std::string& apiKey, int panelPort);
    void stopServer(int serverId);

private:
    std::vector<std::unique_ptr<LocalLogTailer>> tailers_;
    std::mutex mtx_;
};

} // namespace sp
