#include "core.h"
#include <sstream>
#include <iomanip>
#include <random>
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#include <openssl/sha.h>
#else
#include <openssl/sha.h>
#endif

namespace sp {

Logger* Logger::instance_ = nullptr;

void Logger::log(LogLevel l, const std::string& tag, const std::string& msg) {
    if (l < level_) return;
    std::lock_guard<std::mutex> lk(mtx_);
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    std::tm tm_buf;
    localtime_r(&t, &tm_buf);
    char timebuf[32];
    std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm_buf);

    const char* lvlStr = "???";
    const char* color = "\033[0m";
    switch (l) {
        case LogLevel::DEBUG: lvlStr = "DBG"; color = "\033[36m"; break;
        case LogLevel::INFO:  lvlStr = "INF"; color = "\033[32m"; break;
        case LogLevel::WARN:  lvlStr = "WRN"; color = "\033[33m"; break;
        case LogLevel::ERROR: lvlStr = "ERR"; color = "\033[31m"; break;
    }

    if (color_) {
        std::fprintf(stderr, "%s[%s.%03d] [%s] %s\033[0m %s\n",
            color, timebuf, (int)ms.count(), tag.c_str(), lvlStr, msg.c_str());
    } else {
        std::fprintf(stderr, "[%s.%03d] [%s] %s %s\n",
            timebuf, (int)ms.count(), tag.c_str(), lvlStr, msg.c_str());
    }
}

void Config::loadFromFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return;
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos || line[0] == '#') continue;
        auto key = line.substr(0, eq);
        auto val = line.substr(eq + 1);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\r')) key.pop_back();
        while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(val.begin());
        while (!val.empty() && (val.back() == ' ' || val.back() == '\r')) val.pop_back();
        if (!key.empty()) data_[key] = val;
    }
    LOG_I("Config", "Loaded " + std::to_string(data_.size()) + " settings from " + path);
}

void Config::loadFromEnv() {
    const char* keys[] = {
        "PORT", "HOST", "DATA_DIR", "JWT_SECRET", "LOG_LEVEL", "PLUGINS_DIR",
        "RCON_CONNECT_TIMEOUT", "RCON_EXECUTE_TIMEOUT", "RCON_PING_TIMEOUT",
        "RCON_HEALTHCHECK_TIMEOUT", "RCON_SEND_TIMEOUT",
        "RELAY_CONNECT_TIMEOUT", "RELAY_READ_TIMEOUT",
        "RELAY_DOWNLOAD_CONNECT_TIMEOUT", "RELAY_DOWNLOAD_READ_TIMEOUT",
        "HTTP_CONNECT_TIMEOUT", "HTTP_READ_TIMEOUT",
        "RATE_LIMIT_WINDOW_MS", "RATE_LIMIT_MAX_ATTEMPTS",
        "SQLITE_BUSY_TIMEOUT",
        "WOUND_DEDUP_WINDOW_MS", "WOUND_EXPIRY_MS", "SUICIDE_DEDUP_WINDOW_MS",
        "SCRAMBLE_PLAYTIME_WEIGHT", "SCRAMBLE_KD_WEIGHT",
        "LEADERBOARD_PAGE_SIZE", "WS_CLEANUP_INTERVAL_MS",
        "TEAM_SWITCH_DELAY_MS", "DEFAULT_RELAY_PORT", "TICK_RATE_MAX"
    };
    for (const char* k : keys) {
        const char* v = std::getenv(k);
        if (v) data_[k] = v;
    }
}

std::string Config::get(const std::string& key, const std::string& fallback) const {
    auto it = data_.find(key);
    return it != data_.end() ? it->second : fallback;
}

int Config::getInt(const std::string& key, int fallback) const {
    auto it = data_.find(key);
    if (it == data_.end()) return fallback;
    try { return std::stoi(it->second); } catch (...) { return fallback; }
}

bool Config::getBool(const std::string& key, bool fallback) const {
    auto it = data_.find(key);
    if (it == data_.end()) return fallback;
    auto& v = it->second;
    return v == "1" || v == "true" || v == "yes";
}

void Config::set(const std::string& key, const std::string& val) {
    data_[key] = val;
}

std::string nowISO() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    localtime_r(&t, &tm_buf);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return buf;
}

std::string uuid4() {
    static thread_local std::mt19937 rng(std::random_device{}());
    static thread_local std::uniform_int_distribution<uint32_t> dist(0, 15);
    static const char hex[] = "0123456789abcdef";
    std::string s(36, ' ');
    for (int i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) s[i] = '-';
        else if (i == 14) s[i] = '4';
        else { int r = dist(rng); s[i] = hex[(i == 19) ? (r & 0x3) | 0x8 : r]; }
    }
    return s;
}

std::string sha256(const std::string& input) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.c_str()), input.size(), hash);
    char buf[65];
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
        std::sprintf(buf + i * 2, "%02x", hash[i]);
    buf[64] = '\0';
    return buf;
}

std::string randomToken(int len) {
    static thread_local std::mt19937 rng(std::random_device{}());
    static thread_local std::uniform_int_distribution<int> dist(0, 35);
    static const char chars[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    std::string s(len, ' ');
    for (int i = 0; i < len; i++) s[i] = chars[dist(rng)];
    return s;
}

std::string urlDecode(const std::string& s) {
    std::string result;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int val = 0;
            unsigned int uval = 0;
            std::sscanf(s.c_str() + i + 1, "%2x", &uval);
            val = static_cast<int>(uval);
            result += static_cast<char>(val);
            i += 2;
        } else if (s[i] == '+') {
            result += ' ';
        } else {
            result += s[i];
        }
    }
    return result;
}

std::string htmlEscape(const std::string& s) {
    std::string r;
    r.reserve(s.size() * 1.2);
    for (char c : s) {
        switch (c) {
            case '&': r += "&amp;"; break;
            case '<': r += "&lt;"; break;
            case '>': r += "&gt;"; break;
            case '"': r += "&quot;"; break;
            default: r += c;
        }
    }
    return r;
}

} // namespace sp
