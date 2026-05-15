#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>
#include "json.hpp"

namespace sp {
class Database;
class RconPool;

struct PluginContext {
    Database& db;
    RconPool& rcon;
    std::function<void(int,const std::string&)> warn;
    std::function<void(const std::string&)> log;
};

struct PluginEvent {
    std::string type;
    int serverId = 0;
    nlohmann::json data;
};

class Plugin {
public:
    virtual ~Plugin() = default;
    virtual std::string name() const = 0;
    virtual void init(PluginContext& ctx) {}
    virtual void handle(const PluginEvent& ev, PluginContext& ctx) = 0;
};

class PluginManager {
public:
    static PluginManager& instance() {
        static PluginManager pm;
        return pm;
    }
    void registerPlugin(Plugin* p) { plugins_.push_back(p); }
    void initAll(PluginContext& ctx) {
        for (auto* p : plugins_) p->init(ctx);
    }
    void dispatch(const PluginEvent& ev, PluginContext& ctx) {
        for (auto* p : plugins_) {
            try { p->handle(ev, ctx); } catch (...) {}
        }
    }
    std::vector<Plugin*>& plugins() { return plugins_; }
private:
    std::vector<Plugin*> plugins_;
};


} // namespace sp