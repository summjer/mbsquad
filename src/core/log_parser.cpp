#include "core/log_parser.h"
#include "core/core.h"

#include <sstream>
#include <algorithm>

namespace sp {

// Constructor: compile regexes

LogParser::LogParser() {
    // Die: Player Died (SquadJS精确正则, Cont.?oller 兼容 Contoller)
    reDie_ = std::regex(
        R"(\[([0-9.:-]+)\]\[([ 0-9]*)\]LogSquadTrace: \[DedicatedServer\](?:ASQSoldier::)?Die\(\): Player:(.+) KillingDamage=(?:-)*([0-9.]+) from ([A-z_0-9]+) \(Online IDs:([^)|]+)\| Cont.?oller ID: ([\w\d]+)\) caused by (.+))");

    // Wound: Player Wounded
    reWound_ = std::regex(
        R"(\[([0-9.:-]+)\]\[([ 0-9]*)\]LogSquadTrace: \[DedicatedServer\](?:ASQSoldier::)?Wound\(\): Player:(.+) KillingDamage=(?:-)*([0-9.]+) from ([A-z_0-9]+) \(Online IDs:([^)|]+)\| Cont.?oller ID: ([\w\d]+)\) caused by (.+))");

    // Revive: Player revived Player
    reRevive_ = std::regex(
        R"(\[([0-9.:-]+)\]\[([ 0-9]*)\]LogSquad: (.+) \(Online IDs:([^)]+)\) has revived (.+) \(Online IDs:([^)]+)\)\.)");

    // ChatLog: [ChatLog] PlayerName (steamid): message
    reChatLog_ = std::regex(
        R"(\[ChatLog\]\s*(.+?)\s*\((\d{17})\):\s*(.+))",
        std::regex::icase);

    // Chat relay format: [ChatAll/ChatTeam/ChatSquad/ChatAdmin] [Online IDs:...] name : message
    reChatRelay_ = std::regex(
        R"(\[(ChatAll|ChatTeam|ChatSquad|ChatAdmin)\]\s*\[Online IDs:([^\]]+)\]\s*(.+?)\s*:\s*(.+))");

    // PostLogin
    rePostLogin_ = std::regex(
        R"(\[([0-9.:-]+)\]\[([ 0-9]*)\]LogSquad: PostLogin: NewPlayer: BP_PlayerController(?:|.+)_C .+PersistentLevel\.([^\s]+) \(IP: ([\d.]+) \| Online IDs:([^)|]+)\))");

    // Disconnect (CloseBunch format)
    reDisconnect_ = std::regex(
        R"(\[([\d.:-]+)\]\[([ \d]*)\]LogNet: UChannel::Close: Sending CloseBunch\..+RemoteAddr: ([\d.]+).+PC: (\w+PlayerController(?:|.+)_C_\d+),.+UniqueId:\s*([^\s,\]]+))");

    // Disconnect (base format)
    reDisconnectBase_ = std::regex(
        R"(\[([\d.:-]+)\]\[([ \d]*)\]LogNet: (UChannel::CleanUp|UNetConnection::Close):.+(RemoteAddr: ([\d.]+):\d+)?.*UniqueId:\s*(RedpointEOS:([a-f0-9]+)|Steam:(\d{17})|([^\s,\]]+)))");

    // NewGame
    reNewGame_ = std::regex(
        R"(\[([0-9.:-]+)\]\[([ 0-9]*)\]LogWorld: Bringing World \/([A-z0-9]+)\/(?:Maps\/)?([A-z0-9-]+)\/(?:.+\/)?([A-z0-9-]+)(?:\.[A-z0-9-]+))");

    // Server Tick Rate
    reTickRate_ = std::regex(
        R"(\[([0-9.:-]+)\]\[([ 0-9]*)\]LogSquad: USQGameState: Server Tick Rate: ([0-9.]+))");

    // Create Squad (new format)
    reCreateSquadNew_ = std::regex(
        R"((?:Player:\s*)?([^\s(]+)\s*\(Online IDs:[^)]+\)\s*has created Squad (\d+)\s*\(Squad Name:\s*([^)]+)\)\s*on\s*(.+))",
        std::regex::icase);

    // Create Squad (old format)
    reCreateSquadOld_ = std::regex(
        R"(Player:([^\(\s]+)(?:\s*\(Online IDs[^\)]+\))?\s+Created Squad (.+?) with ID:\s*(\d+))",
        std::regex::icase);

    // Admin Broadcast
    reAdminBroadcast_ = std::regex(
        R"(\[([0-9.:-]+)\]\[([ 0-9]*)\]LogSquad: ADMIN COMMAND: Message broadcasted <(.+)> from (.+))");

    // Join succeeded
    reJoinSucceeded_ = std::regex(
        R"(\[([0-9.:-]+)\]\[([ 0-9]*)\]LogNet: Join succeeded: (.+))");
}

// Parse Online IDs string

std::unordered_map<std::string, std::string> LogParser::parseOnlineIds(const std::string& idsStr) {
    std::unordered_map<std::string, std::string> result;
    if (idsStr.empty()) return result;

    std::regex matcher(R"(\s*([^\s:]+)\s*:\s*([^\s|)]+))");
    auto begin = std::sregex_iterator(idsStr.begin(), idsStr.end(), matcher);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        std::string platform = (*it)[1].str();
        std::string id = (*it)[2].str();
        std::transform(platform.begin(), platform.end(), platform.begin(), ::tolower);
        if (platform.find("steam") != std::string::npos) result["steamId"] = id;
        else if (platform.find("eos") != std::string::npos) result["eosId"] = id;
    }
    return result;
}

// Clean player name

std::string LogParser::cleanPlayerName(const std::string& name) {
    if (name.empty()) return name;
    std::string cleaned;
    cleaned.reserve(name.size());
    for (char c : name) {
        if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7F) {
            cleaned += c;
        }
    }
    if (cleaned.empty()) return cleaned;

    // Check for garbled UTF-8 (Latin-1 high chars without CJK)
    int latin1Count = 0;
    int cjkCount = 0;
    for (size_t i = 0; i < cleaned.size(); i++) {
        unsigned char uc = static_cast<unsigned char>(cleaned[i]);
        if (uc >= 0xC0) latin1Count++;  // unsigned char always <= 0xFF
    }
    // Check for CJK characters in UTF-8 (3-byte sequences starting with E4-E9)
    for (size_t i = 0; i + 2 < cleaned.size(); i++) {
        unsigned char b0 = static_cast<unsigned char>(cleaned[i]);
        if (b0 >= 0xE4 && b0 <= 0xE9) cjkCount++;
    }

    if (latin1Count > static_cast<int>(cleaned.size()) * 3 / 10 && cjkCount == 0) {
        return "";
    }
    return cleaned;
}

// Extract weapon name

std::string LogParser::extractWeapon(const std::string& weaponStr) {
    if (weaponStr.empty()) return "Unknown";
    std::string name = weaponStr;
    // Remove _C suffix
    if (name.size() > 2 && name.substr(name.size() - 2) == "_C") {
        name = name.substr(0, name.size() - 2);
    }
    // Remove common prefixes
    for (const char* prefix : {"BP_", "SQ_"}) {
        if (name.find(prefix) == 0) {
            name = name.substr(strlen(prefix));
            break;
        }
    }
    return name.empty() ? "Unknown" : name;
}

// Parse chat from relay format

bool LogParser::parseChatRaw(const std::string& raw, ChatEvent& out) {
    if (raw.empty()) return false;

    // Try relay format: [ChatAll] [Online IDs:...] name : message
    std::smatch m;
    if (std::regex_search(raw, m, std::regex(
            R"(\[(ChatAll|ChatTeam|ChatSquad|ChatAdmin)\]\s*\[Online IDs:([^\]]+)\]\s*(.+?)\s*:\s*(.+))"))) {
        out.chatType = m[1].str();
        std::string idsStr = m[2].str();
        out.playerName = m[3].str();
        out.message = m[4].str();

        // Trim whitespace
        while (!out.playerName.empty() && out.playerName.back() == ' ') out.playerName.pop_back();
        while (!out.message.empty() && out.message.back() == ' ') out.message.pop_back();

        auto ids = parseOnlineIds(idsStr);
        out.steamId = ids.count("steamId") ? ids["steamId"] : "";
        out.eosId = ids.count("eosId") ? ids["eosId"] : "";
        return !out.steamId.empty() && !out.message.empty();
    }
    return false;
}

// Store wound info

void LogParser::storeWound(const std::string& line) {
    std::smatch m;
    if (!std::regex_search(line, m, reWound_)) return;

    std::string victimName = m[3].str();
    // Trim
    while (!victimName.empty() && victimName.back() == ' ') victimName.pop_back();

    std::string idsStr = m[6].str();
    if (idsStr.find("INVALID") != std::string::npos) return;

    auto ids = parseOnlineIds(idsStr);
    std::string weapon = extractWeapon(m[8].str());

    WoundRecord rec;
    rec.victimName = victimName;
    rec.damage = std::stod(m[4].str());
    rec.attackerController = m[5].str();
    rec.attackerSteamId = ids.count("steamId") ? ids["steamId"] : "";
    rec.weapon = weapon;
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    rec.time = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    std::lock_guard<std::mutex> lk(woundMtx_);
    woundStore_[victimName] = rec;
}

// Get recent wounds

std::vector<WoundEvent> LogParser::getRecentWounds() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    std::vector<WoundEvent> result;
    std::lock_guard<std::mutex> lk(woundMtx_);
    for (auto& [key, w] : woundStore_) {
        if (nowMs - w.time < Config::get().getInt("WOUND_DEDUP_WINDOW_MS", 3000)) {
            WoundEvent ev;
            ev.victimName = w.victimName;
            ev.attackerName = w.attackerController;
            ev.attackerSteamId = w.attackerSteamId;
            ev.weapon = w.weapon;
            ev.damage = w.damage;
            result.push_back(ev);
        }
    }
    return result;
}

// Cleanup expired entries

void LogParser::cleanup() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    std::lock_guard<std::mutex> lk(woundMtx_);
    for (auto it = woundStore_.begin(); it != woundStore_.end(); ) {
        if (nowMs - it->second.time > Config::get().getInt("WOUND_EXPIRY_MS", 30000)) {
            it = woundStore_.erase(it);
        } else {
            ++it;
        }
    }
}

// Process a single log line

std::vector<LogParser::ParsedLine> LogParser::processLine(const std::string& line, int serverId) {
    std::vector<ParsedLine> results;

    // Die (kill/suicide)
    if (line.find("Die()") != std::string::npos && line.find("LogSquadTrace") != std::string::npos) {
        std::smatch m;
        if (std::regex_search(line, m, reDie_)) {
            std::string victimName = m[3].str();
            while (!victimName.empty() && victimName.back() == ' ') victimName.pop_back();

            std::string attackerController = m[5].str();
            while (!attackerController.empty() && attackerController.back() == ' ') attackerController.pop_back();

            std::string idsStr = m[6].str();
            std::string weapon = extractWeapon(m[8].str());
            auto ids = parseOnlineIds(idsStr);

            // Skip if IDs are INVALID (except for suicide where attacker is nullptr)
            bool isNullAttacker = (attackerController == "nullptr" ||
                                   attackerController == "None" ||
                                   attackerController.empty());

            if (idsStr.find("INVALID") != std::string::npos && !isNullAttacker) {
                return results;
            }

            // Try to find matching wound record for attacker info
            std::string attackerSteamId = ids.count("steamId") ? ids["steamId"] : "";
            {
                std::lock_guard<std::mutex> lk(woundMtx_);
                auto it = woundStore_.find(victimName);
                if (it != woundStore_.end()) {
                    auto now = std::chrono::steady_clock::now().time_since_epoch();
                    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
                    if (nowMs - it->second.time <= Config::get().getInt("WOUND_EXPIRY_MS", 30000)) {
                        attackerSteamId = it->second.attackerSteamId.empty() ? attackerSteamId : it->second.attackerSteamId;
                        if (attackerController.find("PlayerController") != std::string::npos) {
                            attackerController = it->second.attackerController;
                        }
                        weapon = it->second.weapon.empty() ? weapon : it->second.weapon;
                        woundStore_.erase(it);
                    }
                }
            }

            ParsedLine pl;
            if (isNullAttacker) {
                pl.type = "suicide";
                pl.fields["victimName"] = cleanPlayerName(victimName).empty() ? victimName : cleanPlayerName(victimName);
                pl.fields["victimSteamId"] = attackerSteamId; // From woundStore, self-wound
                pl.fields["weapon"] = weapon;
                pl.fields["damage"] = m[4].str();
            } else {
                pl.type = "kill";
                pl.fields["killerName"] = attackerController;
                pl.fields["killerSteamId"] = attackerSteamId;
                pl.fields["victimName"] = cleanPlayerName(victimName).empty() ? victimName : cleanPlayerName(victimName);
                pl.fields["weapon"] = weapon;
                pl.fields["damage"] = m[4].str();
            }
            results.push_back(pl);
        }
        return results;
    }

    // Wound
    if (line.find("Wound()") != std::string::npos && line.find("LogSquadTrace") != std::string::npos) {
        storeWound(line);
        // Return only the wound we just stored (dedup by victim name)
        std::smatch wm;
        if (std::regex_search(line, wm, reWound_)) {
            std::string vName = wm[3].str();
            while (!vName.empty() && vName.back() == ' ') vName.pop_back();
            std::lock_guard<std::mutex> lk(woundMtx_);
            auto it = woundStore_.find(vName);
            if (it != woundStore_.end()) {
                ParsedLine pl;
                pl.type = "wound";
                pl.fields["victimName"] = it->second.victimName;
                pl.fields["attackerName"] = it->second.attackerController;
                pl.fields["attackerSteamId"] = it->second.attackerSteamId;
                pl.fields["weapon"] = it->second.weapon;
                results.push_back(pl);
            }
        }
        return results;
    }

    // Revive
    if (line.find("has revived") != std::string::npos && line.find("LogSquad:") != std::string::npos) {
        std::smatch m;
        if (std::regex_search(line, m, reRevive_)) {
            auto reviverIds = parseOnlineIds(m[4].str());
            auto victimIds = parseOnlineIds(m[6].str());

            ParsedLine pl;
            pl.type = "revive";
            pl.fields["reviverName"] = m[3].str();
            pl.fields["reviverSteamId"] = reviverIds.count("steamId") ? reviverIds["steamId"] : "";
            pl.fields["revivedName"] = m[5].str();
            pl.fields["revivedSteamId"] = victimIds.count("steamId") ? victimIds["steamId"] : "";
            results.push_back(pl);
        }
        return results;
    }

    // NewGame
    if (line.find("LogWorld: Bringing World") != std::string::npos) {
        std::smatch m;
        if (std::regex_search(line, m, reNewGame_)) {
            std::string layerClassname = m[5].str();
            if (layerClassname == "TransitionMap") return results;

            ParsedLine pl;
            pl.type = "newgame";
            pl.fields["map"] = layerClassname;
            pl.fields["dlc"] = m[3].str();
            results.push_back(pl);
        }
        return results;
    }

    // Join (PostLogin)
    if (line.find("PostLogin: NewPlayer:") != std::string::npos) {
        std::smatch m;
        if (std::regex_search(line, m, rePostLogin_)) {
            std::string idsStr = m[5].str();
            auto ids = parseOnlineIds(idsStr);

            ParsedLine pl;
            pl.type = "join";
            pl.fields["playerName"] = m[3].str();
            pl.fields["ip"] = m[4].str();
            pl.fields["steamId"] = ids.count("steamId") ? ids["steamId"] : "";
            pl.fields["eosId"] = ids.count("eosId") ? ids["eosId"] : "";
            results.push_back(pl);
        }
        return results;
    }

    // Join succeeded
    if (line.find("Join succeeded") != std::string::npos) {
        std::smatch m;
        if (std::regex_search(line, m, reJoinSucceeded_)) {
            ParsedLine pl;
            pl.type = "joinSucceeded";
            pl.fields["playerSuffix"] = m[3].str();
            results.push_back(pl);
        }
        return results;
    }

    // Disconnect
    if ((line.find("Close") != std::string::npos || line.find("CleanUp") != std::string::npos)
        && line.find("UniqueId") != std::string::npos) {
        std::smatch m;
        std::string ip, steamId, eosId, controller;

        if (std::regex_search(line, m, reDisconnect_)) {
            ip = m[3].str();
            controller = m[4].str();
            std::string rawId = m[5].str();
            if (rawId.find("Steam:") == 0) steamId = rawId.substr(6);
            else if (rawId.find("RedpointEOS:") == 0) eosId = rawId.substr(12);
            else if (rawId.size() == 17) steamId = rawId;
            else eosId = rawId;
        } else if (std::regex_search(line, m, reDisconnectBase_)) {
            ip = m[5].str();
            // Group 7: EOS, Group 8: Steam, Group 9: rawId
            if (m[8].matched && !m[8].str().empty()) steamId = m[8].str();
            else if (m[7].matched && !m[7].str().empty()) eosId = m[7].str();
            else if (m[9].matched) {
                std::string rawId = m[9].str();
                if (rawId.size() == 17 && std::all_of(rawId.begin(), rawId.end(), ::isdigit))
                    steamId = rawId;
                else eosId = rawId;
            }
        }

        if (!ip.empty() || !steamId.empty()) {
            ParsedLine pl;
            pl.type = "disconnect";
            pl.fields["ip"] = ip;
            pl.fields["steamId"] = steamId;
            pl.fields["eosId"] = eosId;
            pl.fields["controller"] = controller;
            results.push_back(pl);
        }
        return results;
    }

    // Chat (log format)
    if (line.find("[ChatLog]") != std::string::npos) {
        std::smatch m;
        if (std::regex_search(line, m, reChatLog_)) {
            ParsedLine pl;
            pl.type = "chat";
            pl.fields["playerName"] = m[1].str();
            pl.fields["steamId"] = m[2].str();
            pl.fields["message"] = m[3].str();
            results.push_back(pl);
        }
        return results;
    }

    // Admin Broadcast
    if (line.find("ADMIN COMMAND: Message broadcasted") != std::string::npos) {
        std::smatch m;
        if (std::regex_search(line, m, reAdminBroadcast_)) {
            ParsedLine pl;
            pl.type = "adminBroadcast";
            pl.fields["message"] = m[3].str();
            pl.fields["from"] = m[4].str();
            results.push_back(pl);
        }
        return results;
    }

    // Server Tick Rate
    if (line.find("Server Tick Rate:") != std::string::npos) {
        std::smatch m;
        if (std::regex_search(line, m, reTickRate_)) {
            ParsedLine pl;
            pl.type = "tickRate";
            pl.fields["tickRate"] = m[3].str();
            results.push_back(pl);
        }
        return results;
    }

    // Create Squad
    if (line.find("created Squad") != std::string::npos ||
        (line.find("Created Squad") != std::string::npos && line.find("with ID:") != std::string::npos)) {
        std::smatch m;
        if (std::regex_search(line, m, reCreateSquadNew_)) {
            ParsedLine pl;
            pl.type = "createSquad";
            pl.fields["creatorName"] = m[1].str();
            pl.fields["squadId"] = m[2].str();
            pl.fields["squadName"] = m[3].str();
            pl.fields["teamName"] = m[4].str();
            // Extract creator Steam ID from Online IDs
            std::regex idRe(R"(\(Online\s+IDs:\s*([^)]+)\))", std::regex::icase);
            auto idBegin = std::sregex_iterator(line.begin(), line.end(), idRe);
            auto idEnd = std::sregex_iterator();
            for (auto it = idBegin; it != idEnd; ++it) {
                auto ids = parseOnlineIds((*it)[1].str());
                if (ids.count("steamId")) {
                    pl.fields["creatorSteamId"] = ids["steamId"];
                    break;
                }
            }
            results.push_back(pl);
        } else if (std::regex_search(line, m, reCreateSquadOld_)) {
            ParsedLine pl;
            pl.type = "createSquad";
            pl.fields["creatorName"] = m[1].str();
            pl.fields["squadName"] = m[2].str();
            pl.fields["squadId"] = m[3].str();
            results.push_back(pl);
        }
        return results;
    }

    return results;
}

// Parse RCON ListPlayers

std::vector<PlayerInfo> LogParser::parsePlayerList(const std::string& raw) {
    std::vector<PlayerInfo> players;

    std::istringstream stream(raw);
    std::string line;
    while (std::getline(stream, line)) {
        // Must have SteamID (17-digit number)
        std::smatch steamMatch;
        std::regex steamRe(R"((?:SteamID:\s*|steam:\s*)(\d{17}))", std::regex::icase);
        if (!std::regex_search(line, steamMatch, steamRe)) continue;

        PlayerInfo p;
        p.steamId = steamMatch[1].str();

        // RCON ID
        std::smatch idMatch;
        if (std::regex_search(line, idMatch, std::regex(R"(^(?:ID:\s*)?(\d+)\))", std::regex::icase)) ||
            std::regex_search(line, idMatch, std::regex(R"(^ID:\s*(\d+))", std::regex::icase))) {
            p.rconId = std::stoi(idMatch[1].str());
        }

        // Name
        std::smatch nameMatch;
        if (std::regex_search(line, nameMatch,
                std::regex(R"(Name:\s*([^|]+?)(?:\s*\||\s*(?:Team|Squad|Is|Role|IP|Ping|Time|$)))", std::regex::icase))) {
            p.name = nameMatch[1].str();
            while (!p.name.empty() && p.name.back() == ' ') p.name.pop_back();
        }
        if (p.name.empty()) p.name = "Unknown";

        // Team ID
        std::smatch teamMatch;
        if (std::regex_search(line, teamMatch, std::regex(R"(Team\s+ID:\s*(\d+))", std::regex::icase))) {
            p.teamId = teamMatch[1].str();
        }
        if (p.teamId.empty()) p.teamId = "0";

        // Squad ID
        std::smatch squadMatch;
        if (std::regex_search(line, squadMatch, std::regex(R"(Squad\s*(?:ID)?:\s*(\d+|N\/A|-1))", std::regex::icase))) {
            p.squadId = squadMatch[1].str();
        }
        if (p.squadId.empty()) p.squadId = "N/A";

        // Is Leader
        std::smatch leaderMatch;
        if (std::regex_search(line, leaderMatch, std::regex(R"(Is\s+Leader:\s*(True|False))", std::regex::icase))) {
            p.isLeader = (leaderMatch[1].str() == "True");
        }

        players.push_back(p);
    }

    return players;
}

} // namespace sp
