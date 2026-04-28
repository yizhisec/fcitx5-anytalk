# 架构与开发参考

## 高层架构

```
┌──────────────────── fcitx5 进程 ────────────────────┐
│  anytalk.so (Module addon, ~210 行)                 │
│   ├─ 监听 F2 / Esc 全局键                            │
│   ├─ 通过 D-Bus 调用 anytalk-overlay 的 method       │
│   ├─ 订阅 overlay 的 D-Bus 信号                      │
│   └─ 收到 CommitText 后 ic->commitString            │
└───────────────────────┬─────────────────────────────┘
                        │ D-Bus session bus
                        │ org.fcitx.Fcitx5.AnyTalk.Overlay
                        ▼
┌─────────────── anytalk-overlay (Qt6) ───────────────┐
│  AudioCapture (libpulse-simple, QThread)            │
│  AsrBackend  (interface) ─┐                         │
│    └─ VolcengineBackend (QWebSocket)                │
│       (future: OpenAI / whisper.cpp / Sherpa-ONNX)  │
│  AsrController (拼装 audio + backend)                │
│  OverlayWindow (Aurora dock UI, layer-shell)        │
│  OverlayService (D-Bus methods + signals)           │
│  SettingsDialog (--settings)                        │
└──────────────────────────────────────────────────────┘
```

overlay 通过 **D-Bus session-bus activation** 拉起 —— 用户什么都不用配置，按 F2 时 session bus 自动 fork `/usr/bin/anytalk-overlay`。

## D-Bus 接口

| Service | `org.fcitx.Fcitx5.AnyTalk.Overlay` |
|---|---|
| Object | `/overlay` |
| Interface | `org.fcitx.Fcitx5.AnyTalk.Overlay` |

**Methods**: `StartRecording` / `StopRecording` / `CancelRecording` / `Show` / `Hide` / `Ping` / `OpenSettings`

**Signals**: `StateChanged(s)` / `AudioLevel(d)` / `TranscriptPartial(s)` / `TranscriptFinal(s)` / `ErrorOccurred(s)` / `CommitText(s)`

addon 自身保留 `org.fcitx.Fcitx5.AnyTalk` 的 `StateChanged` 信号，供 waybar 之类已经接入老协议的观察者继续使用。

## 添加新 ASR 后端

```cpp
class MyBackend : public AsrBackend {
    Q_OBJECT
public:
    void start() override        { /* 建立连接 */ }
    void pushPcm(const QByteArray &chunk) override { /* 送音频 */ }
    void stop() override         { /* drain 后 emit finished() */ }
    void cancel() override       { /* 立即 emit finished() */ }
};
```

然后在 `anytalk-overlay/src/asr/AsrBackendFactory.cpp` 加一个分支，
在 `SettingsDialog` 加对应的字段 section，完成。流水线（音频 / UI / D-Bus / commit）不用动。

豆包 ASR 协议参考：[doubao-asr-api.md](doubao-asr-api.md)。

## 项目结构

```
src/                           # fcitx5 addon (~210 行)
  ├── addon.{h,cpp}            # F2/Esc 拦截 + 调 overlay + 收 D-Bus 信号 commit
  └── constants.h              # 状态字符串、图标名、D-Bus 服务名
anytalk-overlay/               # Qt6 独立进程
  ├── CMakeLists.txt
  └── src/
      ├── main.cpp
      ├── Config.{h,cpp}       # INI sections + 兼容旧扁平
      ├── OverlayState.h       # 状态字符串集中常量
      ├── SettingsDialog.{h,cpp}
      ├── AsrController.{h,cpp}    # 拼装 audio + backend
      ├── audio/AudioCapture.{h,cpp}   # libpulse-simple + QThread
      ├── asr/AsrBackend.h             # 后端抽象接口
      ├── asr/AsrBackendFactory.{h,cpp}
      ├── asr/VolcengineProtocol.{h,cpp}
      ├── asr/VolcengineBackend.{h,cpp}    # QWebSocket 实现
      ├── OverlayService.{h,cpp}    # D-Bus 表面
      ├── OverlayWindow.{h,cpp}     # Aurora dock UI
      ├── AuroraBars.{h,cpp}        # 自绘音频条形
      ├── StatusDot.{h,cpp}         # 状态点 + 脉动
      └── Theme.h
data/                          # 图标、conf、D-Bus service 文件
docs/                          # 文档
```

## 从源码构建

依赖：

| 依赖 | 用途 |
|---|---|
| Fcitx5 | 输入法框架 |
| Qt6 (Core / Gui / Widgets / DBus / Concurrent / WebSockets) | overlay 进程 + ASR 通讯 |
| libpulse-simple | 音频抓取（PulseAudio / PipeWire 兼容） |
| CMake 3.16+ + C++20 编译器 | 构建 |
| **layer-shell-qt**（可选） | Wayland 下的精确居中（KDE / Sway / wlroots） |

Arch：

```bash
sudo pacman -S fcitx5 fcitx5-qt qt6-base qt6-tools qt6-websockets cmake base-devel libpulse layer-shell-qt
```

Debian / Ubuntu：

```bash
sudo apt install fcitx5 fcitx5-modules-dev cmake build-essential \
                 qt6-base-dev qt6-websockets-dev libpulse-dev pkg-config
# layer-shell-qt 视发行版而定（可选；缺失时 Wayland 走合成器默认布局）
```

构建：

```bash
cmake -S . -B build
cmake --build build
sudo cmake --install build      # 默认 prefix=/usr
```

`-DBUILD_OVERLAY=OFF` 可以跳过 Qt6 overlay 的构建（仅装 fcitx5 addon）。
