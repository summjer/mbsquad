#pragma once

#include <string>
#include <functional>
#include <memory>

// Suppress warnings from vendor headers
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wextra"
#include <httplib.h>
#include <json.hpp>
#pragma GCC diagnostic pop

namespace sp {

class Database;
class Auth;
class RconPool;

// Request context

struct Context {
    const httplib::Request& req;
    httplib::Response& res;
    nlohmann::json body;
    std::string userId;
    std::string username;
    std::string role;
    bool authenticated = false;
    class Server& server;

    void json(const nlohmann::json& data, int status = 200);
    void error(const std::string& msg, int status = 400);

    std::string param(int idx) const;
    std::string query(const std::string& key, const std::string& fallback = "") const;
};

using Handler = std::function<void(Context&)>;

// HTTP server wrapper

class Server {
public:
    Server(Database& db, Auth& auth);
    ~Server();

    void get(const std::string& pattern, Handler fn, bool authRequired = true);
    void post(const std::string& pattern, Handler fn, bool authRequired = true);
    void put(const std::string& pattern, Handler fn, bool authRequired = true);
    void del(const std::string& pattern, Handler fn, bool authRequired = true);

    void staticFiles(const std::string& dir);
    void listen(const std::string& host, int port);
    void stop();

    Database& db()      { return db_; }
    Auth&     auth()    { return auth_; }
    RconPool* rconPool() { return rconPool_; }
    void setRconPool(RconPool* p) { rconPool_ = p; }

private:
    httplib::Server svr_;
    Database& db_;
    Auth& auth_;
    RconPool* rconPool_ = nullptr;

    void registerRoute(const char* method, const std::string& pattern,
                       Handler fn, bool authRequired);
};

} // namespace sp
