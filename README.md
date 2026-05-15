# Squad 服务器控制面板 (C++)

Squad 游戏服务器的 Web 管理面板，C++ 后端 + 模块化前端。

## 功能

- 🖥️ 服务器管理（RCON 远程控制）
- 👥 玩家管理（踢出/封禁/预留位）
- 🏆 积分系统（击杀/TK/救援计分）
- 🔌 插件系统（JS 插件热加载）
- 👫 团队管理（邀请码/申请/审批）
- 📊 数据分析（在线图表/Tick 监控）
- 💬 聊天记录与口令系统
- 🔄 Relay 中继模式支持

## 技术栈

- **后端**: C++17, cpp-httplib, SQLite3 (WAL), nlohmann/json
- **前端**: 原生 JS 模块化架构
- **部署**: Nginx 反代 + Let's Encrypt HTTPS

## 编译

```bash
cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)
```

## 运行

```bash
./build/squad-panel
```

默认端口 3000，配置见 `config.ini.example`。
