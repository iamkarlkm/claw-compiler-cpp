// WebTransport Backend Implementations (mock + msquic)

#include "webtransport_backend.h"
#include "claw_vm.h"

#include <ctime>
#include <cstring>
#include <unordered_map>

#ifdef CLAW_ENABLE_WEBTRANSPORT
#include <msquic.h>
#include <arpa/inet.h>
#endif

namespace claw {
namespace vm {

// ============================================================================
// Mock Backend
// ============================================================================
static std::weak_ptr<WebTransportValue> g_mock_server;

std::shared_ptr<WebTransportValue> MockWebTransportBackend::connect(const std::string& url) {
    if (url == "mock://server") {
        auto server = g_mock_server.lock();
        if (server) {
            auto client = std::make_shared<WebTransportValue>();
            client->url = url;
            client->connected = true;
            auto srv_conn = std::make_shared<WebTransportValue>();
            srv_conn->url = "mock://server_accepted";
            srv_conn->connected = true;
            client->peer = srv_conn;
            srv_conn->peer = client;
            {
                std::lock_guard<std::mutex> lock(server->server_mutex);
                server->pending_connections.push_back(srv_conn);
            }
            server->server_cv.notify_all();
            return client;
        }
    }
    auto wt = std::make_shared<WebTransportValue>();
    wt->url = url;
    wt->connected = true;
    return wt;
}

bool MockWebTransportBackend::send(const std::shared_ptr<WebTransportValue>& wt, const std::string& data) {
    if (!wt || wt->closed) return false;
    auto target = wt->peer ? wt->peer : wt;
    std::lock_guard<std::mutex> lock(target->queue_mutex);
    target->incoming_queue.push_back(data);
    target->cv.notify_all();
    return true;
}

std::string MockWebTransportBackend::recv(const std::shared_ptr<WebTransportValue>& wt, int timeout_ms) {
    if (!wt || wt->closed) return "";
    std::unique_lock<std::mutex> lock(wt->queue_mutex);
    auto pred = [wt]() { return !wt->incoming_queue.empty() || wt->closed; };
    if (timeout_ms > 0) {
        wt->cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), pred);
    } else {
        wt->cv.wait(lock, pred);
    }
    if (!wt->incoming_queue.empty()) {
        std::string msg = wt->incoming_queue.front();
        wt->incoming_queue.pop_front();
        return msg;
    }
    return "";
}

bool MockWebTransportBackend::close(const std::shared_ptr<WebTransportValue>& wt) {
    if (!wt) return false;
    wt->closed = true;
    wt->connected = false;
    wt->cv.notify_all();
    return true;
}

bool MockWebTransportBackend::ready(const std::shared_ptr<WebTransportValue>& wt) const {
    return wt && wt->connected && !wt->closed;
}

std::shared_ptr<WebTransportValue> MockWebTransportBackend::open_stream(
    const std::shared_ptr<WebTransportValue>& wt, bool bidirectional) {
    (void)bidirectional;
    if (!wt || wt->closed) return nullptr;
    auto stream = std::make_shared<WebTransportValue>();
    stream->is_stream = true;
    stream->connected = true;
    stream->parent_conn = wt;
    return stream;
}

bool MockWebTransportBackend::stream_send(
    const std::shared_ptr<WebTransportValue>& stream, const std::string& data) {
    if (!stream || stream->closed || !stream->is_stream) return false;
    auto parent = stream->parent_conn;
    if (!parent) return false;
    std::lock_guard<std::mutex> lock(parent->queue_mutex);
    parent->incoming_queue.push_back(data);
    parent->cv.notify_all();
    return true;
}

std::string MockWebTransportBackend::stream_recv(
    const std::shared_ptr<WebTransportValue>& stream, int timeout_ms) {
    if (!stream || stream->closed || !stream->is_stream) return "";
    auto parent = stream->parent_conn;
    if (!parent) return "";
    std::unique_lock<std::mutex> lock(parent->queue_mutex);
    auto pred = [parent]() { return !parent->incoming_queue.empty() || parent->closed; };
    if (timeout_ms > 0) {
        parent->cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), pred);
    } else {
        parent->cv.wait(lock, pred);
    }
    if (!parent->incoming_queue.empty()) {
        std::string msg = parent->incoming_queue.front();
        parent->incoming_queue.pop_front();
        return msg;
    }
    return "";
}

bool MockWebTransportBackend::stream_close(
    const std::shared_ptr<WebTransportValue>& stream) {
    if (!stream || !stream->is_stream) return false;
    stream->closed = true;
    stream->connected = false;
    return true;
}

std::shared_ptr<WebTransportValue> MockWebTransportBackend::listen(
    const std::string& address) {
    (void)address;
    auto server = std::make_shared<WebTransportValue>();
    server->is_server = true;
    server->connected = true;
    g_mock_server = server;
    return server;
}

std::shared_ptr<WebTransportValue> MockWebTransportBackend::accept(
    const std::shared_ptr<WebTransportValue>& server, int timeout_ms) {
    if (!server || !server->is_server) return nullptr;
    std::unique_lock<std::mutex> lock(server->server_mutex);
    auto pred = [server]() { return !server->pending_connections.empty() || server->closed; };
    if (timeout_ms > 0) {
        server->server_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), pred);
    } else {
        server->server_cv.wait(lock, pred);
    }
    if (!server->pending_connections.empty()) {
        auto conn = server->pending_connections.front();
        server->pending_connections.pop_front();
        return conn;
    }
    return nullptr;
}

bool MockWebTransportBackend::close_server(
    const std::shared_ptr<WebTransportValue>& server) {
    if (!server || !server->is_server) return false;
    server->closed = true;
    server->connected = false;
    server->server_cv.notify_all();
    g_mock_server.reset();
    return true;
}

#ifdef CLAW_ENABLE_WEBTRANSPORT
// ============================================================================
// Msquic Backend
// ============================================================================

static std::mutex g_msquic_server_conn_mutex;
static std::unordered_map<HQUIC, std::shared_ptr<WebTransportValue>> g_msquic_server_conns;

static std::mutex g_listener_config_mutex;
static std::unordered_map<HQUIC, HQUIC> g_listener_config_map;

struct MsquicWebTransportBackend::Impl {
    const QUIC_API_TABLE* api = nullptr;
    HQUIC registration = nullptr;
    bool initialized = false;

    Impl() {
        if (QUIC_FAILED(MsQuicOpen2(&api))) {
            api = nullptr;
            return;
        }
        QUIC_REGISTRATION_CONFIG regConfig = { "claw", QUIC_EXECUTION_PROFILE_LOW_LATENCY };
        if (QUIC_FAILED(api->RegistrationOpen(&regConfig, &registration))) {
            MsQuicClose(api);
            api = nullptr;
            registration = nullptr;
            return;
        }
        initialized = true;
    }

    ~Impl() {
        if (registration) {
            api->RegistrationClose(registration);
        }
        if (api) {
            MsQuicClose(api);
        }
    }
};

// Connection callback: forwards to the WebTransportValue context
static QUIC_STATUS QUIC_API msquicConnectionCallback(
    _In_ HQUIC connection,
    _In_opt_ void* context,
    _Inout_ QUIC_CONNECTION_EVENT* event
) {
    (void)connection;
    WebTransportValue* wt = static_cast<WebTransportValue*>(context);
    if (!wt) return QUIC_STATUS_SUCCESS;

    switch (event->Type) {
        case QUIC_CONNECTION_EVENT_CONNECTED:
            wt->connected = true;
            wt->msquic_connecting = false;
            wt->cv.notify_all();
            if (wt->connect_future && wt->runtime) {
                auto* rt = static_cast<claw::vm::VMRuntime*>(wt->runtime);
                wt->connect_future->is_resolved = true;
                if (rt->on_future_resolved) {
                    rt->on_future_resolved(wt->connect_future);
                }
                if (rt->pending_futures.load() > 0) {
                    rt->pending_futures--;
                }
                wt->connect_future = nullptr;
            }
            // Server-side: add to server's pending_connections queue
            if (wt->parent_conn) {
                auto server = wt->parent_conn;
                std::shared_ptr<WebTransportValue> conn_sp;
                {
                    std::lock_guard<std::mutex> lock(g_msquic_server_conn_mutex);
                    auto it = g_msquic_server_conns.find(static_cast<HQUIC>(wt->msquic_connection));
                    if (it != g_msquic_server_conns.end()) {
                        conn_sp = it->second;
                    }
                }
                if (conn_sp) {
                    std::lock_guard<std::mutex> lock(server->server_mutex);
                    server->pending_connections.push_back(conn_sp);
                    server->server_cv.notify_all();
                }
            }
            break;
        case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
            wt->closed = true;
            wt->connected = false;
            wt->msquic_connecting = false;
            wt->cv.notify_all();
            if (wt->connect_future && wt->runtime) {
                auto* rt = static_cast<claw::vm::VMRuntime*>(wt->runtime);
                wt->connect_future->is_resolved = true;
                wt->connect_future->resolved_value = claw::vm::Value::nil();
                if (rt->on_future_resolved) {
                    rt->on_future_resolved(wt->connect_future);
                }
                if (rt->pending_futures.load() > 0) {
                    rt->pending_futures--;
                }
                wt->connect_future = nullptr;
            }
            {
                std::lock_guard<std::mutex> lock(g_msquic_server_conn_mutex);
                g_msquic_server_conns.erase(static_cast<HQUIC>(wt->msquic_connection));
            }
            break;
        case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED: {
            // Server-initiated stream: not handled in this minimal client
            // Accept and close immediately to avoid leaks
            if (event->PEER_STREAM_STARTED.Stream) {
                const QUIC_API_TABLE* api = static_cast<const QUIC_API_TABLE*>(wt->backend_api);
                if (api) {
                    api->StreamClose(event->PEER_STREAM_STARTED.Stream);
                }
            }
            break;
        }
        default:
            break;
    }
    return QUIC_STATUS_SUCCESS;
}

// Stream callback: handles receive and send-complete events
static QUIC_STATUS QUIC_API msquicStreamCallback(
    _In_ HQUIC stream,
    _In_opt_ void* context,
    _Inout_ QUIC_STREAM_EVENT* event
) {
    (void)stream;
    WebTransportValue* wt = static_cast<WebTransportValue*>(context);
    if (!wt) return QUIC_STATUS_SUCCESS;

    switch (event->Type) {
        case QUIC_STREAM_EVENT_RECEIVE: {
            std::lock_guard<std::mutex> lock(wt->queue_mutex);
            for (uint64_t i = 0; i < event->RECEIVE.BufferCount; ++i) {
                const QUIC_BUFFER* buf = &event->RECEIVE.Buffers[i];
                if (buf->Length > 0 && buf->Buffer) {
                    wt->incoming_queue.emplace_back(
                        reinterpret_cast<const char*>(buf->Buffer),
                        buf->Length
                    );
                }
            }
            wt->cv.notify_all();
            break;
        }
        case QUIC_STREAM_EVENT_SEND_COMPLETE:
            // Send done; nothing special needed for sync model
            break;
        case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
            wt->msquic_send_shutdown = true;
            wt->cv.notify_all();
            break;
        case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
            wt->msquic_send_shutdown = true;
            wt->cv.notify_all();
            break;
        default:
            break;
    }
    return QUIC_STATUS_SUCCESS;
}

static bool parseUrl(const std::string& url, std::string& host, uint16_t& port) {
    // Strip scheme prefixes
    size_t pos = 0;
    if (url.find("quic://") == 0) pos = 7;
    else if (url.find("https://") == 0) pos = 8;
    else if (url.find("http://") == 0) pos = 7;

    std::string rest = url.substr(pos);
    size_t colon = rest.find(':');
    if (colon != std::string::npos) {
        host = rest.substr(0, colon);
        port = static_cast<uint16_t>(std::stoi(rest.substr(colon + 1)));
    } else {
        host = rest;
        port = 443;
    }
    return !host.empty();
}

MsquicWebTransportBackend::MsquicWebTransportBackend()
    : impl_(std::make_unique<Impl>()) {}

MsquicWebTransportBackend::~MsquicWebTransportBackend() = default;

std::shared_ptr<WebTransportValue> MsquicWebTransportBackend::connect(const std::string& url) {
    auto wt = std::make_shared<WebTransportValue>();
    wt->url = url;

    if (!impl_->initialized) {
        // Msquic not available: fall back to mock semantics but mark as failed
        wt->connected = false;
        wt->closed = true;
        return wt;
    }

    std::string host;
    uint16_t port = 443;
    if (!parseUrl(url, host, port)) {
        wt->connected = false;
        wt->closed = true;
        return wt;
    }

    const QUIC_API_TABLE* api = impl_->api;
    HQUIC registration = impl_->registration;

    // ALPN
    const char* alpn = "claw-wt";
    QUIC_BUFFER alpnBuffer = { static_cast<uint32_t>(std::strlen(alpn)), const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(alpn)) };

    // Configuration
    HQUIC configuration = nullptr;
    if (QUIC_FAILED(api->ConfigurationOpen(registration, &alpnBuffer, 1, nullptr, 0, nullptr, &configuration))) {
        wt->closed = true;
        return wt;
    }

    // Client credentials: no certificate validation for simplicity
    QUIC_CREDENTIAL_CONFIG credConfig = {};
    credConfig.Type = QUIC_CREDENTIAL_TYPE_NONE;
    credConfig.Flags = QUIC_CREDENTIAL_FLAG_CLIENT | QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;

    if (QUIC_FAILED(api->ConfigurationLoadCredential(configuration, &credConfig))) {
        api->ConfigurationClose(configuration);
        wt->closed = true;
        return wt;
    }

    // Connection
    HQUIC connection = nullptr;
    if (QUIC_FAILED(api->ConnectionOpen(registration, msquicConnectionCallback, wt.get(), &connection))) {
        api->ConfigurationClose(configuration);
        wt->closed = true;
        return wt;
    }

    // Set configuration on connection before starting
    if (QUIC_FAILED(api->ConnectionSetConfiguration(connection, configuration))) {
        api->ConnectionClose(connection);
        api->ConfigurationClose(configuration);
        wt->closed = true;
        return wt;
    }

    wt->msquic_connection = connection;
    wt->msquic_connecting = true;

    // Start connection (pass configuration directly)
    if (QUIC_FAILED(api->ConnectionStart(connection, configuration, QUIC_ADDRESS_FAMILY_UNSPEC, host.c_str(), port))) {
        wt->msquic_connecting = false;
        wt->closed = true;
        api->ConnectionClose(connection);
        api->ConfigurationClose(configuration);
        wt->msquic_connection = nullptr;
        return wt;
    }

    // Open a default bidirectional stream immediately (starts after connect)
    HQUIC stream = nullptr;
    if (QUIC_SUCCEEDED(api->StreamOpen(connection, QUIC_STREAM_OPEN_FLAG_NONE, msquicStreamCallback, wt.get(), &stream))) {
        api->StreamStart(stream, QUIC_STREAM_START_FLAG_IMMEDIATE);
        wt->msquic_stream = stream;
    }

    // Connection is now async; caller will wait via future
    // Configuration handle can be closed after ConnectionSetConfiguration
    api->ConfigurationClose(configuration);
    return wt;
}

bool MsquicWebTransportBackend::send(const std::shared_ptr<WebTransportValue>& wt, const std::string& data) {
    if (!wt || wt->closed || !wt->msquic_stream) return false;
    const QUIC_API_TABLE* api = impl_->api;
    HQUIC stream = static_cast<HQUIC>(wt->msquic_stream);

    // QUIC_BUFFER must outlive the async send; allocate on heap and free in callback
    auto* buffer = new QUIC_BUFFER;
    buffer->Length = static_cast<uint32_t>(data.size());
    buffer->Buffer = new uint8_t[data.size()];
    std::memcpy(buffer->Buffer, data.data(), data.size());

    QUIC_STATUS status = api->StreamSend(stream, buffer, 1, QUIC_SEND_FLAG_NONE, buffer);
    if (QUIC_FAILED(status)) {
        delete[] buffer->Buffer;
        delete buffer;
        return false;
    }
    return true;
}

std::string MsquicWebTransportBackend::recv(const std::shared_ptr<WebTransportValue>& wt, int timeout_ms) {
    if (!wt || wt->closed) return "";
    std::unique_lock<std::mutex> lock(wt->queue_mutex);
    auto pred = [wt]() { return !wt->incoming_queue.empty() || wt->closed || wt->msquic_send_shutdown; };
    if (timeout_ms > 0) {
        wt->cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), pred);
    } else {
        wt->cv.wait(lock, pred);
    }
    if (!wt->incoming_queue.empty()) {
        std::string msg = wt->incoming_queue.front();
        wt->incoming_queue.pop_front();
        return msg;
    }
    return "";
}

bool MsquicWebTransportBackend::close(const std::shared_ptr<WebTransportValue>& wt) {
    if (!wt) return false;
    const QUIC_API_TABLE* api = impl_->api;
    if (wt->msquic_stream) {
        api->StreamClose(static_cast<HQUIC>(wt->msquic_stream));
        wt->msquic_stream = nullptr;
    }
    if (wt->msquic_connection) {
        api->ConnectionShutdown(static_cast<HQUIC>(wt->msquic_connection), QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
        api->ConnectionClose(static_cast<HQUIC>(wt->msquic_connection));
        wt->msquic_connection = nullptr;
    }
    wt->closed = true;
    wt->connected = false;
    wt->cv.notify_all();
    return true;
}

bool MsquicWebTransportBackend::ready(const std::shared_ptr<WebTransportValue>& wt) const {
    return wt && wt->connected && !wt->closed && wt->msquic_connection != nullptr;
}

std::shared_ptr<WebTransportValue> MsquicWebTransportBackend::open_stream(
    const std::shared_ptr<WebTransportValue>& wt, bool bidirectional) {
    if (!wt || !wt->msquic_connection || wt->closed) return nullptr;
    const QUIC_API_TABLE* api = impl_->api;
    HQUIC connection = static_cast<HQUIC>(wt->msquic_connection);

    QUIC_STREAM_OPEN_FLAGS flags = bidirectional ? QUIC_STREAM_OPEN_FLAG_NONE : QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL;
    HQUIC stream = nullptr;
    auto stream_val = std::make_shared<WebTransportValue>();
    stream_val->is_stream = true;
    stream_val->parent_conn = wt;

    if (QUIC_SUCCEEDED(api->StreamOpen(connection, flags, msquicStreamCallback, stream_val.get(), &stream))) {
        api->StreamStart(stream, QUIC_STREAM_START_FLAG_IMMEDIATE);
        stream_val->msquic_stream = stream;
        stream_val->connected = true;
    } else {
        return nullptr;
    }
    return stream_val;
}

bool MsquicWebTransportBackend::stream_send(
    const std::shared_ptr<WebTransportValue>& stream, const std::string& data) {
    if (!stream || stream->closed || !stream->msquic_stream) return false;
    const QUIC_API_TABLE* api = impl_->api;
    HQUIC hstream = static_cast<HQUIC>(stream->msquic_stream);

    auto* buffer = new QUIC_BUFFER;
    buffer->Length = static_cast<uint32_t>(data.size());
    buffer->Buffer = new uint8_t[data.size()];
    std::memcpy(buffer->Buffer, data.data(), data.size());

    QUIC_STATUS status = api->StreamSend(hstream, buffer, 1, QUIC_SEND_FLAG_NONE, buffer);
    if (QUIC_FAILED(status)) {
        delete[] buffer->Buffer;
        delete buffer;
        return false;
    }
    return true;
}

std::string MsquicWebTransportBackend::stream_recv(
    const std::shared_ptr<WebTransportValue>& stream, int timeout_ms) {
    if (!stream || stream->closed || !stream->msquic_stream) return "";
    std::unique_lock<std::mutex> lock(stream->queue_mutex);
    auto pred = [stream]() { return !stream->incoming_queue.empty() || stream->closed || stream->msquic_send_shutdown; };
    if (timeout_ms > 0) {
        stream->cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), pred);
    } else {
        stream->cv.wait(lock, pred);
    }
    if (!stream->incoming_queue.empty()) {
        std::string msg = stream->incoming_queue.front();
        stream->incoming_queue.pop_front();
        return msg;
    }
    return "";
}

bool MsquicWebTransportBackend::stream_close(
    const std::shared_ptr<WebTransportValue>& stream) {
    if (!stream || !stream->msquic_stream) return false;
    const QUIC_API_TABLE* api = impl_->api;
    api->StreamClose(static_cast<HQUIC>(stream->msquic_stream));
    stream->msquic_stream = nullptr;
    stream->closed = true;
    stream->connected = false;
    stream->cv.notify_all();
    return true;
}

// Listener callback: handles incoming connections
static QUIC_STATUS QUIC_API msquicListenerCallback(
    _In_ HQUIC listener,
    _In_opt_ void* context,
    _Inout_ QUIC_LISTENER_EVENT* event
) {
    (void)listener;
    WebTransportValue* server_raw = static_cast<WebTransportValue*>(context);
    if (!server_raw || !server_raw->backend_api) return QUIC_STATUS_SUCCESS;

    auto server = server_raw->self_weak.lock();
    if (!server) return QUIC_STATUS_SUCCESS;

    const QUIC_API_TABLE* api = static_cast<const QUIC_API_TABLE*>(server->backend_api);

    switch (event->Type) {
        case QUIC_LISTENER_EVENT_NEW_CONNECTION: {
            HQUIC connection = event->NEW_CONNECTION.Connection;
            if (!connection) break;

            auto conn_wt = std::make_shared<WebTransportValue>();
            conn_wt->msquic_connection = connection;
            conn_wt->backend = server->backend;
            conn_wt->backend_api = server->backend_api;
            conn_wt->parent_conn = server;
            conn_wt->msquic_connecting = true;

            // Set callback before configuration so handshake events are captured
            api->SetCallbackHandler(connection, (void*)msquicConnectionCallback, conn_wt.get());

            // Look up configuration from global map keyed by listener handle
            HQUIC configuration = nullptr;
            {
                std::lock_guard<std::mutex> lock(g_listener_config_mutex);
                auto it = g_listener_config_map.find(listener);
                if (it != g_listener_config_map.end()) {
                    configuration = it->second;
                }
            }
            if (configuration) {
                api->ConnectionSetConfiguration(connection, configuration);
            }

            // Store in global map so the connection callback can retrieve shared_ptr
            {
                std::lock_guard<std::mutex> lock(g_msquic_server_conn_mutex);
                g_msquic_server_conns[connection] = conn_wt;
            }
            break;
        }
        case QUIC_LISTENER_EVENT_STOP_COMPLETE:
            server->closed = true;
            server->server_cv.notify_all();
            break;
        default:
            break;
    }
    return QUIC_STATUS_SUCCESS;
}

std::shared_ptr<WebTransportValue> MsquicWebTransportBackend::listen(
    const std::string& address) {
    if (!impl_->initialized) return nullptr;

    const QUIC_API_TABLE* api = impl_->api;
    HQUIC registration = impl_->registration;

    std::string host;
    uint16_t port = 443;
    if (!parseUrl(address, host, port)) {
        host = "0.0.0.0";
        port = 4433;
    }

    // ALPN
    const char* alpn = "claw-wt";
    QUIC_BUFFER alpnBuffer = { static_cast<uint32_t>(std::strlen(alpn)), const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(alpn)) };

    // Server configuration
    HQUIC configuration = nullptr;
    if (QUIC_FAILED(api->ConfigurationOpen(registration, &alpnBuffer, 1, nullptr, 0, nullptr, &configuration))) {
        return nullptr;
    }

    // Server credentials: self-signed / none for development
    QUIC_CREDENTIAL_CONFIG credConfig = {};
    credConfig.Type = QUIC_CREDENTIAL_TYPE_NONE;
    credConfig.Flags = QUIC_CREDENTIAL_FLAG_NONE;

    if (QUIC_FAILED(api->ConfigurationLoadCredential(configuration, &credConfig))) {
        api->ConfigurationClose(configuration);
        return nullptr;
    }

    auto server = std::make_shared<WebTransportValue>();
    server->url = address;
    server->is_server = true;
    server->backend = this;
    server->backend_api = const_cast<void*>(static_cast<const void*>(api));
    server->self_weak = server;

    // Create listener
    HQUIC listener = nullptr;
    QUIC_ADDR addr = {};
    addr.Ip.sa_family = QUIC_ADDRESS_FAMILY_INET;
    addr.Ipv4.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &addr.Ipv4.sin_addr) != 1) {
        addr.Ipv4.sin_addr.s_addr = INADDR_ANY;
    }

    if (QUIC_FAILED(api->ListenerOpen(registration, msquicListenerCallback, server.get(), &listener))) {
        api->ConfigurationClose(configuration);
        return nullptr;
    }

    if (QUIC_FAILED(api->ListenerStart(listener, &alpnBuffer, 1, &addr))) {
        api->ListenerClose(listener);
        api->ConfigurationClose(configuration);
        return nullptr;
    }

    server->msquic_listener = listener;
    {
        std::lock_guard<std::mutex> lock(g_listener_config_mutex);
        g_listener_config_map[listener] = configuration;
    }

    return server;
}

std::shared_ptr<WebTransportValue> MsquicWebTransportBackend::accept(
    const std::shared_ptr<WebTransportValue>& server, int timeout_ms) {
    if (!server || !server->is_server) return nullptr;

    std::unique_lock<std::mutex> lock(server->server_mutex);
    auto pred = [server]() { return !server->pending_connections.empty() || server->closed; };
    if (timeout_ms > 0) {
        server->server_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), pred);
    } else {
        server->server_cv.wait(lock, pred);
    }
    if (!server->pending_connections.empty()) {
        auto conn = server->pending_connections.front();
        server->pending_connections.pop_front();
        return conn;
    }
    return nullptr;
}

bool MsquicWebTransportBackend::close_server(
    const std::shared_ptr<WebTransportValue>& server) {
    if (!server || !server->is_server) return false;
    const QUIC_API_TABLE* api = impl_->api;

    // Stop and close listener
    if (server->msquic_listener) {
        HQUIC listener = static_cast<HQUIC>(server->msquic_listener);
        api->ListenerStop(listener);
        api->ListenerClose(listener);
        server->msquic_listener = nullptr;
    }

    server->closed = true;
    server->connected = false;
    server->server_cv.notify_all();
    return true;
}

#endif // CLAW_ENABLE_WEBTRANSPORT

} // namespace vm
} // namespace claw
