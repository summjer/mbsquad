#pragma once
#include <atomic>
#include <thread>
#include <vector>
#include <string>

namespace sp {
class Database;
class RconPool;

class TimedTasks {
public:
    void start(Database& db, RconPool& rcon);
    void stop();
private:
    std::atomic<bool> running_{false};
    std::vector<std::thread> threads_;
    void autoRefreshLoop(Database& db, RconPool& rcon);
    void broadcastLoop(Database& db, RconPool& rcon);
    void tkCheckerLoop(Database& db, RconPool& rcon);
    void sessionCleanupLoop(Database& db);
};

} // namespace sp