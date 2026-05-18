#include "core/log_parser.h"
#include "core/core.h"

#include <sstream>
#include <algorithm>

namespace sp {

// ─────────────────────────────────────────────
// Constructor: compile all regexes
// ─────────────────────────────────────────────
LogParser::LogParser() {
    // ── Die ──
    // Rain Ops Mini format:
    // [2026.01.01-12.00.00:000][  0]LogSquadTrace: [DedicatedServer]Die(): Player:VictimName KillingDamage=100.0 from AttackerName (Online IDs: EOS: xxx steam: xxx | Contoller ID: xxx) caused by BP_Weapon
    reDie_ = std::regex(
        R"(\[(\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}:\d{3})\]\[(\d*)\]LogSquadTrace: \[DedicatedServer\](?:ASQSoldier::)?Die\(\): Player:(.+?) KillingDamage=(-?[\d.]+) from (.+?) \(Online IDs: EOS: (\w+) steam: (\d+) \| Contoller? ID: (\w+)\) caused by (.+))");

    // ── Wound ──
    // Same structure as Die but Wound()
    reWound_ = std::regex(
        R"(\[(\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}:\d{3})\]\[(\d*)\]LogSquadTrace: \[DedicatedServer\](?:ASQSoldier::)?Wound\(\): Player:(.+?) KillingDamage=(-?[\d.]+) from (.+?) \(Online IDs: EOS: (\w+) steam: (\d+) \| Controller? ID: (\w+)\) caused by (.+))");

    // ── Revive ──
    // [time][thread]LogSquad: ReviverName (Online IDs: EOS: xxx steam: xxx) has revived VictimName (Online IDs: EOS: xxx steam: xxx).
    reRevive_ = std::regex(
        R"(\[(\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}:\d{3})\]\[(\d*)\]LogSquad:\s*(.+?) \(Online IDs: EOS: (\w+) steam: (\d+)\) has revived (.+?) \(Online IDs: EOS: (\w+) steam: (\d+)\)\.)");

    // ── Chat (log format) ──
    reChatLog_ = std::regex(
        R"(\[ChatLog\]\s*(.+?)\s*\((\d{17})\):\s*(.+))",
        std::regex::icase);

    // ── Chat (relay format) ──
    // [ChatAll] [Online IDs:EOS: xxx steam: xxx] PlayerName : message
    reChatRelay_ = std::regex(
        R"(\[(ChatAll|ChatTeam|ChatSquad|ChatAdmin)\]\s*\[Online IDs:?\s*EOS:\s*(\w+)\s+steam:\s*(\d+)\]\s*(?:\[(.+?)\]\s*(.*)|(.+?)\s*:\s*(.*)))");

    // ── PostLogin ──
    // [time][thread]LogSquad: PostLogin: NewPlayer: BP_PlayerController... (IP: 1.2.3.4 | Online IDs: EOS: xxx steam: xxx)
    rePostLogin_ = std::regex(
        R"(\[(\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}:\d{3})\]\[(\d*)\]LogSquad: PostLogin: NewPlayer:(.+?) \(IP: ([\d.]+) \| Online IDs: EOS: (\w+) steam: (\d+)\))");

    // ── Disconnect (UChannel::Close) ──
    // Rain Ops Mini precise format:
    // [time][thread]LogNet: UChannel::Close: Sending CloseBunch. ChIndex == X. Name: [UChannel] ChIndex: X, Closing: X [UNetConnection] RemoteAddr: X:X, Name: RedpointEOSIpNetConnection_X, Driver: X, IsServer: YES, PC: X, Owner: X, UniqueId: RedpointEOS:xxx
    reDisconnect_ = std::regex(
        R"(\[(\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}:\d{3})\]\[(\d*)\]LogNet: UChannel::Close: Sending CloseBunch\. ChIndex == (\d+)\. Name: \[UChannel\] ChIndex: (\d+), Closing: (\d+) \[UNetConnection\] RemoteAddr: ([\d.]+):(\d+), Name: RedpointEOSIpNetConnection_(\w+), Driver: (\w+), IsServer: YES, PC: (\w+), Owner: (\w+), UniqueId: RedpointEOS:(\w+))");

    // ── Disconnect (BeginInactiveState) ──
    // Rain Ops Mini format:
    // [time][thread]LogSquadTrace: [DedicatedServer]BeginInactiveState(): PC=PlayerName (Online IDs: EOS: xxx steam: xxx)
    reBeginInactive_ = std::regex(
        R"(\[(\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}:\d{3})\]\[(\d*)\]LogSquadTrace: \[DedicatedServer\]BeginInactiveState\(\): PC=(.+?) \(Online IDs: EOS: (\w+) steam: (\d+)\))");

    // ── Create Squad ──
    // Rain Ops Mini format:
    // [time][thread]LogSquad: PlayerName (Online IDs: EOS: xxx steam: xxx) has created Squad 1 (Squad Name: Alpha) on Team 1
    reCreateSquad_ = std::regex(
        R"(\[(\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}:\d{3})\]\[(\d*)\]LogSquad:\s*(.+?) \(Online IDs: EOS: (\w+) steam: (\d+)\) has created Squad (\d+) \(Squad Name: (.*?)\) on (.+))");

    // ── Create Squad (old format compat) ──
    reCreateSquadOld_ = std::regex(
        R"(Player:(.+?)(?:\s*\(Online IDs[^\)]*\))?\s+Created Squad (.+?) with ID:\s*(\d+))",
        std::regex::icase);

    // ── Tick Rate ──
    reTickRate_ = std::regex(
        R"(\[(\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}:\d{3})\]\[(\d*)\]LogSquad: USQGameState: Server Tick Rate: ([\d.]+))");

    // ── New Game ──
    reNewGame_ = std::regex(
        R"(\[(\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}:\d{3})\]\[(\d*)\]LogWorld: Bringing World /([A-z0-9]+)/(?:Maps/)?([A-z0-9-]+)/(?:.*/)?([A-z0-9-]+)(?:\.[A-z0-9-]+))");

    // ── Match End ──
    // Rain Ops Mini format:
    // [time][thread]LogSquadGameEvents: Display: Team 1, 1st Infantry Division has won the match with 250 Tickets on layer Yehorivka_AAS_v1 (level Yehorivka)!
    reMatchEnd_ = std::regex(
        R"(\[(\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}:\d{3})\]\[(\d*)\]LogSquadGameEvents: Display: Team (\d+), (.+?) has (won|lost) the match with (\d+) Tickets? on layer (.+?) \(level (.+)\)!)");

    // ── Change Layer ──
    reChangeLayer_ = std::regex(
        R"(\[(\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}:\d{3})\]\[(\d*)\]LogSquad: ADMIN COMMAND: Change layer to (.+?) from (.+))");

    // ── Set Next Layer ──
    reSetNextLayer_ = std::regex(
        R"(\[(\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}:\d{3})\]\[(\d*)\]LogSquad: ADMIN COMMAND: Set next layer to (.+?) from (.+))");

    // ── Removed Player (AntiCheat) ──
    reRemovedPlayer_ = std::regex(
        R"(\[(\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}:\d{3})\]\[(\d*)\]LogEOSAntiCheat: Verbose: Dedicated server Anti-Cheat: (.+?): UnregisterPlayer\(Session: (\w+), UserId: (\w+)\): Removed player from player tracking\.)");

    // ── Attack (TakeDamage / ActualDamage) ──
    reAttack_ = std::regex(
        R"(\[(\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}:\d{3})\]\[(\d*)\]LogSquad: Player:(.+?) ActualDamage=([\d.]+) from (.+?) \(Online IDs: EOS: (\w+) steam: (\d+) \| Player Controller ID: (\w+)\)caused by (.+))");

    // ── Admin Broadcast ──
    reAdminBroadcast_ = std::regex(
        R"(\[(\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}:\d{3})\]\[(\d*)\]LogSquad: ADMIN COMMAND: Message broadcasted <(.+)> from (.+))");

    // ── Join Succeeded ──
    reJoinSucceeded_ = std::regex(
        R"(\[(\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}:\d{3})\]\[(\d*)\]LogNet: Join succeeded: (.+))");
}


// ─────────────────────────────────────────────
// Preprocess: replace INVALID with placeholder
// (Rain Ops Mini technique — handles barbed wire,
//  artillery splash, etc. where attacker is INVALID)
// ─────────────────────────────────────────────
std::string LogParser::preprocessInvalidIds(const std::string& line) {
    if (line.find("INVALID") == std::string::npos) return line;
    // Replace "INVALID" with a valid-looking placeholder so regex matches
    std::string result = line;
    // "INVALID" appears where "EOS: xxx steam: xxx" is expected
    // Replace with: "EOS: 0000000000000000000000000000000 steam: 00000000000000000"
    auto pos = result.find("INVALID");
    while (pos != std::string::npos) {
        result.replace(pos, 7, "EOS: 0000000000000000000000000000000 steam: 00000000000000000");
        pos = result.find("INVALID", pos + 53);
    }
    return result;
}


// ─────────────────────────────────────────────
// Parse Online IDs: "EOS: xxx steam: xxx"
// ─────────────────────────────────────────────
std::unordered_map<std::string, std::string> LogParser::parseOnlineIds(const std::string& idsStr) {
    std::unordered_map<std::string, std::string> result;
    if (idsStr.empty()) return result;

    // Format: "EOS: <hex> steam: <digits>"
    std::regex matcher(R"(EOS:\s*(\w+)\s+steam:\s*(\d+))");
    std::smatch m;
    if (std::regex_search(idsStr, m, matcher)) {
        result["eosId"] = m[1].str();
        result["steamId"] = m[2].str();
    }
    return result;
}


// ─────────────────────────────────────────────
// Clean player name
// ─────────────────────────────────────────────
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
        if (uc >= 0xC0) latin1Count++;
    }
    for (size_t i = 0; i + 2 < cleaned.size(); i++) {
        unsigned char b0 = static_cast<unsigned char>(cleaned[i]);
        if (b0 >= 0xE4 && b0 <= 0xE9) cjkCount++;
    }

    if (latin1Count > static_cast<int>(cleaned.size()) * 3 / 10 && cjkCount == 0) {
        return "";
    }
    return cleaned;
}


// ─────────────────────────────────────────────
// Extract weapon: strip BP_/SQ_ prefix and _C suffix
// ─────────────────────────────────────────────
std::string LogParser::extractWeapon(const std::string& weaponStr) {
    if (weaponStr.empty()) return "Unknown";
    std::string name = weaponStr;
    if (name.size() > 2 && name.substr(name.size() - 2) == "_C") {
        name = name.substr(0, name.size() - 2);
    }
    for (const char* prefix : {"BP_", "SQ_"}) {
        if (name.find(prefix) == 0) {
            name = name.substr(strlen(prefix));
            break;
        }
    }
    return name.empty() ? "Unknown" : name;
}


// ─────────────────────────────────────────────
// Extract weapon middle part (Rain Ops Mini technique)
// "BP_SoldierInsurgent_Wep_MAK47_Component_C" → "Wep_MAK47"
// ─────────────────────────────────────────────
std::string LogParser::extractWeaponMiddle(const std::string& weaponStr) {
    if (weaponStr.empty()) return "Unknown";
    std::vector<std::string> parts;
    std::stringstream ss(weaponStr);
    std::string item;
    while (std::getline(ss, item, '_')) {
        if (!item.empty()) parts.push_back(item);
    }
    if (parts.size() < 4) return extractWeapon(weaponStr); // fallback
    std::string result;
    for (size_t i = 1; i < parts.size() - 2; i++) {
        if (i > 1) result += "_";
        result += parts[i];
    }
    return result.empty() ? extractWeapon(weaponStr) : result;
}


// ─────────────────────────────────────────────
// Parse chat from relay format
// ─────────────────────────────────────────────
bool LogParser::parseChatRaw(const std::string& raw, ChatEvent& out) {
    if (raw.empty()) return false;

    std::smatch m;
    // [ChatAll] [Online IDs:EOS: xxx steam: xxx] name : message
    if (std::regex_search(raw, m, std::regex(
            R"(\[(ChatAll|ChatTeam|ChatSquad|ChatAdmin)\]\s*\[Online IDs:?\s*EOS:\s*(\w+)\s+steam:\s*(\d+)\]\s*(?:\[(.+?)\]\s*(.*)|(.+?)\s*:\s*(.*)))"))) {
        out.chatType = m[1].str();
        out.eosId = m[2].str();
        out.steamId = m[3].str();
        // Group 4/5 = [Name] Message format, Group 6/7 = Name : Message format
        if (m[4].matched && !m[4].str().empty()) {
            out.playerName = m[4].str();
            out.message = m[5].str();
        } else {
            out.playerName = m[6].str();
            out.message = m[7].str();
        }
        while (!out.playerName.empty() && out.playerName.back() == ' ') out.playerName.pop_back();
        while (!out.message.empty() && out.message.back() == ' ') out.message.pop_back();
        return !out.steamId.empty() && !out.message.empty();
    }
    return false;
}


// ─────────────────────────────────────────────
// Store wound (for Wound→Die dedup)
// ─────────────────────────────────────────────
void LogParser::storeWound(const std::string& line) {
    std::string processed = preprocessInvalidIds(line);
    std::smatch m;
    if (!std::regex_search(processed, m, reWound_)) return;

    std::string victimName = m[3].str();
    while (!victimName.empty() && victimName.back() == ' ') victimName.pop_back();

    std::string weapon = extractWeapon(m[9].str());

    WoundRecord rec;
    rec.victimName = victimName;
    rec.damage = std::stod(m[4].str());
    rec.attackerController = m[5].str();
    while (!rec.attackerController.empty() && rec.attackerController.back() == ' ')
        rec.attackerController.pop_back();
    rec.attackerEosId = m[6].str();
    rec.attackerSteamId = m[7].str();
    rec.weapon = weapon;
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    rec.time = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    std::lock_guard<std::mutex> lk(woundMtx_);
    woundStore_[victimName] = rec;
}


// ─────────────────────────────────────────────
// Get recent wounds
// ─────────────────────────────────────────────
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
            ev.attackerEosId = w.attackerEosId;
            ev.weapon = w.weapon;
            ev.damage = w.damage;
            result.push_back(ev);
        }
    }
    return result;
}


// ─────────────────────────────────────────────
// Cleanup expired wound entries
// ─────────────────────────────────────────────
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


// ─────────────────────────────────────────────
// Process a single log line
// ─────────────────────────────────────────────
std::vector<LogParser::ParsedLine> LogParser::processLine(const std::string& line, int serverId) {
    std::vector<ParsedLine> results;

    // Preprocess: replace INVALID with placeholder so regex matches
    std::string processed = preprocessInvalidIds(line);

    // ── Die ──
    if (processed.find("Die()") != std::string::npos && processed.find("LogSquadTrace") != std::string::npos) {
        std::smatch m;
        if (std::regex_search(processed, m, reDie_)) {
            std::string victimName = m[3].str();
            while (!victimName.empty() && victimName.back() == ' ') victimName.pop_back();

            std::string attackerController = m[5].str();
            while (!attackerController.empty() && attackerController.back() == ' ') attackerController.pop_back();

            std::string attackerEosId = m[6].str();
            std::string attackerSteamId = m[7].str();
            std::string weapon = extractWeapon(m[9].str());

            // Skip placeholder IDs (INVALID was replaced with zeros)
            bool isNullAttacker = (attackerSteamId == "00000000000000000" ||
                                   attackerController == "nullptr" ||
                                   attackerController == "None" ||
                                   attackerController.empty());

            // Try to find matching wound record for attacker info
            {
                std::lock_guard<std::mutex> lk(woundMtx_);
                auto it = woundStore_.find(victimName);
                if (it != woundStore_.end()) {
                    auto now = std::chrono::steady_clock::now().time_since_epoch();
                    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
                    if (nowMs - it->second.time <= Config::get().getInt("WOUND_EXPIRY_MS", 30000)) {
                        // Use wound record's attacker info (more reliable)
                        if (!it->second.attackerSteamId.empty() && it->second.attackerSteamId != "00000000000000000") {
                            attackerSteamId = it->second.attackerSteamId;
                            attackerEosId = it->second.attackerEosId;
                        }
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
                pl.fields["victimSteamId"] = attackerSteamId;
                pl.fields["victimEosId"] = attackerEosId;
                pl.fields["weapon"] = weapon;
                pl.fields["damage"] = m[4].str();
            } else {
                pl.type = "kill";
                pl.fields["killerName"] = attackerController;
                pl.fields["killerSteamId"] = attackerSteamId;
                pl.fields["killerEosId"] = attackerEosId;
                pl.fields["victimName"] = cleanPlayerName(victimName).empty() ? victimName : cleanPlayerName(victimName);
                pl.fields["victimSteamId"] = "";  // victim ID not in Die line, filled by caller from RCON cache
                pl.fields["weapon"] = weapon;
                pl.fields["damage"] = m[4].str();
            }
            results.push_back(pl);
        }
        return results;
    }

    // ── Wound ──
    if (processed.find("Wound()") != std::string::npos && processed.find("LogSquadTrace") != std::string::npos) {
        storeWound(line);  // store original line (preprocessInside)
        std::smatch wm;
        if (std::regex_search(processed, wm, reWound_)) {
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
                pl.fields["attackerEosId"] = it->second.attackerEosId;
                pl.fields["weapon"] = it->second.weapon;
                pl.fields["damage"] = std::to_string(it->second.damage);
                results.push_back(pl);
            }
        }
        return results;
    }

    // ── Revive ──
    if (processed.find("has revived") != std::string::npos && processed.find("LogSquad:") != std::string::npos) {
        std::smatch m;
        if (std::regex_search(processed, m, reRevive_)) {
            ParsedLine pl;
            pl.type = "revive";
            pl.fields["reviverName"] = m[3].str();
            pl.fields["reviverEosId"] = m[4].str();
            pl.fields["reviverSteamId"] = m[5].str();
            pl.fields["revivedName"] = m[6].str();
            pl.fields["revivedEosId"] = m[7].str();
            pl.fields["revivedSteamId"] = m[8].str();
            results.push_back(pl);
        }
        return results;
    }

    // ── New Game ──
    if (processed.find("LogWorld: Bringing World") != std::string::npos) {
        std::smatch m;
        if (std::regex_search(processed, m, reNewGame_)) {
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

    // ── Join (PostLogin) ──
    if (processed.find("PostLogin: NewPlayer:") != std::string::npos) {
        std::smatch m;
        if (std::regex_search(processed, m, rePostLogin_)) {
            ParsedLine pl;
            pl.type = "join";
            pl.fields["playerName"] = m[3].str();
            while (!pl.fields["playerName"].empty() && pl.fields["playerName"].back() == ' ')
                pl.fields["playerName"].pop_back();
            pl.fields["ip"] = m[4].str();
            pl.fields["eosId"] = m[5].str();
            pl.fields["steamId"] = m[6].str();
            results.push_back(pl);
        }
        return results;
    }

    // ── Join Succeeded ──
    if (processed.find("Join succeeded") != std::string::npos) {
        std::smatch m;
        if (std::regex_search(processed, m, reJoinSucceeded_)) {
            ParsedLine pl;
            pl.type = "joinSucceeded";
            pl.fields["playerSuffix"] = m[3].str();
            results.push_back(pl);
        }
        return results;
    }

    // ── Disconnect (UChannel::Close) ──
    if (processed.find("UChannel::Close:") != std::string::npos) {
        std::smatch m;
        if (std::regex_search(processed, m, reDisconnect_)) {
            ParsedLine pl;
            pl.type = "disconnect";
            pl.fields["ip"] = m[6].str();
            pl.fields["eosId"] = m[12].str();
            pl.fields["steamId"] = "";  // this format only has EOS
            pl.fields["controller"] = m[10].str();
            results.push_back(pl);
        }
        return results;
    }

    // ── Disconnect (BeginInactiveState — Rain Ops Mini) ──
    if (processed.find("BeginInactiveState()") != std::string::npos) {
        std::smatch m;
        if (std::regex_search(processed, m, reBeginInactive_)) {
            ParsedLine pl;
            pl.type = "disconnect";
            pl.fields["playerName"] = m[3].str();
            while (!pl.fields["playerName"].empty() && pl.fields["playerName"].back() == ' ')
                pl.fields["playerName"].pop_back();
            pl.fields["eosId"] = m[4].str();
            pl.fields["steamId"] = m[5].str();
            pl.fields["ip"] = "";
            results.push_back(pl);
        }
        return results;
    }

    // ── Chat (log format) ──
    if (processed.find("[ChatLog]") != std::string::npos) {
        std::smatch m;
        if (std::regex_search(processed, m, reChatLog_)) {
            ParsedLine pl;
            pl.type = "chat";
            pl.fields["playerName"] = m[1].str();
            pl.fields["steamId"] = m[2].str();
            pl.fields["message"] = m[3].str();
            results.push_back(pl);
        }
        return results;
    }

    // ── Admin Broadcast ──
    if (processed.find("ADMIN COMMAND: Message broadcasted") != std::string::npos) {
        std::smatch m;
        if (std::regex_search(processed, m, reAdminBroadcast_)) {
            ParsedLine pl;
            pl.type = "adminBroadcast";
            pl.fields["message"] = m[3].str();
            pl.fields["from"] = m[4].str();
            results.push_back(pl);
        }
        return results;
    }

    // ── Server Tick Rate ──
    if (processed.find("Server Tick Rate:") != std::string::npos) {
        std::smatch m;
        if (std::regex_search(processed, m, reTickRate_)) {
            ParsedLine pl;
            pl.type = "tickRate";
            pl.fields["tickRate"] = m[3].str();
            results.push_back(pl);
        }
        return results;
    }

    // ── Create Squad ──
    if (processed.find("has created Squad") != std::string::npos) {
        std::smatch m;
        if (std::regex_search(processed, m, reCreateSquad_)) {
            ParsedLine pl;
            pl.type = "createSquad";
            pl.fields["creatorName"] = m[3].str();
            while (!pl.fields["creatorName"].empty() && pl.fields["creatorName"].back() == ' ')
                pl.fields["creatorName"].pop_back();
            pl.fields["creatorEosId"] = m[4].str();
            pl.fields["creatorSteamId"] = m[5].str();
            pl.fields["squadId"] = m[6].str();
            pl.fields["squadName"] = m[7].str();
            while (!pl.fields["squadName"].empty() && pl.fields["squadName"].back() == ' ')
                pl.fields["squadName"].pop_back();
            pl.fields["teamName"] = m[8].str();
            while (!pl.fields["teamName"].empty() && pl.fields["teamName"].back() == ' ')
                pl.fields["teamName"].pop_back();
            results.push_back(pl);
        } else {
            // Old format fallback
            std::smatch m2;
            if (std::regex_search(processed, m2, reCreateSquadOld_)) {
                ParsedLine pl;
                pl.type = "createSquad";
                pl.fields["creatorName"] = m2[1].str();
                pl.fields["squadName"] = m2[2].str();
                pl.fields["squadId"] = m2[3].str();
                results.push_back(pl);
            }
        }
        return results;
    }

    // ── Match End (Rain Ops Mini) ──
    if (processed.find("the match with") != std::string::npos &&
        processed.find("LogSquadGameEvents") != std::string::npos) {
        std::smatch m;
        if (std::regex_search(processed, m, reMatchEnd_)) {
            ParsedLine pl;
            pl.type = "matchEnd";
            pl.fields["teamNumber"] = m[3].str();
            pl.fields["teamInfo"] = m[4].str();
            pl.fields["result"] = m[5].str();  // "won" or "lost"
            pl.fields["tickets"] = m[6].str();
            pl.fields["mapLayer"] = m[7].str();
            pl.fields["mapLevel"] = m[8].str();
            results.push_back(pl);
        }
        return results;
    }

    // ── Change Layer (Rain Ops Mini) ──
    if (processed.find("ADMIN COMMAND: Change layer") != std::string::npos) {
        std::smatch m;
        if (std::regex_search(processed, m, reChangeLayer_)) {
            ParsedLine pl;
            pl.type = "changeLayer";
            pl.fields["layer"] = m[3].str();
            pl.fields["admin"] = m[4].str();
            results.push_back(pl);
        }
        return results;
    }

    // ── Set Next Layer (Rain Ops Mini) ──
    if (processed.find("ADMIN COMMAND: Set next layer") != std::string::npos) {
        std::smatch m;
        if (std::regex_search(processed, m, reSetNextLayer_)) {
            ParsedLine pl;
            pl.type = "setNextLayer";
            pl.fields["layer"] = m[3].str();
            pl.fields["admin"] = m[4].str();
            results.push_back(pl);
        }
        return results;
    }

    // ── Removed Player (Rain Ops Mini) ──
    if (processed.find("Removed player from player tracking") != std::string::npos) {
        std::smatch m;
        if (std::regex_search(processed, m, reRemovedPlayer_)) {
            ParsedLine pl;
            pl.type = "removedPlayer";
            pl.fields["anticheatId"] = m[3].str();
            pl.fields["sessionId"] = m[4].str();
            pl.fields["eosId"] = m[5].str();
            results.push_back(pl);
        }
        return results;
    }

    // ── Attack / TakeDamage (Rain Ops Mini) ──
    if (processed.find("ActualDamage") != std::string::npos) {
        std::smatch m;
        if (std::regex_search(processed, m, reAttack_)) {
            ParsedLine pl;
            pl.type = "attack";
            pl.fields["victimName"] = m[3].str();
            while (!pl.fields["victimName"].empty() && pl.fields["victimName"].back() == ' ')
                pl.fields["victimName"].pop_back();
            pl.fields["damage"] = m[4].str();
            pl.fields["attackerName"] = m[5].str();
            while (!pl.fields["attackerName"].empty() && pl.fields["attackerName"].back() == ' ')
                pl.fields["attackerName"].pop_back();
            pl.fields["attackerEosId"] = m[6].str();
            pl.fields["attackerSteamId"] = m[7].str();
            pl.fields["weapon"] = m[9].str();
            results.push_back(pl);
        }
        return results;
    }

    return results;
}


// ─────────────────────────────────────────────
// Parse RCON ListPlayers output
// ─────────────────────────────────────────────
std::vector<PlayerInfo> LogParser::parsePlayerList(const std::string& raw) {
    std::vector<PlayerInfo> players;
    if (raw.empty()) return players;

    // RCON ListPlayers format:
    // ID: 40 | Online IDs: EOS: 00026c77c7fc494fa0ab3a337382826f steam: 76561199065911668 | Name:  ANDRUHA | Team ID: 1 | Squad ID: N/A | Is Leader: False | Role: USA_Rifleman_01
    std::regex playerRe(
        R"(ID:\s*(\d+)\s*\|\s*Online IDs:\s*EOS:\s*(\w+)\s+steam:\s*(\d+)\s*\|\s*Name:\s*(.+?)\s*\|\s*Team ID:\s*(\d+)\s*\|\s*Squad ID:\s*(.+?)\s*\|\s*Is Leader:\s*(\w+)\s*\|\s*Role:\s*(.+))");

    std::istringstream iss(raw);
    std::string line;
    while (std::getline(iss, line)) {
        std::smatch m;
        if (std::regex_search(line, m, playerRe)) {
            PlayerInfo p;
            p.rconId = std::stoi(m[1].str());
            p.eosId = m[2].str();
            p.steamId = m[3].str();
            p.name = m[4].str();
            while (!p.name.empty() && p.name.back() == ' ') p.name.pop_back();
            p.teamId = m[5].str();
            std::string squadStr = m[6].str();
            while (!squadStr.empty() && squadStr.back() == ' ') squadStr.pop_back();
            if (squadStr != "N/A") p.squadId = squadStr;
            p.isLeader = (m[7].str() == "True");
            p.role = m[8].str();
            while (!p.role.empty() && p.role.back() == ' ') p.role.pop_back();
            players.push_back(p);
        }
    }
    return players;
}

} // namespace sp
