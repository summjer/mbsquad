/*
 * Squad Relay v1.1-cpp
 * 日志中转 + 游戏进程管理 + RCON + 解封 + Admin 同步
 *
 * 部署位置：Squad 游戏服务器（Windows）
 * 零依赖，单 exe，双击即用
 *
 * 编译：x86_64-w64-mingw32-g++ -std=c++17 -O2 -s -static -Ivendor -o squad-relay.exe main.cpp -lws2_32 -lbcrypt -lshell32
 */

#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <functional>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <random>
#include <iomanip>
#include <ctime>
#include <condition_variable>
#include <queue>

#ifdef _WIN32
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32")
#pragma comment(lib, "bcrypt")
#pragma comment(lib, "shell32")
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <netdb.h>
#include <fcntl.h>
#include <iconv.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define closesocket ::close
#endif

#include "vendor/json.hpp"
#include "src/web_content.h"

using json = nlohmann::json;
using namespace std::chrono_literals;

// ═══════════════════ 常量 ═══════════════════
static const std::string VERSION = "1.1-cpp";

// ═══════════════════ WSInit (Windows 自动初始化 Winsock) ═══════════════════
#ifdef _WIN32
struct WSInit {
    WSInit() { WSADATA w; WSAStartup(MAKEWORD(2,2), &w); }
    ~WSInit() { WSACleanup(); }
} g_wsInit;
#endif

// ═══════════════════ 日志 ═══════════════════
static std::mutex g_logMtx;
std::string nowISO() {
    auto t = std::chrono::system_clock::now();
    auto tc = std::chrono::system_clock::to_time_t(t);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t.time_since_epoch()) % 1000;
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &tc);
#else
    localtime_r(&tc, &tm);
#endif
    char buf[32];
    snprintf(buf,sizeof(buf),"%04d-%02d-%02d %02d:%02d:%02d.%03d",
        tm.tm_year+1900,tm.tm_mon+1,tm.tm_mday,tm.tm_hour,tm.tm_min,tm.tm_sec,(int)ms.count());
    return buf;
}
void log(const std::string& tag,const std::string& msg){
    std::lock_guard<std::mutex> lk(g_logMtx);
    std::cout<<nowISO()<<" ["<<tag<<"] "<<msg<<std::endl;
}
void logErr(const std::string& tag,const std::string& msg){
    std::lock_guard<std::mutex> lk(g_logMtx);
    std::cerr<<nowISO()<<" ["<<tag<<"] ERR "<<msg<<std::endl;
}

// ═══════════════════ 工具 ═══════════════════
std::string readFile(const std::string& p){
    std::ifstream f(p,std::ios::binary); if(!f) return "";
    std::ostringstream ss; ss<<f.rdbuf(); return ss.str();
}
bool writeFile(const std::string& p,const std::string& c){
    std::ofstream f(p,std::ios::binary); if(!f) return false; f<<c; return true;
}
bool fileExists(const std::string& p){ std::ifstream f(p); return f.good(); }
std::string trim(const std::string& s){
    auto a=s.find_first_not_of(" \t\r\n"); if(a==std::string::npos) return "";
    auto b=s.find_last_not_of(" \t\r\n"); return s.substr(a,b-a+1);
}
std::string dirOf(const std::string& p){
    auto pos=p.find_last_of("\\/"); return pos==std::string::npos?".":p.substr(0,pos);
}
std::string baseName(const std::string& p){
    auto pos=p.find_last_of("\\/"); return pos==std::string::npos?p:p.substr(pos+1);
}

// ═══════════════════ 轻量 HTTP 客户端 ═══════════════════
struct HttpResp { int status=0; std::string body; bool ok=false; };

SOCKET tcpConnect(const std::string& host, int port, int timeoutMs=10000){
    SOCKET s=socket(AF_INET,SOCK_STREAM,0);
    if(s==INVALID_SOCKET) return INVALID_SOCKET;
    struct addrinfo hints{},*res=nullptr;
    hints.ai_family=AF_INET; hints.ai_socktype=SOCK_STREAM;
    if(getaddrinfo(host.c_str(),nullptr,&hints,&res)!=0||!res){ closesocket(s); return INVALID_SOCKET; }
    struct sockaddr_in addr{}; addr.sin_family=AF_INET; addr.sin_port=htons(port);
    addr.sin_addr=((struct sockaddr_in*)res->ai_addr)->sin_addr;
    freeaddrinfo(res);
#ifdef _WIN32
    DWORD tv=timeoutMs; setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,(const char*)&tv,sizeof(tv)); setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(const char*)&tv,sizeof(tv));
#else
    struct timeval tv; tv.tv_sec=timeoutMs/1000; tv.tv_usec=(timeoutMs%1000)*1000;
    setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv)); setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
#endif
    if(connect(s,(struct sockaddr*)&addr,sizeof(addr))==SOCKET_ERROR){ closesocket(s); return INVALID_SOCKET; }
    return s;
}

std::string recvAll(SOCKET s){
    std::string r; char buf[4096];
    while(true){ int n=recv(s,buf,sizeof(buf),0); if(n<=0) break; r.append(buf,n); }
    return r;
}

HttpResp httpPost(const std::string& url, const json& data, const std::map<std::string,std::string>& hdrs={}){
    // Parse URL
    std::string rest=url, scheme, host, path="/"; int port=80;
    auto p=rest.find("://"); if(p!=std::string::npos){ scheme=rest.substr(0,p); rest=rest.substr(p+3); }
    p=rest.find('/'); if(p!=std::string::npos){ path=rest.substr(p); rest=rest.substr(0,p); }
    p=rest.find(':'); if(p!=std::string::npos){ host=rest.substr(0,p); port=stoi(rest.substr(p+1)); }
    else { host=rest; port=(scheme=="https")?443:80; }

    std::string body=data.dump();
    std::string req="POST "+path+" HTTP/1.1\r\nHost:"+host+"\r\nContent-Type:application/json\r\nContent-Length:"+std::to_string(body.size())+"\r\nConnection:close\r\n";
    for(auto& [k,v]:hdrs) req+=k+":"+v+"\r\n";
    req+="\r\n"+body;

    SOCKET s=tcpConnect(host,port); if(s==INVALID_SOCKET) return {0,"connect failed",false};
    send(s,req.c_str(),(int)req.size(),0);
    std::string resp=recvAll(s); closesocket(s);

    HttpResp hr;
    auto bp=resp.find("\r\n\r\n"); if(bp==std::string::npos) return {0,"bad resp",false};
    hr.body=resp.substr(bp+4);
    auto sl=resp.substr(0,resp.find("\r\n")); auto sp=sl.find(' ');
    if(sp!=std::string::npos) hr.status=stoi(sl.substr(sp+1));
    hr.ok=(hr.status>=200&&hr.status<300);
    return hr;
}

// ═══════════════════ 配置 ═══════════════════
struct Config {
    // Panel connection
    std::string panelUrl="http://127.0.0.1:3000";
    std::string apiKey, registerCode, serverName, serverHost;

    // RCON
    int rconPort=21114;
    std::string rconPassword;

    // Paths
    std::string logDir, gameExePath, launchParams;
    bool autoRestart=false;

    // Server ports
    int webPort=18976;
    int unbanPort=18977;

    // TCP/timeouts
    int tcpConnectTimeoutMs=10000;
    int rconAuthTimeoutMs=5000;
    int rconExecTimeoutMs=10000;
    int rconRecvTimeoutMs=5000;

    // Polling intervals
    int logPollIntervalMs=300;
    int chatPollIntervalMs=100;
    int keepaliveIntervalSec=10;

    // Batch sender
    int rawFlushIntervalMs=200;
    int rawBatchSize=200;

    // Line dedup
    int lineDedupMaxSize=1000;

    // Restart logic
    int maxRestartAttempts=3;
    int restartCooldownSec=300;
    int restartBaseDelaySec=5;

    // HTTP server
    int httpRequestBufferSize=16384;
    int httpServerBacklog=5;

    // RCON reconnect
    bool rconReconnectEnabled=true;
    int rconReconnectIntervalSec=30;

    // Internal state (persisted)
    std::string _token;
    int _serverId=0;
    std::string _serverName, _rconHost;
    int _rconPort=0;
    std::string _rconPassword;

    json toJson() const {
        return {
            {"panelUrl",panelUrl},{"apiKey",apiKey},{"registerCode",registerCode},
            {"serverName",serverName},{"serverHost",serverHost},
            {"rconPort",rconPort},{"rconPassword",rconPassword},
            {"logDir",logDir},{"gameExePath",gameExePath},{"launchParams",launchParams},
            {"autoRestart",autoRestart},
            {"webPort",webPort},{"unbanPort",unbanPort},
            {"tcpConnectTimeoutMs",tcpConnectTimeoutMs},
            {"rconAuthTimeoutMs",rconAuthTimeoutMs},
            {"rconExecTimeoutMs",rconExecTimeoutMs},
            {"rconRecvTimeoutMs",rconRecvTimeoutMs},
            {"logPollIntervalMs",logPollIntervalMs},
            {"chatPollIntervalMs",chatPollIntervalMs},
            {"keepaliveIntervalSec",keepaliveIntervalSec},
            {"rawFlushIntervalMs",rawFlushIntervalMs},
            {"rawBatchSize",rawBatchSize},
            {"lineDedupMaxSize",lineDedupMaxSize},
            {"maxRestartAttempts",maxRestartAttempts},
            {"restartCooldownSec",restartCooldownSec},
            {"restartBaseDelaySec",restartBaseDelaySec},
            {"httpRequestBufferSize",httpRequestBufferSize},
            {"httpServerBacklog",httpServerBacklog},
            {"rconReconnectEnabled",rconReconnectEnabled},
            {"rconReconnectIntervalSec",rconReconnectIntervalSec},
            {"_token",_token},{"_serverId",_serverId},{"_serverName",_serverName},
            {"_rconHost",_rconHost},{"_rconPort",_rconPort},{"_rconPassword",_rconPassword}
        };
    }

    static Config fromJson(const json& j){
        Config c;
        if(j.contains("panelUrl"))c.panelUrl=j["panelUrl"];
        if(j.contains("apiKey"))c.apiKey=j["apiKey"];
        if(j.contains("registerCode"))c.registerCode=j["registerCode"];
        if(j.contains("serverName"))c.serverName=j["serverName"];
        if(j.contains("serverHost"))c.serverHost=j["serverHost"];
        if(j.contains("rconPort"))c.rconPort=j["rconPort"];
        if(j.contains("rconPassword"))c.rconPassword=j["rconPassword"];
        if(j.contains("logDir"))c.logDir=j["logDir"];
        if(j.contains("gameExePath"))c.gameExePath=j["gameExePath"];
        if(j.contains("launchParams"))c.launchParams=j["launchParams"];
        if(j.contains("autoRestart"))c.autoRestart=j["autoRestart"];
        if(j.contains("webPort"))c.webPort=j["webPort"];
        if(j.contains("unbanPort"))c.unbanPort=j["unbanPort"];
        if(j.contains("tcpConnectTimeoutMs"))c.tcpConnectTimeoutMs=j["tcpConnectTimeoutMs"];
        if(j.contains("rconAuthTimeoutMs"))c.rconAuthTimeoutMs=j["rconAuthTimeoutMs"];
        if(j.contains("rconExecTimeoutMs"))c.rconExecTimeoutMs=j["rconExecTimeoutMs"];
        if(j.contains("rconRecvTimeoutMs"))c.rconRecvTimeoutMs=j["rconRecvTimeoutMs"];
        if(j.contains("logPollIntervalMs"))c.logPollIntervalMs=j["logPollIntervalMs"];
        if(j.contains("chatPollIntervalMs"))c.chatPollIntervalMs=j["chatPollIntervalMs"];
        if(j.contains("keepaliveIntervalSec"))c.keepaliveIntervalSec=j["keepaliveIntervalSec"];
        if(j.contains("rawFlushIntervalMs"))c.rawFlushIntervalMs=j["rawFlushIntervalMs"];
        if(j.contains("rawBatchSize"))c.rawBatchSize=j["rawBatchSize"];
        if(j.contains("lineDedupMaxSize"))c.lineDedupMaxSize=j["lineDedupMaxSize"];
        if(j.contains("maxRestartAttempts"))c.maxRestartAttempts=j["maxRestartAttempts"];
        if(j.contains("restartCooldownSec"))c.restartCooldownSec=j["restartCooldownSec"];
        if(j.contains("restartBaseDelaySec"))c.restartBaseDelaySec=j["restartBaseDelaySec"];
        if(j.contains("httpRequestBufferSize"))c.httpRequestBufferSize=j["httpRequestBufferSize"];
        if(j.contains("httpServerBacklog"))c.httpServerBacklog=j["httpServerBacklog"];
        if(j.contains("rconReconnectEnabled"))c.rconReconnectEnabled=j["rconReconnectEnabled"];
        if(j.contains("rconReconnectIntervalSec"))c.rconReconnectIntervalSec=j["rconReconnectIntervalSec"];
        if(j.contains("_token"))c._token=j["_token"];
        if(j.contains("_serverId"))c._serverId=j["_serverId"];
        if(j.contains("_serverName"))c._serverName=j["_serverName"];
        if(j.contains("_rconHost"))c._rconHost=j["_rconHost"];
        if(j.contains("_rconPort"))c._rconPort=j["_rconPort"];
        if(j.contains("_rconPassword"))c._rconPassword=j["_rconPassword"];
        return c;
    }
};

static std::string g_cfgPath;
Config g_cfg;
std::mutex g_cfgMtx;
Config loadCfg(){ auto d=readFile(g_cfgPath); if(d.empty()) return Config(); try{return Config::fromJson(json::parse(d));}catch(...){return Config();} }
void saveCfg(const Config& c){ writeFile(g_cfgPath,c.toJson().dump(2)); }

// ═══════════════════ 行去重 ═══════════════════
class LineDedup {
    std::unordered_map<std::string,int64_t> seen_; size_t max_;
public:
    explicit LineDedup(size_t m=1000):max_(m){}
    bool markAndSend(const std::string& l){ if(l.size()<10)return false; auto fp=l.substr(0,50)+"|"+std::to_string(l.size()); if(seen_.count(fp))return false; seen_[fp]=time(nullptr); clean(); return true; }
    bool checkAndSup(const std::string& l){ return markAndSend(l); }
    void clean(){ if(seen_.size()<=max_)return; size_t t=max_/2; auto it=seen_.begin(); while(seen_.size()>t&&it!=seen_.end()) it=seen_.erase(it); }
    void setMax(size_t m){ max_=m; }
};

// ═══════════════════ 批量日志发送 ═══════════════════
class RawSender {
    std::string url_, key_; int sid_; std::vector<std::string> buf_; std::mutex m_; std::thread t_; std::atomic<bool> run_{false}; std::atomic<int> sent_{0};
    int flushIntervalMs_;
    int batchSize_;
public:
    RawSender(const std::string& u,const std::string& k,int s,int flushMs=200,int batchSz=200)
        :url_(u+"/api/events/raw"),key_(k),sid_(s),flushIntervalMs_(flushMs),batchSize_(batchSz){}
    void add(const std::string& l){ if(l.size()<10)return; std::lock_guard<std::mutex> lk(m_); buf_.push_back(l); }
    void start(){ run_=true; t_=std::thread([this](){ while(run_){ std::this_thread::sleep_for(std::chrono::milliseconds(flushIntervalMs_)); flush(); } flush(); }); }
    void stop(){ run_=false; if(t_.joinable())t_.join(); }
    int total()const{return sent_;}
private:
    void flush(){
        std::vector<std::string> b; { std::lock_guard<std::mutex> lk(m_); if(buf_.empty())return; size_t n=std::min(buf_.size(),(size_t)batchSize_); b.assign(buf_.begin(),buf_.begin()+n); buf_.erase(buf_.begin(),buf_.begin()+n); }
        auto r=httpPost(url_,{{"serverId",sid_},{"lines",b}},{{"X-API-Key",key_}});
        if(r.ok) sent_+=b.size(); else logErr("RawSender","send failed: "+r.body);
    }
};

// ═══════════════════ RCON ═══════════════════
class Rcon {
    SOCKET s_=INVALID_SOCKET; bool ok_=false; int id_=1; std::string host_; int port_; std::string pass_; std::mutex m_;
    int authTimeoutMs_;
    int execTimeoutMs_;
    int recvTimeoutMs_;
    bool reconnectEnabled_;
    int reconnectIntervalSec_;
    std::atomic<bool> reconnecting_{false};
    std::chrono::steady_clock::time_point lastReconnectAttempt_;
    static int readI32(const char* p){ return (unsigned char)p[0]|((unsigned char)p[1]<<8)|((unsigned char)p[2]<<16)|((unsigned char)p[3]<<24); }
    static void writeI32(char* p,int v){ p[0]=v&0xFF; p[1]=(v>>8)&0xFF; p[2]=(v>>16)&0xFF; p[3]=(v>>24)&0xFF; }
public:
    std::function<void(const std::string&,const std::string&,const std::string&,const std::string&)> onChat;

    Rcon(const std::string& h,int p,const std::string& pw,
         int authTimeoutMs=5000,int execTimeoutMs=10000,int recvTimeoutMs=5000,
         bool reconnectEnabled=true,int reconnectIntervalSec=30)
        :host_(h),port_(p),pass_(pw),
         authTimeoutMs_(authTimeoutMs),execTimeoutMs_(execTimeoutMs),recvTimeoutMs_(recvTimeoutMs),
         reconnectEnabled_(reconnectEnabled),reconnectIntervalSec_(reconnectIntervalSec),
         lastReconnectAttempt_(std::chrono::steady_clock::now() - std::chrono::seconds(60)){}

    bool isOk()const{return ok_;}

    bool connect(){
        s_=tcpConnect(host_,port_,authTimeoutMs_); if(s_==INVALID_SOCKET) return false;
        // Auth
        int aid=id_++; auto pk=pack(3,aid,pass_);
        send(s_,pk.data(),(int)pk.size(),0);
        char hdr[12]; int n=recvExact(hdr,12,authTimeoutMs_); if(n<12){closesocket(s_);s_=INVALID_SOCKET;return false;}
        int rid=readI32(hdr+4);
        if(rid==-1){closesocket(s_);s_=INVALID_SOCKET;logErr("RCON","Auth failed");return false;}
        ok_=true; log("RCON","Connected "+host_+":"+std::to_string(port_)); return true;
    }

    void disconnect(){ ok_=false; if(s_!=INVALID_SOCKET){closesocket(s_);s_=INVALID_SOCKET;} }

    bool reconnect(){
        if(reconnecting_.exchange(true)) return ok_; // already reconnecting
        log("RCON","Attempting reconnect to "+host_+":"+std::to_string(port_));
        disconnect();
        bool result = connect();
        reconnecting_ = false;
        lastReconnectAttempt_ = std::chrono::steady_clock::now();
        if(result){
            log("RCON","Reconnect successful");
        } else {
            logErr("RCON","Reconnect failed");
        }
        return result;
    }

    bool shouldAttemptReconnect() const {
        if(!reconnectEnabled_ || ok_) return false;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastReconnectAttempt_).count();
        return elapsed >= reconnectIntervalSec_;
    }

    std::string exec(const std::string& cmd){
        // If not connected and reconnect is enabled, try to reconnect first
        if(!ok_ && reconnectEnabled_ && shouldAttemptReconnect()){
            reconnect();
        }
        if(!ok_)return "";
        std::lock_guard<std::mutex> lk(m_);
        int mid=id_++; auto pk=pack(2,mid,cmd);
        if(send(s_,pk.data(),(int)pk.size(),0)==SOCKET_ERROR){ok_=false;return "";}
        char hdr[12]; int n=recvExact(hdr,12,execTimeoutMs_); if(n<12)return "";
        int sz=readI32(hdr); if(sz<4)return "";
        std::string body(sz,'\0'); n=recvExact(&body[0],sz,recvTimeoutMs_); if(n<sz)return "";
        if(body.size()>8) return body.substr(8,body.size()-10); // skip id+type, trim nulls
        return "";
    }

    void pollChat(){
        if(!ok_||s_==INVALID_SOCKET)return;
#ifdef _WIN32
        u_long mode=1; ioctlsocket(s_,FIONBIO,&mode);
#else
        fcntl(s_,F_SETFL,fcntl(s_,F_GETFL,0)|O_NONBLOCK);
#endif
        char buf[4096]; int n=recv(s_,buf,sizeof(buf),0);
#ifdef _WIN32
        mode=0; ioctlsocket(s_,FIONBIO,&mode);
#else
        fcntl(s_,F_SETFL,fcntl(s_,F_GETFL,0)&~O_NONBLOCK);
#endif
        if(n<12)return;
        int type=readI32(buf+8);
        if(type==1&&onChat){
            std::string body(buf+12,n-14);
            // Parse: [ChatType] [Online Ids:...Steam:xxx] Name : msg
            auto c1=body.find(']'); if(c1==std::string::npos)return;
            std::string ct=body.substr(1,c1-1);
            auto c2=body.find(']',c1+1); if(c2==std::string::npos)return;
            std::string ids=body.substr(c1+2,c2-c1-2);
            std::string sid; auto sp=body.find("Steam:"); if(sp!=std::string::npos){sid=body.substr(sp+6);auto e=sid.find(' ');if(e!=std::string::npos)sid=sid.substr(0,e);}
            auto co=body.find(" : ",c2); if(co==std::string::npos)return;
            onChat(ct,trim(body.substr(c2+2,co-c2-2)),body.substr(co+3),sid);
        }
    }

private:
    std::string pack(int type,int id,const std::string& body){
        int sz=4+4+(int)body.size()+2;
        std::string p(sz+4,'\0');
        writeI32(&p[0],sz); writeI32(&p[4],id); writeI32(&p[8],type);
        memcpy(&p[12],body.data(),body.size());
        return p;
    }
    int recvExact(char* buf,int len,int timeoutMs){
        int total=0; auto start=std::chrono::steady_clock::now();
        while(total<len){ int n=recv(s_,buf+total,len-total,0); if(n<=0){auto el=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count(); if(el>timeoutMs)break; std::this_thread::sleep_for(10ms); continue;} total+=n; }
        return total;
    }
};

// ═══════════════════ LogTail ═══════════════════

// GBK -> UTF-8 conversion (Squad logs on Windows are GBK encoded)
std::string gbkToUtf8(const std::string& gbk){
#ifdef _WIN32
    return gbk; // On Windows, native encoding matches
#else
    if(gbk.empty()) return gbk;
    iconv_t cd = iconv_open("UTF-8", "GBK");
    if(cd == (iconv_t)-1) return gbk; // fallback: return as-is
    size_t inbytes = gbk.size();
    size_t outbytes = inbytes * 4; // UTF-8 max 4 bytes per char
    std::string out(outbytes, '\0');
    char* inbuf = const_cast<char*>(gbk.data());
    char* outbuf = &out[0];
    size_t inleft = inbytes, outleft = outbytes;
    iconv(cd, &inbuf, &inleft, &outbuf, &outleft);
    iconv_close(cd);
    out.resize(outbytes - outleft);
    return out;
#endif
}

class LogTail {
    std::string dir_, file_; long long pos_=0; std::function<void(const std::string&)> cb_; std::thread t_; std::atomic<bool> run_{false}; int cnt_=0;
    int pollIntervalMs_;
public:
    explicit LogTail(const std::string& d,int pollMs=300):dir_(d),pollIntervalMs_(pollMs){}
    void onLine(std::function<void(const std::string&)> f){cb_=f;}
    void start(){ findLog(); if(file_.empty()){logErr("LogTail","No log in "+dir_);return;} std::ifstream f(file_,std::ios::binary|std::ios::ate); if(f)pos_=f.tellg(); log("LogTail","Tailing "+file_+" from "+std::to_string(pos_)); run_=true; t_=std::thread([this](){while(run_){std::this_thread::sleep_for(std::chrono::milliseconds(pollIntervalMs_));poll();}});}
    void stop(){run_=false;if(t_.joinable())t_.join();}
private:
    void findLog(){
#ifdef _WIN32
        WIN32_FIND_DATAA fd; HANDLE h=FindFirstFileA((dir_+"\\*.log").c_str(),&fd);
        if(h==INVALID_HANDLE_VALUE){file_.clear();return;}
        std::string active; std::vector<std::pair<std::string,FILETIME>> fs;
        do{ std::string n=fd.cFileName; std::transform(n.begin(),n.end(),n.begin(),::tolower); if(n=="squadgame.log") active=dir_+"\\"+fd.cFileName; fs.push_back({dir_+"\\"+fd.cFileName,fd.ftLastWriteTime}); }while(FindNextFileA(h,&fd));
        FindClose(h);
        if(!active.empty()){file_=active;return;}
        std::sort(fs.begin(),fs.end(),[](auto&a,auto&b){return CompareFileTime(&a.second,&b.second)>0;});
        if(!fs.empty())file_=fs[0].first;
#else
        FILE* p=popen(("ls -t "+dir_+"/*.log 2>/dev/null|head -1").c_str(),"r");
        if(p){char b[512];if(fgets(b,sizeof(b),p))file_=trim(b);pclose(p);}
#endif
    }
    void poll(){
        if(++cnt_%30==0&&!file_.empty()){
            std::string n=baseName(file_); std::transform(n.begin(),n.end(),n.begin(),::tolower);
            if(n!="squadgame.log"){auto c=dir_+"\\SquadGame.log";if(fileExists(c)){log("LogTail","Rotation->SquadGame.log");file_=c;pos_=0;}}
        }
        if(file_.empty()){findLog();if(!file_.empty()){std::ifstream f(file_,std::ios::binary|std::ios::ate);if(f)pos_=f.tellg();log("LogTail","Found "+file_);}return;}
        if(!fileExists(file_)){file_.clear();findLog();if(!file_.empty())pos_=0;return;}
        std::ifstream f(file_,std::ios::binary|std::ios::ate); if(!f)return;
        auto sz=f.tellg(); if(sz<=pos_){if(sz<pos_){pos_=0;findLog();}return;}
        f.seekg(pos_); std::string buf(sz-pos_,'\0'); f.read(&buf[0],sz-pos_); pos_=sz;
        size_t st=0; for(size_t i=0;i<buf.size();i++){if(buf[i]=='\n'){auto ln=buf.substr(st,i-st);if(!ln.empty()&&ln.back()=='\r')ln.pop_back();if(!ln.empty()&&cb_)cb_(gbkToUtf8(ln));st=i+1;}}
        if(st<buf.size()){auto ln=buf.substr(st);if(!ln.empty()&&ln.back()=='\r')ln.pop_back();if(!ln.empty()&&cb_)cb_(gbkToUtf8(ln));}
    }
};

// ═══════════════════ 游戏进程管理 ═══════════════════
class GameSvr {
    std::string exe_, params_; bool autoR_=false;
#ifdef _WIN32
    PROCESS_INFORMATION pi_{}; HANDLE hRead_=NULL;
#endif
    std::atomic<bool> run_{false}, stop_{false}; std::thread outT_, monT_; std::string st_="stopped"; int rc_=0; time_t lct_=0, startT_=0;
    int maxRestartAttempts_=3;
    int restartCooldownSec_=300;
    int restartBaseDelaySec_=5;
public:
    std::function<void(const std::string&)> onLine;
    std::function<void(const std::string&)> onStatus;
    void config(const std::string& e,const std::string& p,bool a,int maxRst=3,int cooldown=300,int baseDelay=5){
        exe_=e;params_=p;autoR_=a;maxRestartAttempts_=maxRst;restartCooldownSec_=cooldown;restartBaseDelaySec_=baseDelay;
    }
    std::string status()const{return st_;}
    bool running()const{return run_;}
    int pid()const{ return 0; /* simplified for cross-compile */ }
    time_t startTime()const{return startT_;}
    bool start(){
        if(run_||st_=="starting")return false;
        if(exe_.empty()||!fileExists(exe_)){logErr("GameSvr","Exe not found");return false;}
        setStatus("starting");
#ifdef _WIN32
        SECURITY_ATTRIBUTES sa{sizeof(sa),NULL,TRUE};
        HANDLE hWrite; if(!CreatePipe(&hRead_,&hWrite,&sa,0)){setStatus("crashed");return false;}
        SetHandleInformation(hRead_,HANDLE_FLAG_INHERIT,0);
        STARTUPINFOA si{}; si.cb=sizeof(si); si.hStdOutput=hWrite; si.hStdError=hWrite; si.dwFlags=STARTF_USESTDHANDLES;
        std::string cmd="\""+exe_+"\" "+params_;
        std::vector<char> cb(cmd.begin(),cmd.end()); cb.push_back(0);
        std::string wd=dirOf(exe_);
        if(!CreateProcessA(NULL,cb.data(),NULL,NULL,TRUE,CREATE_NO_WINDOW,NULL,wd.c_str(),&si,&pi_)){
            logErr("GameSvr","CreateProcess failed"); CloseHandle(hRead_); CloseHandle(hWrite); setStatus("crashed"); return false;
        }
        CloseHandle(hWrite);
#endif
        run_=true; stop_=false; startT_=time(nullptr); setStatus("running");
        outT_=std::thread([this](){
            char b[4096];
#ifdef _WIN32
            DWORD n; while(run_){if(!ReadFile(hRead_,b,sizeof(b)-1,&n,NULL)||n==0)break;b[n]=0;std::string d(b,n);size_t s=0;for(size_t i=0;i<d.size();i++){if(d[i]=='\n'){auto l=d.substr(s,i-s);if(!l.empty()&&l.back()=='\r')l.pop_back();if(!l.empty()&&onLine)onLine(l);s=i+1;}}if(s<d.size()){auto l=d.substr(s);if(!l.empty()&&l.back()=='\r')l.pop_back();if(!l.empty()&&onLine)onLine(l);}}
            CloseHandle(hRead_); hRead_=NULL;
#endif
        });
        monT_=std::thread([this](){
#ifdef _WIN32
            WaitForSingleObject(pi_.hProcess,INFINITE); DWORD ec; GetExitCodeProcess(pi_.hProcess,&ec);
            CloseHandle(pi_.hProcess); CloseHandle(pi_.hThread); pi_.hProcess=NULL; pi_.hThread=NULL;
#endif
            run_=false; if(outT_.joinable())outT_.join();
            if(stop_){setStatus("stopped");} else {setStatus("crashed"); if(autoR_)tryRestart();}
        });
        return true;
    }
    void stop(){ if(!run_)return; stop_=true;
#ifdef _WIN32
        if(pi_.hProcess)TerminateProcess(pi_.hProcess,0);
#endif
    }
    void restart(){stop();std::this_thread::sleep_for(2s);start();}
private:
    void setStatus(const std::string& s){st_=s;if(onStatus)onStatus(s);}
    void tryRestart(){
        time_t n=time(nullptr);
        if(lct_&&n-lct_>restartCooldownSec_) rc_=0;
        lct_=n;
        if(++rc_>maxRestartAttempts_){logErr("GameSvr","Max restarts ("+std::to_string(maxRestartAttempts_)+") reached");return;}
        int d=restartBaseDelaySec_*rc_;
        setStatus("restarting");
        std::this_thread::sleep_for(std::chrono::seconds(d));
        start();
    }
};
GameSvr g_game;

// Forward declarations
bool startRelay();
void startUnbanSvr();

std::vector<std::string> splitLines(const std::string& s);
// ═══════════════════ Ban/Admin 操作 ═══════════════════
std::string findReservedSlots(){
#ifdef _WIN32
    for(auto& b:{dirOf(dirOf(g_cfg.logDir))+std::string("\\ServerConfig\\ReservedSlots.cfg"),std::string("C:\\Servers\\squad\\SquadGame\\ServerConfig\\ReservedSlots.cfg"),std::string("C:\\SquadServer\\SquadGame\\ServerConfig\\ReservedSlots.cfg"),std::string("D:\\Servers\\squad\\SquadGame\\ServerConfig\\ReservedSlots.cfg")})
#else
    for(auto& b:{dirOf(dirOf(g_cfg.logDir))+"/ServerConfig/ReservedSlots.cfg",std::string("/opt/squad/SquadGame/ServerConfig/ReservedSlots.cfg")})
#endif
        if(fileExists(b)) return b;
    return "";
}

std::string findBanList(){
#ifdef _WIN32
    for(auto& b:{dirOf(dirOf(g_cfg.logDir))+std::string("\\ServerConfig\\BanList.cfg"),std::string("C:\\Servers\\squad\\SquadGame\\ServerConfig\\BanList.cfg"),std::string("C:\\SquadServer\\SquadGame\\ServerConfig\\BanList.cfg"),std::string("D:\\Servers\\squad\\SquadGame\\ServerConfig\\BanList.cfg")})
#else
    for(auto& b:{dirOf(dirOf(g_cfg.logDir))+"/ServerConfig/BanList.cfg",std::string("/opt/squad/SquadGame/ServerConfig/BanList.cfg")})
#endif
        if(fileExists(b)) return b;
    return "";
}
std::string findAdmins(){
#ifdef _WIN32
    for(auto& b:{dirOf(dirOf(g_cfg.logDir))+std::string("\\ServerConfig\\Admins.cfg"),std::string("C:\\Servers\\squad\\SquadGame\\ServerConfig\\Admins.cfg"),std::string("C:\\SquadServer\\SquadGame\\ServerConfig\\Admins.cfg"),std::string("D:\\Servers\\squad\\SquadGame\\ServerConfig\\Admins.cfg")})
#else
    for(auto& b:{dirOf(dirOf(g_cfg.logDir))+"/ServerConfig/Admins.cfg",std::string("/opt/squad/SquadGame/ServerConfig/Admins.cfg")})
#endif
        if(fileExists(b)) return b;
    return "";
}
bool unban(const std::string& sid){
    auto p=findBanList(); if(p.empty())throw std::runtime_error("找不到BanList.cfg");
    auto c=readFile(p); bool found=false; std::string nc;
    for(auto& l:splitLines(c)){auto t=trim(l);if(t.empty()||t[0]=='#'){nc+=l+"\r\n";continue;}auto cp=t.find(':');if(cp!=std::string::npos&&t.substr(0,cp)==sid){found=true;continue;}nc+=l+"\r\n";}
    if(!found)throw std::runtime_error("未找到"+sid); writeFile(p,nc); log("Unban","Removed "+sid); return true;
}
struct SyncResult{bool success=false,already=false,notFound=false;};
SyncResult syncAdmin(const std::string& sid,const std::string& act,const std::string& lv){
    auto p=findAdmins(); if(p.empty())throw std::runtime_error("找不到Admins.cfg");
    std::string grp=(lv=="server_owner")?"Admin":"Moderator", perm=(lv=="server_owner")?"cankickcanban":"canbalancecanrestart";
    auto c=readFile(p); bool found=false; std::string nc;
    for(auto& l:splitLines(c)){auto t=trim(l);if(t.empty()||t[0]=='#'){nc+=l+"\r\n";continue;}auto cp=t.find(':');if(cp!=std::string::npos){auto r=t.substr(cp+1);auto c2=r.find(':');std::string s=(c2!=std::string::npos)?r.substr(0,c2):r;if(s==sid){found=true;if(act=="add")nc+=l+"\r\n";continue;}}nc+=l+"\r\n";}
    SyncResult r; if(act=="add"&&!found){nc+="Admin:"+sid+":"+grp+":"+perm+"\r\n";r.success=true;}else if(act=="add"&&found)r.already=true;else if(act=="remove"&&found)r.success=true;else r.notFound=true;
    writeFile(p,nc); return r;
}
// splitLines defined here for ban/admin use
std::vector<std::string> splitLines(const std::string& s){std::vector<std::string> v;std::istringstream ss(s);std::string l;while(std::getline(ss,l)){if(!l.empty()&&l.back()=='\r')l.pop_back();v.push_back(l);}return v;}

// ═══════════════════ 检测 ═══════════════════
std::string detectLog(){
    for(auto& c:{"C:\\Servers\\squad\\SquadGame\\Saved\\Logs","C:\\SquadServer\\SquadGame\\Saved\\Logs","C:\\Squad\\SquadGame\\Saved\\Logs","D:\\Servers\\squad\\SquadGame\\Saved\\Logs","D:\\SquadServer\\SquadGame\\Saved\\Logs","D:\\Squad\\SquadGame\\Saved\\Logs","C:\\steamcmd\\squad_server\\SquadGame\\Saved\\Logs","D:\\steamcmd\\squad_server\\SquadGame\\Saved\\Logs"}){
#ifdef _WIN32
        WIN32_FIND_DATAA fd; HANDLE h=FindFirstFileA((std::string(c)+"\\*.log").c_str(),&fd); if(h!=INVALID_HANDLE_VALUE){FindClose(h);return c;}
#else
        if(fileExists(std::string(c)+"/."))return c;
#endif
    }
    return "";
}
std::vector<std::string> detectExe(){
    std::vector<std::string> f;
    for(auto& p:{"C:\\SquadServer\\Binaries\\Win64\\SquadServer.exe","D:\\SquadServer\\Binaries\\Win64\\SquadServer.exe","C:\\Servers\\squad\\Binaries\\Win64\\SquadServer.exe","D:\\Servers\\squad\\Binaries\\Win64\\SquadServer.exe","C:\\steamcmd\\squad_server\\Binaries\\Win64\\SquadServer.exe","D:\\steamcmd\\squad_server\\Binaries\\Win64\\SquadServer.exe"})
        if(fileExists(p))f.push_back(p);
    return f;
}

// ═══════════════════ 全局状态 ═══════════════════
struct Status {
    std::atomic<bool> panelOk{false}, rconOk{false};
    std::string logDir, lastEvt, startT;
    std::atomic<int> sent{0};
    std::vector<std::string> errs; std::mutex m_;
    void addErr(const std::string& e){std::lock_guard<std::mutex>lk(m_);errs.push_back(nowISO()+" "+e);if(errs.size()>50)errs.erase(errs.begin());}
};
Status g_st;
std::unique_ptr<RawSender> g_sender;
std::unique_ptr<Rcon> g_rcon;
std::unique_ptr<LogTail> g_tail;
std::thread g_rconT, g_keepT;
std::atomic<bool> g_running{false};
SOCKET g_srvSocket = INVALID_SOCKET;   // httpServer listen socket
SOCKET g_unbanSocket = INVALID_SOCKET; // unbanSvr listen socket
std::thread g_startupThread;           // relay startup thread (for clean shutdown)
std::thread g_restartThread;           // game restart thread (tracked, not detached)
std::mutex g_restartMtx;               // protects restart thread lifecycle
std::mutex g_startupMtx;               // protects startup/shutdown sequencing
static std::atomic<bool> g_unbanRunning{false};

// ═══════════════════ HTTP 服务器（极简）══════════════════
// 轻量 HTTP 响应发送
void httpReply(SOCKET client, int code, const std::string& contentType, const std::string& body){
    std::string r="HTTP/1.1 "+std::to_string(code)+(code==200?" OK":" Error")+"\r\nContent-Type:"+contentType+"\r\nContent-Length:"+std::to_string(body.size())+"\r\nConnection:close\r\nAccess-Control-Allow-Origin:*\r\nAccess-Control-Allow-Methods:GET,POST,OPTIONS\r\nAccess-Control-Allow-Headers:Content-Type\r\n\r\n"+body;
    send(client,r.c_str(),(int)r.size(),0);
}

// 解析 HTTP 请求
struct HttpRequest { std::string method, path, body; };
bool parseReq(const std::string& raw, HttpRequest& req){
    auto hp=raw.find("\r\n\r\n"); if(hp==std::string::npos)return false;
    req.body=raw.substr(hp+4);
    auto sl=raw.substr(0,raw.find("\r\n"));
    auto sp1=sl.find(' '); if(sp1==std::string::npos)return false;
    req.method=sl.substr(0,sp1);
    auto sp2=sl.find(' ',sp1+1); if(sp2==std::string::npos)return false;
    req.path=sl.substr(sp1+1,sp2-sp1-1);
    return true;
}

// 处理请求
void handleReq(SOCKET client, const HttpRequest& req){
    // CORS preflight
    if(req.method=="OPTIONS"){ httpReply(client,200,"text/plain",""); return; }

    // GET / (Web UI)
    if(req.method=="GET" && req.path=="/"){
        httpReply(client,200,"text/html; charset=utf-8",WEB_HTML); return;
    }

    // GET /api/config
    if(req.method=="GET" && req.path=="/api/config"){
        Config c=g_cfg; json j={{"config",c.toJson()},{"detectedLogDir",detectLog()}};
        httpReply(client,200,"application/json",j.dump()); return;
    }

    // POST /api/config
    if(req.method=="POST" && req.path=="/api/config"){
        try{
            auto body=json::parse(req.body);
            Config cfg=Config::fromJson(body);
            if(!cfg.registerCode.empty()&&cfg.apiKey.empty()){
                auto r=httpPost(cfg.panelUrl+"/api/relay/register",{{"registerCode",cfg.registerCode},{"name",cfg.serverName.empty()?"Squad Server":cfg.serverName},{"host",cfg.serverHost},{"rconPort",cfg.rconPort},{"rconPassword",cfg.rconPassword}});
                if(!r.ok){httpReply(client,400,"application/json",json{{"error","注册失败:"+r.body}}.dump());return;}
                cfg.apiKey=json::parse(r.body)["apiKey"];
            }
            if(cfg.apiKey.empty()){httpReply(client,400,"application/json",json{{"error","注册码或API Key必填"}}.dump());return;}
            auto ar=httpPost(cfg.panelUrl+"/api/relay/auth",{{"apiKey",cfg.apiKey}});
            if(!ar.ok&&!cfg.registerCode.empty()){
                auto r2=httpPost(cfg.panelUrl+"/api/relay/register",{{"registerCode",cfg.registerCode},{"name",cfg.serverName},{"host",cfg.serverHost},{"rconPort",cfg.rconPort},{"rconPassword",cfg.rconPassword}});
                if(r2.ok){cfg.apiKey=json::parse(r2.body)["apiKey"];ar=httpPost(cfg.panelUrl+"/api/relay/auth",{{"apiKey",cfg.apiKey}});}
            }
            if(!ar.ok){httpReply(client,400,"application/json",json{{"error","认证失败:"+ar.body}}.dump());return;}
            auto aj=json::parse(ar.body); cfg._token=aj["token"]; cfg._serverId=aj["serverId"]; cfg._serverName=aj.value("serverName","");
            if(aj.contains("rcon")){cfg._rconHost=aj["rcon"].value("host","");cfg._rconPort=aj["rcon"].value("port",0);cfg._rconPassword=aj["rcon"].value("password","");}
            {std::lock_guard<std::mutex>lk(g_cfgMtx);g_cfg=cfg;saveCfg(cfg);}
            httpReply(client,200,"application/json",json{{"ok",true},{"serverName",cfg._serverName},{"serverId",cfg._serverId}}.dump());
            // Start relay in a tracked thread (not detached)
            {
                std::lock_guard<std::mutex> lk(g_startupMtx);
                if (g_startupThread.joinable()) g_startupThread.join(); // wait for previous startup
                g_startupThread = std::thread([](){
                    std::this_thread::sleep_for(500ms);
                    if (!g_running) return; // shutdown happened before we could start
                    startRelay();
                    if (g_running) startUnbanSvr();
                });
            }
        }catch(std::exception&e){httpReply(client,400,"application/json",json{{"error",e.what()}}.dump());}
        return;
    }

    // GET /api/status
    if(req.method=="GET" && req.path=="/api/status"){
        std::vector<std::string> errs;{std::lock_guard<std::mutex>lk(g_st.m_);errs=g_st.errs;}
        std::string gameExePath; {std::lock_guard<std::mutex> lk(g_cfgMtx); gameExePath=g_cfg.gameExePath;}
        json gs=nullptr; if(!gameExePath.empty()) gs={{"status",g_game.status()},{"pid",g_game.pid()},{"uptime",g_game.running()?(int)(time(nullptr)-g_game.startTime()):0}};
        json j={{{"relay",{{"panelOk",g_st.panelOk.load()},{"rconOk",g_st.rconOk.load()},{"logDir",g_st.logDir},{"eventsSent",g_st.sent.load()},{"lastEvent",g_st.lastEvt},{"errors",errs},{"startTime",g_st.startT}}},{"gameServer",gs},{"hasGameServerConfig",!gameExePath.empty()}}};
        httpReply(client,200,"application/json",j.dump()); return;
    }

    // GET /api/detect-log
    if(req.method=="GET" && req.path=="/api/detect-log"){
        httpReply(client,200,"application/json",json{{"detected",detectLog()}}.dump()); return;
    }

    // GET /api/detect-exe
    if(req.method=="GET" && req.path=="/api/detect-exe"){
        httpReply(client,200,"application/json",json{{"detected",detectExe()}}.dump()); return;
    }

    // POST /api/server/start|stop|restart
    if(req.path=="/api/server/start"&&req.method=="POST"){ g_game.start()?httpReply(client,200,"application/json",json{{"ok",true}}.dump()):httpReply(client,500,"application/json",json{{"error","start failed"}}.dump()); return; }
    if(req.path=="/api/server/stop"&&req.method=="POST"){ g_game.stop(); httpReply(client,200,"application/json",json{{"ok",true}}.dump()); return; }
    if(req.path=="/api/server/restart"&&req.method=="POST"){
        // Use tracked thread instead of detached
        {
            std::lock_guard<std::mutex> lk(g_restartMtx);
            if (g_restartThread.joinable()) g_restartThread.join(); // wait for previous restart
            g_restartThread = std::thread([](){ g_game.restart(); });
        }
        httpReply(client,200,"application/json",json{{"ok",true}}.dump());
        return;
    }


    // POST /api/admin-sync
    if(req.method=="POST"&&req.path=="/api/admin-sync"){
        try{auto j=json::parse(req.body);
            std::string sid=j.value("steamId",""), act=j.value("action",""), lv=j.value("level","");
            if(sid.empty()||act.empty()){httpReply(client,400,"application/json",json{{"error","steamId and action required"}}.dump());return;}
            auto r=syncAdmin(sid,act,lv);
            httpReply(client,200,"application/json",json{{"success",r.success},{"already",r.already},{"notFound",r.notFound}}.dump());
        }catch(std::exception&e){httpReply(client,500,"application/json",json{{"error",e.what()}}.dump());}
        return;
    }
    
    // POST /api/reserved-slots/sync
    if(req.method=="POST"&&req.path=="/api/reserved-slots/sync"){
        try{auto j=json::parse(req.body);
            std::string action=j.value("action",""); // "add" or "remove"
            std::string steamId=j.value("steamId","");
            std::string playerName=j.value("playerName","");
            if(action.empty()||steamId.empty()){httpReply(client,400,"application/json","{\"error\":\"action and steamId required\"}");return;}
            auto p=findReservedSlots();
            if(p.empty()){
                // Create the file if it doesn't exist
                auto cfgDir=dirOf(dirOf(g_cfg.logDir))+"/ServerConfig";
#ifdef _WIN32
                cfgDir=dirOf(dirOf(g_cfg.logDir))+"\\ServerConfig";
#endif
                p=cfgDir+"/ReservedSlots.cfg";
            }
            auto c=readFile(p);
            auto lines=splitLines(c);
            std::string nc; bool found=false;
            for(auto& l:lines){auto t=trim(l);
                if(t.empty()||t[0]=='#'){nc+=l+"\r\n";continue;}
                if(t==steamId){found=true;if(action=="remove")continue;}
                nc+=l+"\r\n";
            }
            if(action=="add"&&!found){nc+=steamId+"\r\n";}
            writeFile(p,nc);
            log("ReservedSlots",action+" "+steamId);
            httpReply(client,200,"application/json","{\"success\":true}");
        }catch(std::exception&e){httpReply(client,500,"application/json",json{{"error",e.what()}}.dump());}
        return;
    }

    // GET /api/reserved-slots
    if(req.method=="GET"&&req.path=="/api/reserved-slots"){
        auto p=findReservedSlots();
        json slots=json::array();
        if(!p.empty()){
            auto c=readFile(p);
            for(auto& l:splitLines(c)){auto t=trim(l);if(!t.empty()&&t[0]!='#')slots.push_back(t);}
        }
        httpReply(client,200,"application/json",json{{"slots",slots}}.dump());
        return;
    }

httpReply(client,404,"application/json","{\"error\":\"not found\"}");
}

// ═══════════════════ 优雅关闭 ═══════════════════
void shutdownAll() {
    if (!g_running.exchange(false)) return; // already shutting down
    log("Main", "Shutting down...");
    // Close server sockets to unblock accept() — g_running is already false
    // so the accept() loop will exit after being unblocked
    if (g_srvSocket != INVALID_SOCKET) { closesocket(g_srvSocket); g_srvSocket = INVALID_SOCKET; }
    if (g_unbanSocket != INVALID_SOCKET) { closesocket(g_unbanSocket); g_unbanSocket = INVALID_SOCKET; }
    // Small delay to let blocked accept() wake up and check g_running
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // Wait for startup thread to finish
    {
        std::lock_guard<std::mutex> lk(g_startupMtx);
        if (g_startupThread.joinable()) g_startupThread.join();
    }
    // Wait for restart thread to finish
    {
        std::lock_guard<std::mutex> lk(g_restartMtx);
        if (g_restartThread.joinable()) g_restartThread.join();
    }
    // Stop background components
    if (g_sender) g_sender->stop();
    if (g_tail) g_tail->stop();
    g_game.stop();
    if (g_rconT.joinable()) g_rconT.join();
    if (g_keepT.joinable()) g_keepT.join();
    if (g_rcon) g_rcon->disconnect();
    g_unbanRunning = false;
    log("Main", "Shutdown complete");
}

#ifdef _WIN32
BOOL WINAPI consoleHandler(DWORD type) {
    if (type == CTRL_CLOSE_EVENT || type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) {
        log("Main", "Console signal received (type=" + std::to_string(type) + "), shutting down...");
        shutdownAll();
        // Give threads a moment to finish
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        return TRUE;
    }
    return FALSE;
}
#else
void signalHandler(int sig) {
    log("Main", "Signal " + std::to_string(sig) + " received, shutting down...");
    shutdownAll();
}
#endif

// HTTP 服务器线程
void httpServer(int port, int backlog, int bufferSize){
    g_srvSocket=socket(AF_INET,SOCK_STREAM,0); if(g_srvSocket==INVALID_SOCKET){logErr("HTTP","socket failed");return;}
    int opt=1; setsockopt(g_srvSocket,SOL_SOCKET,SO_REUSEADDR,(const char*)&opt,sizeof(opt));
    struct sockaddr_in addr{}; addr.sin_family=AF_INET; addr.sin_port=htons(port); addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(bind(g_srvSocket,(struct sockaddr*)&addr,sizeof(addr))==SOCKET_ERROR){logErr("HTTP","bind "+std::to_string(port)+" failed");closesocket(g_srvSocket);g_srvSocket=INVALID_SOCKET;return;}
    listen(g_srvSocket,backlog); log("HTTP","Listening on 127.0.0.1:"+std::to_string(port));
#ifdef _WIN32
    ShellExecuteA(NULL,"open",("http://localhost:"+std::to_string(port)).c_str(),NULL,NULL,SW_SHOWNORMAL);
#endif
    while(g_running){
        struct sockaddr_in ca; socklen_t cl=sizeof(ca);
        SOCKET c=accept(g_srvSocket,(struct sockaddr*)&ca,&cl);
        if(c==INVALID_SOCKET){break;}  // socket closed by shutdownAll, exit immediately
        // Capture bufferSize by value for the detached handler thread
        std::thread([c, bufferSize](){
            std::vector<char> buf(bufferSize);
            int n=recv(c,buf.data(),bufferSize-1,0); if(n>0){buf[n]=0; HttpRequest req; if(parseReq(std::string(buf.data(),n),req))handleReq(c,req);}
            closesocket(c);
        }).detach();
    }
    closesocket(g_srvSocket); g_srvSocket=INVALID_SOCKET;
    log("HTTP","Server stopped");
}

// 解封服务器线程
void unbanSvr(int port, int backlog){
    g_unbanSocket=socket(AF_INET,SOCK_STREAM,0); if(g_unbanSocket==INVALID_SOCKET)return;
    int opt=1; setsockopt(g_unbanSocket,SOL_SOCKET,SO_REUSEADDR,(const char*)&opt,sizeof(opt));
    struct sockaddr_in addr{}; addr.sin_family=AF_INET; addr.sin_port=htons(port); addr.sin_addr.s_addr=htonl(INADDR_ANY);
    if(bind(g_unbanSocket,(struct sockaddr*)&addr,sizeof(addr))==SOCKET_ERROR){closesocket(g_unbanSocket);g_unbanSocket=INVALID_SOCKET;return;}
    listen(g_unbanSocket,backlog); log("UnbanSvr","Listening on 0.0.0.0:"+std::to_string(port));
    while(g_running){
        struct sockaddr_in ca; socklen_t cl=sizeof(ca);
        SOCKET c=accept(g_unbanSocket,(struct sockaddr*)&ca,&cl);
        if(c==INVALID_SOCKET){break;}
        std::thread([c](){
            char buf[4096]; int n=recv(c,buf,sizeof(buf)-1,0); if(n<=0){closesocket(c);return;} buf[n]=0;
            HttpRequest req; if(!parseReq(buf,req)){closesocket(c);return;}
            // Auth check: require X-API-Key matching our apiKey
            std::string reqApiKey, storedApiKey;
            {std::lock_guard<std::mutex> lk(g_cfgMtx); storedApiKey=g_cfg.apiKey;}
            {std::string raw(buf,n);
            auto akPos=raw.find("X-API-Key:"); if(akPos!=std::string::npos){auto v=raw.substr(akPos+10);auto e=v.find("\r\n");if(e!=std::string::npos)v=v.substr(0,e);reqApiKey=trim(v);}}
            if(reqApiKey.empty()||reqApiKey!=storedApiKey){httpReply(c,401,"application/json","{\"error\":\"unauthorized\"}");closesocket(c);return;}
            if(req.method=="POST"&&req.path=="/api/unban"){
                try{auto j=json::parse(req.body); std::string sid=j.value("steamId",""); if(sid.empty()){httpReply(c,400,"application/json","{\"error\":\"steamId\"}");} else {unban(sid);httpReply(c,200,"application/json","{\"success\":true}");}}catch(std::exception&e){httpReply(c,500,"application/json",json{{"error",e.what()}}.dump());}
            } else if(req.method=="POST"&&req.path=="/api/admin-sync"){
                try{auto j=json::parse(req.body); std::string sid=j.value("steamId",""), act=j.value("action",""), lv=j.value("level",""); if(sid.empty()||act.empty()){httpReply(c,400,"application/json","{\"error\":\"steamId and action required\"}");} else {auto r=syncAdmin(sid,act,lv); httpReply(c,200,"application/json",json{{"success",r.success},{"already",r.already},{"notFound",r.notFound}}.dump());}}catch(std::exception&e){httpReply(c,500,"application/json",json{{"error",e.what()}}.dump());}
            } else if(req.method=="POST"&&req.path=="/api/reserved-slots/sync"){
                try{auto j=json::parse(req.body); std::string action=j.value("action",""), steamId=j.value("steamId",""), playerName=j.value("playerName",""); if(action.empty()||steamId.empty()){httpReply(c,400,"application/json","{\"error\":\"action and steamId required\"}");} else {auto p=findReservedSlots(); if(p.empty()){auto cfgDir=dirOf(dirOf(g_cfg.logDir))+"/ServerConfig"; p=cfgDir+"/ReservedSlots.cfg";} auto cc=readFile(p); auto lines=splitLines(cc); std::string nc; bool found=false; for(auto& l:lines){auto t=trim(l); if(t.empty()||t[0]=='#'){nc+=l+"\r\n";continue;} if(t==steamId){found=true;if(action=="remove")continue;} nc+=l+"\r\n";} if(action=="add"&&!found){nc+=steamId+"\r\n";} writeFile(p,nc); log("ReservedSlots",action+" "+steamId); httpReply(c,200,"application/json","{\"success\":true}");}}catch(std::exception&e){httpReply(c,500,"application/json",json{{"error",e.what()}}.dump());}
            } else if(req.method=="GET"&&req.path=="/api/reserved-slots"){
                auto p=findReservedSlots(); json slots=json::array(); if(!p.empty()){auto cc=readFile(p); for(auto& l:splitLines(cc)){auto t=trim(l);if(!t.empty()&&t[0]!='#')slots.push_back(t);}} httpReply(c,200,"application/json",json{{"slots",slots}}.dump());
            } else { httpReply(c,404,"application/json","{}"); }
            closesocket(c);
        }).detach();
    }
    closesocket(g_unbanSocket); g_unbanSocket=INVALID_SOCKET;
    log("UnbanSvr","Server stopped");
}
void startUnbanSvr(){
    if(g_unbanRunning.exchange(true)) return; // already running
    if(!g_cfg.apiKey.empty()) unbanSvr(g_cfg.unbanPort, g_cfg.httpServerBacklog);
    g_unbanRunning = false; // reset on exit (so it can be retried)
}

// ═══════════════════ Relay 启动 ═══════════════════
static std::atomic<bool> g_relayStarted{false};
bool startRelay(){
    if(g_relayStarted.exchange(true)){log("Relay","Already running, skipping");return true;}
    Config cfg=g_cfg; log("Relay","Starting v"+VERSION+"...");
    int serverId=cfg._serverId; std::string token=cfg._token;
    if(serverId==0||token.empty()){
        auto r=httpPost(cfg.panelUrl+"/api/relay/auth",{{"apiKey",cfg.apiKey}},{{"X-API-Key",cfg.apiKey}});
        if(!r.ok){logErr("Relay","Auth failed:"+r.body);g_st.addErr("面板认证失败");g_relayStarted=false;return false;}
        try{auto j=json::parse(r.body);serverId=j["serverId"];token=j["token"];cfg._serverId=serverId;cfg._token=token;cfg._serverName=j.value("serverName","");if(j.contains("rcon")){cfg._rconHost=j["rcon"].value("host","");cfg._rconPort=j["rcon"].value("port",0);cfg._rconPassword=j["rcon"].value("password","");}{std::lock_guard<std::mutex>lk(g_cfgMtx);g_cfg=cfg;saveCfg(cfg);}}catch(std::exception&e){logErr("Relay","Auth parse:"+std::string(e.what()));g_relayStarted=false;return false;}
    }
    g_st.panelOk=true; g_st.startT=nowISO(); log("Relay","Panel OK, serverId:"+std::to_string(serverId));
    g_sender=std::make_unique<RawSender>(cfg.panelUrl,cfg.apiKey,serverId,cfg.rawFlushIntervalMs,cfg.rawBatchSize); g_sender->start();
    auto dedup=std::make_shared<LineDedup>(cfg.lineDedupMaxSize);
    if(!cfg.gameExePath.empty()){
        g_game.config(cfg.gameExePath,cfg.launchParams,cfg.autoRestart,cfg.maxRestartAttempts,cfg.restartCooldownSec,cfg.restartBaseDelaySec);
        g_game.onLine=[&](const std::string& l){if(dedup->markAndSend(l))g_sender->add(l);};
        g_game.onStatus=[](const std::string& s){log("GameSvr","Status:"+s);};
    }
    // Set g_running BEFORE creating threads
    g_running=true;
    std::string rh=cfg.rconPassword.empty()?"":(!cfg._rconHost.empty()&&cfg._rconHost!="127.0.0.1"?"127.0.0.1":cfg._rconHost);
    int rp=cfg.rconPort>0?cfg.rconPort:cfg._rconPort;
    std::string rpw=cfg.rconPassword.empty()?cfg._rconPassword:cfg.rconPassword;
    if(!rpw.empty()){
        std::string h=rh.empty()?"127.0.0.1":rh;
        g_rcon=std::make_unique<Rcon>(h,rp,rpw,
            cfg.rconAuthTimeoutMs,cfg.rconExecTimeoutMs,cfg.rconRecvTimeoutMs,
            cfg.rconReconnectEnabled,cfg.rconReconnectIntervalSec);
        if(g_rcon->connect()){
            g_st.rconOk=true;
            g_rcon->onChat=[&](const std::string& ct,const std::string& pn,const std::string& msg,const std::string& sid){
                httpPost(cfg.panelUrl+"/api/events/chat",{{"type","chat"},{"chatType",ct},{"playerName",pn},{"message",msg},{"steamId",sid},{"serverId",serverId},{"timestamp",nowISO()}},{{"X-API-Key",cfg.apiKey}});
                g_st.sent++; g_st.lastEvt="chat";
            };
            g_rconT=std::thread([&](){while(g_running){std::this_thread::sleep_for(std::chrono::milliseconds(cfg.chatPollIntervalMs));if(g_rcon&&g_rcon->isOk())g_rcon->pollChat();}});
            g_keepT=std::thread([&,sid=serverId,k=cfg.apiKey,u=cfg.panelUrl](){
                while(g_running){
                    std::this_thread::sleep_for(std::chrono::seconds(cfg.keepaliveIntervalSec));
                    if(!g_rcon) continue;
                    // If RCON lost connection, attempt reconnect
                    if(!g_rcon->isOk()){
                        if(cfg.rconReconnectEnabled && g_rcon->shouldAttemptReconnect()){
                            if(g_rcon->reconnect()){
                                g_st.rconOk=true;
                                log("RCON","Reconnected in keepalive thread");
                            } else {
                                g_st.rconOk=false;
                            }
                        }
                        continue;
                    }
                    g_st.rconOk=true;
                    auto raw=g_rcon->exec("ListPlayers");
                    if(!raw.empty())httpPost(u+"/api/events/playerlist-raw",{{"serverId",sid},{"raw",raw}},{{"X-API-Key",k}});
                }
            });
        } else { logErr("RCON","Connect failed"); g_st.addErr("RCON连接失败"); }
    }
    std::string ld=cfg.logDir.empty()?detectLog():cfg.logDir;
    if(!ld.empty()){g_st.logDir=ld;g_tail=std::make_unique<LogTail>(ld,cfg.logPollIntervalMs);g_tail->onLine([&](const std::string& l){if(dedup->checkAndSup(l))g_sender->add(l);});g_tail->start();}
    log("Relay","Started v"+VERSION); return true;
}

// ═══════════════════ Web UI HTML ═══════════════════

// ═══════════════════ 主入口 ═══════════════════
int main(int argc, char* argv[]){
#ifdef _WIN32
    SetConsoleOutputCP(65001); SetConsoleCP(65001);
    char pathBuf[MAX_PATH]; GetModuleFileNameA(NULL,pathBuf,MAX_PATH); g_cfgPath=std::string(dirOf(pathBuf))+"\\relay-config.json";
    SetConsoleCtrlHandler(consoleHandler, TRUE);
#else
    g_cfgPath=(argc>0?dirOf(argv[0]):".")+"/relay-config.json";
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
#endif
    log("Main","Squad Relay v"+VERSION+" starting...");
    g_cfg=loadCfg();
    if(!g_cfg._token.empty()&&!g_cfg.apiKey.empty()){
        log("Main","Config found, auto-starting...");
        {
            std::lock_guard<std::mutex> lk(g_startupMtx);
            g_startupThread = std::thread([](){
                std::this_thread::sleep_for(1s);
                if (!g_running) return; // shutdown happened before we could start
                startRelay();
                if (g_running) startUnbanSvr();
            });
        }
    }
    httpServer(g_cfg.webPort, g_cfg.httpServerBacklog, g_cfg.httpRequestBufferSize); // blocking until shutdown
    shutdownAll();
    return 0;
}
