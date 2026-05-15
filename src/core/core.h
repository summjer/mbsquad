#pragma once

#include <json.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <memory>
#include <functional>

namespace sp {

// Logger
enum class LogLevel { DEBUG, INFO, WARN, ERROR };

class Logger {
    static Logger* instance_;
    std::mutex mtx_;
    LogLevel level_ = LogLevel::INFO;
    bool color_ = true;
public:
    static Logger& get() {
        static Logger inst;
        return inst;
    }
    void setLevel(LogLevel l) { level_ = l; }
    void log(LogLevel l, const std::string& tag, const std::string& msg);
};

#define LOG_D(tag, msg) sp::Logger::get().log(sp::LogLevel::DEBUG, tag, msg)
#define LOG_I(tag, msg) sp::Logger::get().log(sp::LogLevel::INFO, tag, msg)
#define LOG_W(tag, msg) sp::Logger::get().log(sp::LogLevel::WARN, tag, msg)
#define LOG_E(tag, msg) sp::Logger::get().log(sp::LogLevel::ERROR, tag, msg)

// Config (Singleton)
class Config {
    std::unordered_map<std::string, std::string> data_;
    Config() = default;
public:
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    static Config& get() {
        static Config inst;
        return inst;
    }

    void loadFromFile(const std::string& path);
    void loadFromEnv();
    std::string get(const std::string& key, const std::string& fallback = "") const;
    int getInt(const std::string& key, int fallback = 0) const;
    bool getBool(const std::string& key, bool fallback = false) const;
    void set(const std::string& key, const std::string& val);
};

// Utility
std::string nowISO();
std::string uuid4();
std::string sha256(const std::string& input);
std::string randomToken(int len = 32);
std::string urlDecode(const std::string& s);
std::string htmlEscape(const std::string& s);

// JSON helpers
inline std::string jsonStr(const nlohmann::json& j, const std::string& key, const std::string& def = "") {
    if (!j.contains(key)) return def;
    auto& v = j[key];
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number_integer()) return std::to_string(v.get<int64_t>());
    if (v.is_number()) return std::to_string(v.get<double>());
    if (v.is_boolean()) return v.get<bool>() ? "1" : "0";
    if (v.is_null()) return def;
    return v.dump();
}

inline int jsonInt(const nlohmann::json& j, const std::string& key, int def = 0) {
    if (!j.contains(key)) return def;
    auto& v = j[key];
    if (v.is_number_integer()) return v.get<int>();
    if (v.is_number()) return (int)v.get<double>();
    if (v.is_string()) { try { return std::stoi(v.get<std::string>()); } catch (...) {} }
    return def;
}

} // namespace sp
