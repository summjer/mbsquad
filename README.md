# Squad 服务器控制面板 (C++)

Squad 游戏服务器的 Web 管理面板，C++ 后端 + 模块化前端。支持多服务器管理、RCON 远程控制、Relay 中继模式、插件系统、团队管理等功能。


> 注意：由于没有找到带认证的 Squad 服务器进行实测，代码中的 RCON 指令可能不完全正确，使用前请自行验证并更正。
> 项目经历了 Node.js → C++ 的完整重写，当前版本 v14.4，约 10000 行 C++ 代码，71 个源文件，130+ 条 API 路由，31 张数据库表。

---

## 功能一览

### 服务器管理
- 多服务器支持，每个服务器独立配置
- RCON 远程控制（踢出/封禁/警告/广播/换图）
- 实时状态监控（在线玩家/地图/Tick）
- 服务器预设管理

### 玩家管理
- 玩家库（按 SteamID 追踪，跨服务器）
- 踢出 / 封禁 / 解封（支持 Relay 同步到 BanList.cfg）
- 预留位管理（RCON 即时生效 + Relay 持久化）
- 跳边锁定（防止反复换队）

### 积分系统

| 事件 | 积分 |
|------|------|
| 击杀 | +2 |
| TK 击杀 | -5 |
| 自杀 | -2 |
| 救援 | +1 |

- 积分排行榜（公开 API，无需登录）
- CDK 激活码兑换
- 聊天口令：签到 / 抽奖 / 查战绩 / 查积分 / 兑换 / 跳边

### 团队系统 (v14.4)
- 创建团队 / 邀请码生成 / 通过邀请码加入
- 申请加入 / 审批 / 拒绝
- 移除成员 / 退出团队

### 插件系统
内置 6 个 C++ 插件 + 11 个 JS 插件：

| 插件 | 功能 |
|------|------|
| chat_commands | 聊天口令（签到/抽奖/查战绩等） |
| tk_forgive | TK 道歉系统 |
| welcome | 进服欢迎消息 |
| cdk_redeem | CDK 激活码兑换 |
| team_balance | 队伍自动平衡 |
| switch_lock | 防跳边锁定 |
| timed-broadcast | 定时广播 |
| kill-points | 击杀积分 |
| revive-points | 救援积分 |
| player-sync | 玩家数据同步 |
| chat-commands | 聊天命令扩展 |

### 数据分析
- 在线人数图表 / 服务器 Tick 监控 / 建队记录 / 击杀统计（按武器/玩家/时间段）

### 聊天系统
- 聊天记录查询 / AdminWarn 通知（RCON 发送，自动转义特殊字符） / 击杀通知实时推送

### Relay 中继模式
```
游戏服务器 (Windows)                    VPS (Ubuntu)
┌─────────────────────┐                ┌──────────────────────┐
│  SquadServer.exe    │                │  squad-panel (C++)   │
│  SquadGame.log      │                │  :3000               │
│                     │   HTTP POST    │                      │
│  squad-relay.exe ───┼───────────────►│  /api/events/raw     │
│  (:18976 Web UI)    │                │  /api/events/chat    │
│  (:18977 Unban)     │                │  /api/events/...     │
│                     │                │                      │
│  RCON ──────────────┼──poll chat────►│  /api/relay/auth     │
│  BanList.cfg        │                │  /api/relay/register │
│  Admins.cfg         │◄──sync admin───│  admin_handler       │
│  ReservedSlots.cfg  │◄──sync slots───│  missing_routes      │
└─────────────────────┘                └──────────────────────┘
```
- Relay 认证：注册码 → apiKey → token
- 管理员/预留位/解封双向同步（RCON 即时 + Relay 持久化）

---

## 技术栈

| 层 | 技术 |
|----|------|
| 后端 | C++17, cpp-httplib, SQLite3 (WAL), nlohmann/json |
| 前端 | 原生 JS 模块化架构（9 个模块） |
| 反代 | Nginx（gzip 压缩 + 静态缓存 7 天） |
| HTTPS | Let's Encrypt（certbot 自动续期） |
| 编译 | g++ 11.4.0, C++17 |
| 服务 | systemd |

---

## 项目结构

```
squad-panel-cpp/
├── config.ini.example         ← 配置模板
├── CMakeLists.txt
├── src/
│   ├── main.cpp               ← 入口
│   ├── core/                  ← 数据库/认证/日志解析/积分/工具库
│   ├── net/                   ← HTTP 服务器/RCON/WebSocket
│   ├── handlers/              ← 18 个 handler，130+ 条路由
│   ├── plugins/               ← 6 个 C++ 插件
│   └── tasks/                 ← 4 个后台定时任务
├── public/                    ← 前端静态文件
│   ├── index.html
│   ├── css/
│   └── js/modules/            ← servers/users/kills/bans/points/plugins/logs/chat/players
├── plugins/                   ← JS 插件目录
└── vendor/                    ← httplib.h / json.hpp
```

---

## 编译与部署

```bash
# 编译
cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)

# 配置
cp config.ini.example config.ini

# 运行
./build/squad-panel

# systemd 服务
sudo systemctl enable squad-panel-cpp
sudo systemctl start squad-panel-cpp
```

---

## 插件开发

```cpp
#include "plugin.h"

class MyPlugin : public Plugin {
public:
    std::string name() const override { return "my-plugin"; }
    void handle(const PluginEvent& ev, PluginContext& ctx) override {
        // 处理事件：player_join / kill / team_kill / chat / ...
    }
};

static auto _ = PluginManager::instance().registerPlugin(std::make_unique<MyPlugin>());
```

---

## 默认管理员

```
用户名: admin
密码: squad2026
```

> 首次部署后请立即修改默认密码。

---

## License

MIT
