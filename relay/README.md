# Squad Relay (C++)

Squad 游戏服务器的日志中转程序，部署在 Windows 游戏服务器上，负责将游戏日志转发到面板后端。

## 功能

- 实时监听 SquadGame.log，转发事件到面板
- 内置 Web 管理界面（端口 18976）
- RCON 聊天轮询
- 解封接口（端口 18977）
- Admin/预留位配置同步
- 零依赖，单 exe，双击即用

## 编译

### Windows (MinGW 交叉编译，必须用 posix 线程模型)

```bash
x86_64-w64-mingw32-g++-posix -std=c++17 -O2 -s -static -Ivendor -o squad-relay.exe main.cpp -lws2_32 -lbcrypt -lshell32
```

### Linux

```bash
g++ -std=c++17 -O2 -s -Ivendor -o squad-relay main.cpp -lpthread
```

## 使用

1. 将 `squad-relay.exe` 放到 Squad 游戏服务器上
2. 双击运行，浏览器打开 `http://localhost:18976`
3. 在 Web 界面中输入面板地址和注册码完成配置
