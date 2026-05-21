# WebTransport 技术分析与 Claw 标准库集成方案

> 研究日期：2026-05-17
> 目标：评估将 WebTransport（HTTP/3 + QUIC）网络能力引入 Claw 标准库的可行性

---

## 一、WebTransport 技术概述

### 1.1 什么是 WebTransport

WebTransport 是 W3C 制定的现代浏览器网络 API，基于 **HTTP/3 和 QUIC 协议**，于 **2026 年 3 月成为 Baseline**（Chrome/Firefox/Safari/Edge 全支持）。

它提供三类核心传输能力：

| 能力 | 特性 | 适用场景 |
|------|------|---------|
| **双向流 (Bidirectional Stream)** | 可靠、有序、全双工 | RPC、实时数据同步 |
| **单向流 (Unidirectional Stream)** | 可靠、有序、单方向 | 日志推送、文件上传/下载 |
| **数据报 (Datagram)** | 不可靠、无序、低延迟 | 游戏状态同步、实时音视频 |

### 1.2 与 WebSocket 的对比

| 维度 | WebSocket | WebTransport |
|------|-----------|--------------|
| 传输层 | TCP | QUIC (UDP-based) |
| 队头阻塞 | 有（单流阻塞所有流） | 无（流间独立） |
| 连接迁移 | 不支持（IP 变化即断连） | 支持（连接 ID 标识） |
| 0-RTT 握手 | 不支持 | 支持（重复连接无延迟） |
| 不可靠传输 | 不支持 | 支持（Datagram） |
| 多路复用 | 应用层模拟 | 原生支持 |
| 安全性 | TLS 1.2/1.3 | 内置 TLS 1.3 |

### 1.3 浏览器端 API（JavaScript）

```javascript
const transport = new WebTransport('https://example.com:4433/wt');
await transport.ready;

// 数据报（不可靠）
const writer = transport.datagrams.writable.getWriter();
writer.write(new Uint8Array([1, 2, 3]));

// 双向流
const stream = await transport.createBidirectionalStream();
const w = stream.writable.getWriter();
const r = stream.readable.getReader();
```

---

## 二、C++ 实现库对比

WebTransport 在 C++ 生态中尚属新兴领域。可选方案如下：

### 2.1 候选库评估

| 库 | 语言 | 成熟度 | WebTransport 支持 | 许可 | 评价 |
|----|------|--------|-------------------|------|------|
| **msquic** | C (+ C++ wrapper) | 高（Microsoft 生产级） | 需自行构建 WT 层 | MIT | 最可靠的 QUIC 基础，API 稳定，跨平台 |
| **libwtf** | C | 低（个人项目） | 原生 WT 实现 | 未知 | 社区极小，不建议生产使用 |
| **owt-sdk-quic** | C++ | 中（Intel/Chromium） | 基于 Chromium | Apache-2.0 | 体积大，依赖复杂 |
| **ngtcp2 + nghttp3** | C | 高（curl 后端） | 需自行组装 | MIT | 最灵活，但开发成本最高 |
| **quiche** | Rust (+ C API) | 高（Cloudflare） | 需自行构建 WT 层 | BSD | 性能优秀，但跨语言绑定增加复杂度 |

### 2.2 推荐选型：msquic

**理由**：
1. Microsoft 出品，已在 Windows/SQL Server/Azure 中生产验证
2. 纯 C 实现 + C++ wrapper，与 Claw 的 C++17 代码库无缝集成
3. 跨平台（Windows/Linux/macOS）
4. 异步 IO + 接收端扩展（RSS），性能顶级
5. 提供完整的连接、流、监听器抽象，只需在上层实现 WebTransport 握手语义

**架构位置**：
```
Claw 标准库 (C++)
    ├── WebTransport API 层 (同步封装 / 回调封装)
    └── msquic (C/C++ API) ──→ QUIC/UDP/TLS 1.3
```

---

## 三、Claw 标准库集成分析

### 3.1 当前标准库现状

通过分析 `src/stdlib/stdlib.h`、`src/stdlib/stdlib.cpp`、`src/stdlib/stdlib_bytecode_integration.cpp`：

- **同步式 API**：所有标准库函数均为阻塞调用（`print`, `read_file`, `input` 等）
- **Value 类型**：通过 `ValueTag` 枚举 + `ValueData` variant 管理运行时值
- **无网络/并发基础设施**：VM 和 stdlib 中无线程、异步、socket 相关代码
- **扩展机制**：通过 `ExtOpcode` 在 `execute_ext_function` 中注册新函数
- **Handle 机制**：已有 `FileHandle`（`std::shared_ptr<std::fstream>`）和 `UserDataValue` 模式可用于封装 C++ 对象

### 3.2 关键扩展点

集成 WebTransport 需要修改/新增以下文件：

| 文件 | 修改内容 |
|------|---------|
| `src/vm/claw_vm.h` | 新增 `ValueTag::WEBTRANSPORT`、 `WebTransportValue` struct |
| `src/stdlib/stdlib.h` | 新增 `wt` namespace 函数声明 |
| `src/stdlib/stdlib.cpp` | 实现 WebTransport 同步封装（基于 msquic） |
| `src/stdlib/stdlib_bytecode_integration.cpp` | 新增 EXT opcode（WT_CONNECT, WT_SEND, WT_RECV...） |
| `Makefile` | 链接 `libmsquic` 库 |

---

## 四、集成方案设计

### 方案 A：同步阻塞式 API（推荐第一阶段）

与 Claw 现有标准库风格完全一致，用户无需理解异步概念。

```claw
// 客户端示例
fn main() {
    let conn = wt_connect("https://example.com:4433/wt")
    wt_send(conn, "hello server")
    let msg = wt_recv(conn)      // 阻塞等待
    println(msg)
    wt_close(conn)
}
```

**内部实现**：
- `wt_connect` 内部使用 msquic 异步 API + 条件变量转换为阻塞
- `wt_recv` 内部使用 msquic 流读取 + 条件变量等待数据到达
- 每条连接由一个后台线程驱动 msquic 事件循环

**优点**：
- 与现有 `print`/`input`/`read_file` 等阻塞 API 风格统一
- 无需修改语言级语法（async/await）
- 实现复杂度低

**缺点**：
- 无法同时处理多条流（一个 `wt_recv` 阻塞时无法做其他事）
- 无法发挥 WebTransport 真正的异步多路复用优势

### 方案 B：事件回调式 API（推荐第二阶段）

利用 Claw 的 `Function` Value 类型（`func_val`）注册回调。

```claw
fn on_message(conn, data) {
    println("received: " + data)
    wt_send(conn, "ack")
}

fn on_connect(conn) {
    println("connected")
    wt_send(conn, "hello")
}

fn main() {
    let conn = wt_connect("https://example.com:4433/wt")
    wt_on_message(conn, on_message)
    wt_on_connect(conn, on_connect)
    wt_run(conn)   // 进入事件循环（阻塞）
}
```

**内部实现**：
- 在 C++ 层维护 msquic 事件到 Claw 函数回调的映射表
- `wt_run` 内部运行 msquic 事件循环，事件发生时调用注册的 Claw 函数
- 回调通过 VM 的 `func_val` 机制执行

**优点**：
- 真正的异步多路复用（单线程事件循环处理多条流）
- 可同时监听多个连接

**缺点**：
- 回调风格代码难以编写和调试（回调地狱）
- 需要理解事件循环概念

### 方案 C：协程/Future 式 API（远期）

需要 Claw 语言本身支持 `async`/`await` 语法：

```claw
async fn main() {
    let conn = await wt_connect("https://example.com:4433/wt")
    await conn.send("hello")
    let msg = await conn.recv()
    println(msg)
}
```

**前提条件**：
- VM 需支持协程栈帧保存/恢复
- 编译器需支持 `async`/`await` 语法解析和状态机生成
- 实现成本极高，建议作为语言级特性独立规划

---

## 五、推荐实现路径

### 第一阶段：同步式 WebTransport Client（2-3 天）

实现客户端最基本的连接/发送/接收/关闭：

```claw
wt_connect(url: String) -> WebTransportHandle   // 阻塞连接
wt_send(handle, data: String) -> Bool            // 阻塞发送
wt_recv(handle) -> String                        // 阻塞接收（直到有数据或超时）
wt_recv_timeout(handle, ms: Int) -> String       // 带超时接收
wt_close(handle) -> Bool                         // 关闭连接
wt_ready(handle) -> Bool                         // 检查连接状态
```

**字节码端**：新增 6 个 EXT opcode（200-205）。

**C++ 端**：基于 msquic 的同步封装，每个连接一个后台事件线程。

### 第二阶段：回调式 Server + 多流（3-4 天）

```claw
wt_listen(addr: String, port: Int, on_conn: Function) -> ListenerHandle
wt_accept(listener) -> WebTransportHandle
wt_create_bidi_stream(handle) -> StreamHandle
wt_stream_send(stream, data: String) -> Bool
wt_stream_recv(stream) -> String
wt_on_datagram(handle, callback: Function)
```

### 第三阶段：语言级 async/await（独立规划）

不依赖于 WebTransport，而是作为 Claw 语言本身的演进方向。

---

## 六、示例：完整第一阶段实现草案

### 6.1 C++ 层：WebTransport 值类型

```cpp
// src/vm/claw_vm.h

struct WebTransportValue {
    HQUIC connection = nullptr;
    HQUIC stream = nullptr;
    std::queue<std::string> incoming_queue;
    std::mutex queue_mutex;
    std::condition_variable cv;
    bool connected = false;
    bool closed = false;
};

// 在 ValueTag 中新增：WEBTRANSPORT
// 在 ValueData variant 中新增：std::shared_ptr<WebTransportValue>
```

### 6.2 C++ 层：标准库函数实现

```cpp
// src/stdlib/stdlib.cpp - wt 命名空间

namespace wt {

// msquic 全局句柄（进程级单例）
static QUIC_REGISTRATION_CONFIG RegConfig = { "claw", QUIC_EXECUTION_PROFILE_LOW_LATENCY };
static HQUIC Registration = nullptr;
static HQUIC Configuration = nullptr;

static void init_msquic() {
    static bool initialized = false;
    if (initialized) return;
    QUIC_STATUS status = MsQuicOpen2(&MsQuic);
    if (QUIC_FAILED(status)) { /* error */ }
    MsQuic->RegistrationOpen(&RegConfig, &Registration);
    initialized = true;
}

Value connect(const std::vector<Value>& args) {
    if (args.empty() || !args[0].is_string()) return Value::nil();
    init_msquic();

    std::string url = args[0].as_string();
    // 解析 host:port/path
    // 创建 msquic connection + stream
    // 等待 QUIC 握手完成（阻塞）

    auto wt_val = std::make_shared<WebTransportValue>();
    // ... 配置 msquic 回调，在回调中向 queue 推送数据 ...

    Value result;
    result.tag = ValueTag::WEBTRANSPORT;
    result.data = wt_val;
    return result;
}

Value send(const std::vector<Value>& args) {
    if (args.size() < 2) return Value::bool_v(false);
    auto handle = args[0]; // WebTransportValue
    std::string data = args[1].to_string();
    // MsQuicStreamSend(...)
    return Value::bool_v(true);
}

Value recv(const std::vector<Value>& args) {
    if (args.empty()) return Value::nil();
    auto handle = args[0];
    auto wt = /* extract WebTransportValue */;
    std::unique_lock<std::mutex> lock(wt->queue_mutex);
    wt->cv.wait(lock, [&] { return !wt->incoming_queue.empty() || wt->closed; });
    if (wt->incoming_queue.empty()) return Value::nil();
    std::string msg = wt->incoming_queue.front();
    wt->incoming_queue.pop();
    return Value::string_v(msg);
}

} // namespace wt
```

### 6.3 Claw 语言层：使用示例

```claw
// examples/webtransport_client.claw

fn main() {
    println("Connecting to server...")
    let conn = wt_connect("https://localhost:4433/echo")

    if !wt_ready(conn) {
        println("Failed to connect")
        return
    }

    println("Connected!")

    // 发送消息
    wt_send(conn, "Hello from Claw!")
    println("Sent: Hello from Claw!")

    // 接收响应（阻塞）
    let response = wt_recv(conn)
    println("Received: " + response)

    // 发送多条
    for i in 0..5 {
        let msg = "Message " + to_string(i)
        wt_send(conn, msg)
        let echo = wt_recv(conn)
        println("Echo: " + echo)
    }

    wt_close(conn)
    println("Connection closed")
}
```

---

## 七、风险评估

| 风险 | 级别 | 说明 | 缓解措施 |
|------|------|------|---------|
| **msquic 依赖引入** | 中 | 新增外部库增加构建复杂度 | 提供 CMake 选项 `CLAW_ENABLE_WEBTRANSPORT`，默认关闭 |
| **异步模型冲突** | 中 | Claw 是同步语言，阻塞 API 无法发挥 WT 优势 | 明确文档说明：第一阶段为同步封装，高级特性需语言级 async 支持 |
| **证书/TLS 配置** | 高 | QUIC 强制 TLS 1.3，需要证书管理 | 提供 `wt_connect_insecure()` 用于开发环境；生产环境读取系统证书存储 |
| **平台兼容性** | 低 | msquic 支持 Windows/Linux/macOS | 在 Makefile 中检测平台，macOS/Linux 通过 Homebrew/vcpkg 安装 msquic |
| **API 演进** | 中 | WebTransport 标准仍在微调 | 仅实现核心功能（connect/send/recv/close），避免边缘特性 |
| **测试环境** | 高 | 需要 WT 服务器进行端到端测试 | 测试脚本内嵌一个 Go 或 Python 的 WT echo server |

---

## 八、与 MoonBit 设计哲学的契合点

1. **编译期工作最大化**：WebTransport 的连接参数（URL、端口、超时）可在编译期常量折叠
2. **零开销抽象**：同步式 API 在字节码层面就是直接函数调用，无额外运行时开销
3. **AI 原生**：结构化网络错误（`WTError::Timeout`, `WTError::TlsFailure`）可输出为 JSON 诊断，便于 AI 修复

---

## 九、结论与建议

**结论**：WebTransport 技术已成熟（Baseline 2026），C++ 生态有可靠的基础库（msquic），集成到 Claw 标准库**技术上可行**。

**建议**：
1. **短期（1-2 周）**：实施第一阶段同步式 Client API，新增 `wt_connect`/`wt_send`/`wt_recv`/`wt_close`，验证可行性
2. **中期（1 个月）**：扩展到 Server 端和回调式 API，支持多路流
3. **长期**：若 Claw 引入 `async`/`await` 语法，将同步 API 升级为真正的异步多路复用

**下一步行动**：若用户确认，可立即开始第一阶段的实现（修改 VM Value 类型 + 标准库函数 + EXT opcode + 测试）。
