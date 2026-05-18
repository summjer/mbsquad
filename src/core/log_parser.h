#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <regex>
#include <ctime>
#include <chrono>
#include <mutex>

namespace sp {

// Parsed event structures

struct PlayerInfo {
    int rconId = 0;
    std::string steamId;
    std::string eosId;
    std::string name;
    std::string teamId;
    std::string squadId;
    bool isLeader = false;
    std::string role;
};

struct ChatEvent {
    std::string chatType;
    std::string playerName;
    std::string message;
    std::string steamId;
    std::string eosId;
};

struct KillEvent {
    std::string type; // "kill", "suicide"
    std::string killerName;
    std::string killerSteamId;
    std::string killerEosId;
    std::string victimName;
    std::string victimSteamId;
    std::string victimEosId;
    std::string weapon;
    double damage = 0;
};

struct ReviveEvent {
    std::string reviverName;
    std::string reviverSteamId;
    std::string reviverEosId;
    std::string revivedName;
    std::string revivedSteamId;
    std::string revivedEosId;
};

struct WoundEvent {
    std::string victimName;
    std::string attackerName;
    std::string attackerSteamId;
    std::string attackerEosId;
    std::string weapon;
    double damage = 0;
};

// Wound store entry (for Wound→Die dedup)

struct WoundRecord {
    std::string victimName;
    double damage = 0;
    std::string attackerController;
    std::string attackerSteamId;
    std::string attackerEosId;
    std::string weapon;
    int64_t time = 0; // ms since epoch
};

// Log parser

class LogParser {
public:
    LogParser();

    // Parse RCON ListPlayers raw output
    std::vector<PlayerInfo> parsePlayerList(const std::string& raw);

    // Parse a single log line, returns event type + data
    struct ParsedLine {
        std::string type;
        // "kill", "suicide", "wound", "revive", "chat", "join", "disconnect",
        // "newgame", "createSquad", "tickRate", "adminBroadcast",
        // "matchEnd", "changeLayer", "setNextLayer", "removedPlayer",
        // "beginInactiveState", "attack"
        std::unordered_map<std::string, std::string> fields;
    };
    std::vector<ParsedLine> processLine(const std::string& line, int serverId = 0);

    // Store wound info (called for Wound() lines)
    void storeWound(const std::string& line);

    // Get recent wounds (within dedup window)
    std::vector<WoundEvent> getRecentWounds();

    // Preprocess line: replace INVALID with placeholder (Rain Ops Mini technique)
    static std::string preprocessInvalidIds(const std::string& line);

    // Parse Online IDs string → {steamId, eosId}
    // Supports "EOS: xxx steam: xxx" format
    static std::unordered_map<std::string, std::string> parseOnlineIds(const std::string& idsStr);

    // Clean player name (remove control chars, detect garbled UTF-8)
    static std::string cleanPlayerName(const std::string& name);

    // Extract weapon name from UE4 blueprint name
    static std::string extractWeapon(const std::string& weaponStr);

    // Extract middle part of weapon string (Rain Ops Mini technique)
    // e.g. "BP_SoldierInsurgent_Wep_MAK47_Component_C" → "Wep_MAK47"
    static std::string extractWeaponMiddle(const std::string& weaponStr);

    // Parse chat from [ChatAll/ChatTeam/...] format
    static bool parseChatRaw(const std::string& raw, ChatEvent& out);

    // Cleanup expired entries (call periodically)
    void cleanup();

private:
    std::unordered_map<std::string, WoundRecord> woundStore_;
    std::mutex woundMtx_;
    int64_t lastCleanup_ = 0;

    // Regex patterns (compiled once)
    // --- Core events (updated to Rain Ops Mini precise format) ---
    std::regex reDie_;
    std::regex reWound_;
    std::regex reRevive_;
    std::regex rePostLogin_;
    std::regex reDisconnect_;       // UChannel::Close
    std::regex reBeginInactive_;    // BeginInactiveState (Rain Ops Mini)
    std::regex reCreateSquad_;
    std::regex reTickRate_;
    std::regex reNewGame_;
    std::regex reChatLog_;
    std::regex reChatRelay_;

    // --- New events from Rain Ops Mini ---
    std::regex reMatchEnd_;
    std::regex reChangeLayer_;
    std::regex reSetNextLayer_;
    std::regex reRemovedPlayer_;
    std::regex reAttack_;

    // --- Legacy/compat ---
    std::regex reJoinSucceeded_;
    std::regex reAdminBroadcast_;
    std::regex reCreateSquadOld_;
};

} // namespace sp
