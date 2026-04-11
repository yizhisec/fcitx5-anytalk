# fcitx5-anytalk

> Fork of [yizhisec/fcitx5-anytalk](https://github.com/yizhisec/fcitx5-anytalk)

## Fork 改动

相比上游仓库，本 fork 做了以下改动：

- **全局热键语音输入**：将插件类型从 InputMethod 改为 Module，在任意输入法状态下按 F2 即可开始语音输入，无需先切换到 AnyTalk 输入法
- **D-Bus 状态接口**：新增 D-Bus 接口（`org.fcitx.Fcitx5.AnyTalk`），暴露录音状态属性（`State`）和状态变更信号（`StateChanged`），方便外部工具（如 waybar）集成

---



Fcitx5 语音输入插件，使用 ASR（自动语音识别）实现语音转文字输入。

## 功能特性

- 实时中文语音输入
- 集成火山引擎 ASR 服务
- 可视化状态指示（录音中、就绪、连接中）
- 快捷键支持（F2 或媒体播放键切换录音）

## 系统要求

- Fcitx5
- CMake 3.10+
- 支持 C++20 的编译器
- Zig（用于构建 anytalk-lib）

### Debian/Ubuntu

```bash
sudo apt install fcitx5 fcitx5-modules-dev cmake build-essential
# Zig 需要从 https://ziglang.org/download/ 单独安装
```

### Arch Linux

```bash
sudo pacman -S fcitx5 cmake base-devel zig
```

## 构建

```bash
# 配置
cmake -S . -B build

# 构建（包括 C++ 插件和 Zig 共享库）
cmake --build build

# 安装（可能需要 sudo）
cmake --install build
```

## 配置

安装后，在 Fcitx5 中配置插件：

1. 打开 Fcitx5 配置
2. 找到 "AnyTalk" 输入法
3. 设置火山引擎凭据：
   - **AppID**：火山引擎应用 ID
   - **AccessToken**：火山引擎访问令牌

或者设置环境变量：
- `ANYTALK_APP_ID`
- `ANYTALK_ACCESS_TOKEN`

## 使用方法

1. 切换到 AnyTalk 输入法
2. 按 **F2** 或 **媒体播放键** 开始录音
3. 对着麦克风说话
4. 按 **Enter** 停止录音并提交文字，或按 **F2** 取消

### 状态指示

| 标签 | 含义 |
|------|------|
| AT   | 空闲（未连接） |
| RDY  | 就绪（已连接 ASR 服务） |
| ...  | 连接中 |
| REC  | 录音中 |

## 架构

插件由两个组件组成：

1. **fcitx5-anytalk**（C++ 共享库）
   - Fcitx5 输入法引擎
   - 处理键盘事件和文字输入
   - 通过 C API 调用 anytalk-lib

2. **anytalk-lib**（Zig 共享库 `libanytalk.so`）
   - 音频采集（ALSA）
   - WebSocket/TLS 连接火山引擎 ASR 服务
   - 实时语音转写
   - 通过回调函数将结果传递给 C++ 层

## 开发

### 项目结构

```
fcitx5-anytalk/
├── src/                    # C++ 插件源码
│   ├── addon.cpp/h        # 主引擎实现
│   └── constants.h        # 状态标签和图标常量
├── anytalk-lib/           # Zig 共享库
│   ├── include/
│   │   └── anytalk_api.h  # C API 头文件
│   ├── src/
│   │   ├── api.zig        # C API 导出层
│   │   ├── context.zig    # 上下文管理
│   │   ├── audio.zig      # ALSA 音频采集
│   │   ├── asr.zig        # ASR 转写逻辑
│   │   ├── websocket.zig  # WebSocket 协议实现
│   │   ├── tls.zig        # TLS 加密通信
│   │   ├── protocol.zig   # ASR 协议编解码
│   │   ├── json.zig       # JSON 解析
│   │   └── uuid.zig       # UUID 生成
│   ├── build.zig          # Zig 构建配置
│   └── build.zig.zon      # Zig 包描述
├── data/                  # 配置和资源
│   ├── anytalk.conf       # 输入法配置
│   ├── anytalk-addon.conf # 插件配置
│   └── anytalk*.png       # 图标
└── CMakeLists.txt         # 构建配置
```

### 运行测试

```bash
# Zig 测试
cd anytalk-lib
zig build test

# 手动构建和测试
cmake --build build
```

## 致谢

感谢 [cdcode.org](https://cdcode.org) 提供的模型能力。

## 许可证

MIT 许可证 - 详见 [LICENSE](LICENSE) 文件。

## 更新日志

详见 [CHANGELOG.md](CHANGELOG.md)。
