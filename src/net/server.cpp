#include "net/server.h"
#include "core/core.h"
#include "core/database.h"
#include "core/auth.h"
#include "net/rcon.h"
#include <openssl/crypto.h>

namespace sp {

// Context helpers

void Context::json(const nlohmann::json& data, int status) {
    res.status = status;
    res.set_content(data.dump(), "application/json");
    res.set_header("Content-Type", "application/json");
}

void Context::error(const std::string& msg, int status) {
    nlohmann::json err = {{"error", msg}};
    res.status = status;
    res.set_content(err.dump(), "application/json");
    res.set_header("Content-Type", "application/json");
}

std::string Context::param(int idx) const {
    if (idx >= 0 && idx < static_cast<int>(req.matches.size())) {
        return req.matches[static_cast<size_t>(idx)].str();
    }
    return "";
}

std::string Context::query(const std::string& key, const std::string& fallback) const {
    if (req.has_param(key)) {
        return req.get_param_value(key);
    }
    return fallback;
}

// Server

Server::Server(Database& db, Auth& auth) : db_(db), auth_(auth) {
    // CORS preflight -- origin from env or default
    const char* corsEnv = std::getenv("CORS_ORIGIN");
    std::string corsOrigin = corsEnv ? corsEnv : "https://squadshutiao.top:8443";
    svr_.set_pre_routing_handler([corsOrigin](const httplib::Request& req, httplib::Response& res)
        -> httplib::Server::HandlerResponse {
        res.set_header("Access-Control-Allow-Origin", corsOrigin);
        res.set_header("Vary", "Origin");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        if (req.method == "OPTIONS") {
            res.status = 204;
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });
}

Server::~Server() { svr_.stop(); }

void Server::registerRoute(const char* method, const std::string& pattern,
                            Handler handler, bool authRequired) {
    auto wrapper = [this, handler, authRequired]
                   (const httplib::Request& req, httplib::Response& res) {
        Context ctx{req, res, nullptr, "", "", "", false, *this};

        // Parse JSON body
        if (!req.body.empty()) {
            try {
                ctx.body = nlohmann::json::parse(req.body);
            } catch (...) {
                // Non-JSON body (e.g. file upload), skip parsing
            }
        }

        // Auth check
        if (authRequired) {
            auto authHeader = req.get_header_value("Authorization");
            if (authHeader.empty()) {
                ctx.error("Not authenticated", 401);
                return;
            }
            std::string token = authHeader;
            if (token.size() > 7 && token.substr(0, 7) == "Bearer ") {
                token = token.substr(7);
            }
            auto session = auth_.getSession(token);
            if (!session.valid || session.token.empty()) {
                ctx.error("Invalid or expired token", 401);
                return;
            }
            // Constant-time token comparison to prevent timing attacks
            if (token.size() != session.token.size() ||
                CRYPTO_memcmp(token.data(), session.token.data(), token.size()) != 0) {
                ctx.error("Invalid or expired token", 401);
                return;
            }
            ctx.userId       = session.userId;
            ctx.username     = session.username;
            ctx.role         = session.role;
            ctx.authenticated = true;
        }

        handler(ctx);
    };

    std::string m = method;
    if (m == "GET")         svr_.Get(pattern.c_str(), wrapper);
    else if (m == "POST")   svr_.Post(pattern.c_str(), wrapper);
    else if (m == "PUT")    svr_.Put(pattern.c_str(), wrapper);
    else if (m == "DELETE") svr_.Delete(pattern.c_str(), wrapper);
}

void Server::get(const std::string& pattern, Handler fn, bool authRequired) {
    registerRoute("GET", pattern, fn, authRequired);
}

void Server::post(const std::string& pattern, Handler fn, bool authRequired) {
    registerRoute("POST", pattern, fn, authRequired);
}

void Server::put(const std::string& pattern, Handler fn, bool authRequired) {
    registerRoute("PUT", pattern, fn, authRequired);
}

void Server::del(const std::string& pattern, Handler fn, bool authRequired) {
    registerRoute("DELETE", pattern, fn, authRequired);
}

void Server::staticFiles(const std::string& dir) {
    svr_.set_mount_point("/", dir);
    LOG_I("Server", "Static files: " + dir);
}

void Server::listen(const std::string& host, int port) {
    LOG_I("Server", "Listening on " + host + ":" + std::to_string(port));
    if (!svr_.listen(host, port)) {
        LOG_E("Server", "Failed to bind " + host + ":" + std::to_string(port));
    }
}

void Server::stop() {
    svr_.stop();
    LOG_I("Server", "Stopped");
}

} // namespace sp
