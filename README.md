# fcitx5-anytalk

Fcitx5 语音输入插件，使用 ASR（自动语音识别）实现语音转文字输入。

## 功能特性

- 实时中文语音输入
- 集成火山引擎 ASR 服务
- 可视化状态指示（录音中、就绪、连接中）
- 快捷键支持（F2 或媒体播放键切换录音）

## 系统要求

- Fcitx5
- CMake 3.16+
- 支持 C++20 的编译器
- Rust 1.70+（用于构建 daemon）
- ALSA 开发库
- json-c 库

### Debian/Ubuntu

```bash
sudo apt install fcitx5 fcitx5-modules-dev cmake build-essential \
    libasound2-dev libjson-c-dev cargo
```

### Arch Linux

```bash
sudo pacman -S fcitx5 cmake base-devel alsa-lib json-c rust
```

## 构建

```bash
# 配置
cmake -S . -B build

# 构建（包括 C++ 插件和 Rust daemon）
cmake --build build

# 安装（可能需要 sudo）
cmake --install build
```

### 构建选项

- `BUILD_DAEMON`（默认：ON）- 构建 Rust daemon
- `DAEMON_DEBUG`（默认：OFF）- 以调试模式构建 daemon

```bash
# 示例：跳过 daemon 构建
cmake -S . -B build -DBUILD_DAEMON=OFF
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
   - 通过 Unix socket 与 daemon 通信

2. **anytalk-daemon**（Rust 二进制程序）
   - 音频采集和处理
   - WebSocket 连接 ASR 服务
   - 实时转写

## 开发

### 项目结构

```
fcitx5-anytalk/
├── src/                    # C++ 插件源码
│   ├── addon.cpp/h        # 主引擎实现
│   ├── ipc_client.cpp/h   # Unix socket IPC 客户端
│   ├── daemon_manager.cpp/h # Daemon 生命周期管理
│   └── constants.h        # 字符串常量
├── anytalk-daemon/        # Rust daemon
│   └── src/
│       ├── main.rs        # 入口点
│       ├── audio.rs       # 音频采集
│       ├── asr.rs         # ASR WebSocket 客户端
│       └── ...
├── data/                  # 配置和资源
│   ├── anytalk.conf       # 输入法配置
│   ├── anytalk-addon.conf # 插件配置
│   └── anytalk*.png       # 图标
└── CMakeLists.txt         # 构建配置
```

### 运行测试

```bash
# Rust 测试
cd anytalk-daemon
cargo test

# 手动构建和测试
cmake --build build
./anytalk-daemon/target/release/anytalk-daemon
```

### 开发者模式

在配置中启用开发者模式，可手动启动 daemon 进行调试：

```bash
# 手动启动 daemon 并开启日志
RUST_LOG=debug ./anytalk-daemon/target/release/anytalk-daemon
```

## 许可证

MIT 许可证 - 详见 [LICENSE](LICENSE) 文件。

## 更新日志

详见 [CHANGELOG.md](CHANGELOG.md)。
