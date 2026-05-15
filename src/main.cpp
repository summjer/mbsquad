#include "core/core.h"
#include "core/database.h"
#include "core/auth.h"
#include "core/log_parser.h"
#include "core/points_service.h"
#include "net/server.h"
#include "net/rcon.h"
#include "net/ws_handler.h"
#include "handlers/auth_handler.h"
#include "handlers/server_handler.h"
#include "handlers/player_handler.h"
#include "handlers/event_handler.h"
#include "handlers/points_handler.h"
#include "handlers/ban_handler.h"
#include "handlers/chat_handler.h"
#include "handlers/kill_handler.h"
#include "handlers/admin_handler.h"
#include "handlers/config_handler.h"
#include "handlers/rcon_handler.h"
#include "handlers/plugin_handler.h"
#include "handlers/tk_handler.h"
#include "handlers/query_handler.h"
#include "handlers/misc_handler.h"
#include "handlers/missing_routes.h"
#include "handlers/relay_routes.h"
#include "handlers/analytics_routes.h"
#include "handlers/extras_routes.h"
#include "plugins/plugin.h"
#include "tasks/tasks.h"
#include "core/local_log_tailer.h"
#include "core/remote_api_poller.h"

#include <csignal>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

static sp::Server* g_server = nullptr;
static sp::TimedTasks g_timedTasks;
static sp::LocalLogTailerManager g_localTailerManager;
static sp::RemoteApiPollerManager g_remoteApiManager;

static void signalHandler(int signum) {
    LOG_I("Main", "Received signal " + std::to_string(signum) + ", shutting down...");
    if (g_server) g_server->stop();
}

int main() {
    auto& config = sp::Config::get();
    config.loadFromFile("config.ini");
    config.loadFromEnv();

    // Log level
    std::string logLevel = config.get("LOG_LEVEL", "info");
    if (logLevel == "debug")     sp::Logger::get().setLevel(sp::LogLevel::DEBUG);
    else if (logLevel == "warn") sp::Logger::get().setLevel(sp::LogLevel::WARN);
    else if (logLevel == "error")sp::Logger::get().setLevel(sp::LogLevel::ERROR);

    LOG_I("Main", "Squad Panel C++ starting...");

    std::string dataDir = config.get("DATA_DIR", "data");
    std::error_code ec;
    fs::create_directories(dataDir, ec);

    sp::Database db;
    std::string dbPath = config.get("DB_FILE", dataDir + "/squad.db");
    db.open(dbPath);
    db.migrate();

    sp::Auth auth(db);

    sp::RconPool rconPool(db);

    sp::LogParser logParser;

    sp::PointsService pointsService(db);

    sp::WsHandler wsHandler(db, auth);

    sp::Server server(db, auth);
    server.setRconPool(&rconPool);
    g_server = &server;

    // Init event module with shared dependencies
    sp::initEventModule(&logParser, &wsHandler, &pointsService);

    // Register routes
    sp::registerAuthRoutes(server);
    sp::registerServerRoutes(server);
    sp::registerPlayerRoutes(server);
    sp::registerEventRoutes(server);
    sp::registerPointsRoutes(server);
    sp::registerBanRoutes(server);
    sp::registerChatRoutes(server);
    sp::registerKillRoutes(server);
    sp::registerAdminRoutes(server);
    sp::registerConfigRoutes(server);
    sp::registerRconRoutes(server);
    sp::registerPluginRoutes(server);
    sp::registerTkForgiveRoutes(server);
    sp::registerQueryRoutes(server);
    sp::registerScrambleRoutes(server);
    sp::registerBuildRoutes(server);
    sp::registerMissingRoutes(server);
    sp::registerRelayRoutes(server);
    sp::registerAnalyticsRoutes(server);
    sp::registerExtrasRoutes(server);

    {
        sp::PluginContext pctx{db, rconPool,
            [&](int s, const std::string& st) { rconPool.send(s, "AdminWarn \"" + st + "\""); },
            [](const std::string& m) { LOG_I("Plugin", m); }
        };
        sp::PluginManager::instance().initAll(pctx);
        LOG_I("Main", "Plugins initialized: " + std::to_string(sp::PluginManager::instance().plugins().size()));
    }

    g_timedTasks.start(db, rconPool);

    int port = config.getInt("PORT", 3000);
    g_localTailerManager.startAll(db, port);

    g_remoteApiManager.startAll(db, wsHandler);
    LOG_I("Main", "Data collectors started");

    // Static files
    std::string publicDir = config.get("PUBLIC_DIR", "public");
    server.staticFiles(publicDir);

    // Signal handling
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::string host = config.get("HOST", "0.0.0.0");

    LOG_I("Main", "Listening on " + host + ":" + std::to_string(port));
    server.listen(host, port);

    // Stop timed tasks
    g_localTailerManager.stopAll();
    g_remoteApiManager.stopAll();
    g_timedTasks.stop();

    // Cleanup
    g_server = nullptr;
    LOG_I("Main", "Server stopped");
    return 0;
}
