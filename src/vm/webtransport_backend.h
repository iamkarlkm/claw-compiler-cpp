// WebTransport Backend Abstraction
// Provides mock (in-memory) and msquic (real QUIC) implementations.

#ifndef CLAW_WEBTRANSPORT_BACKEND_H
#define CLAW_WEBTRANSPORT_BACKEND_H

#include <memory>
#include <string>

namespace claw {
namespace vm {

struct WebTransportValue;

// ============================================================================
// Abstract Backend Interface
// ============================================================================
class WebTransportBackend {
public:
    virtual ~WebTransportBackend() = default;

    virtual std::shared_ptr<WebTransportValue> connect(const std::string& url) = 0;
    virtual bool send(const std::shared_ptr<WebTransportValue>& wt, const std::string& data) = 0;
    virtual std::string recv(const std::shared_ptr<WebTransportValue>& wt, int timeout_ms) = 0;
    virtual bool close(const std::shared_ptr<WebTransportValue>& wt) = 0;
    virtual bool ready(const std::shared_ptr<WebTransportValue>& wt) const = 0;

    // Stream multiplexing
    virtual std::shared_ptr<WebTransportValue> open_stream(const std::shared_ptr<WebTransportValue>& wt, bool bidirectional) = 0;
    virtual bool stream_send(const std::shared_ptr<WebTransportValue>& stream, const std::string& data) = 0;
    virtual std::string stream_recv(const std::shared_ptr<WebTransportValue>& stream, int timeout_ms) = 0;
    virtual bool stream_close(const std::shared_ptr<WebTransportValue>& stream) = 0;

    // Server-side
    virtual std::shared_ptr<WebTransportValue> listen(const std::string& address) = 0;
    virtual std::shared_ptr<WebTransportValue> accept(const std::shared_ptr<WebTransportValue>& server, int timeout_ms) = 0;
    virtual bool close_server(const std::shared_ptr<WebTransportValue>& server) = 0;
};

// ============================================================================
// Mock Backend - In-memory echo queue (no network I/O)
// ============================================================================
class MockWebTransportBackend : public WebTransportBackend {
public:
    std::shared_ptr<WebTransportValue> connect(const std::string& url) override;
    bool send(const std::shared_ptr<WebTransportValue>& wt, const std::string& data) override;
    std::string recv(const std::shared_ptr<WebTransportValue>& wt, int timeout_ms) override;
    bool close(const std::shared_ptr<WebTransportValue>& wt) override;
    bool ready(const std::shared_ptr<WebTransportValue>& wt) const override;

    std::shared_ptr<WebTransportValue> open_stream(const std::shared_ptr<WebTransportValue>& wt, bool bidirectional) override;
    bool stream_send(const std::shared_ptr<WebTransportValue>& stream, const std::string& data) override;
    std::string stream_recv(const std::shared_ptr<WebTransportValue>& stream, int timeout_ms) override;
    bool stream_close(const std::shared_ptr<WebTransportValue>& stream) override;

    std::shared_ptr<WebTransportValue> listen(const std::string& address) override;
    std::shared_ptr<WebTransportValue> accept(const std::shared_ptr<WebTransportValue>& server, int timeout_ms) override;
    bool close_server(const std::shared_ptr<WebTransportValue>& server) override;
};

#ifdef CLAW_ENABLE_WEBTRANSPORT
// ============================================================================
// Msquic Backend - Real QUIC network I/O via libmsquic
// ============================================================================
class MsquicWebTransportBackend : public WebTransportBackend {
public:
    MsquicWebTransportBackend();
    ~MsquicWebTransportBackend() override;

    std::shared_ptr<WebTransportValue> connect(const std::string& url) override;
    bool send(const std::shared_ptr<WebTransportValue>& wt, const std::string& data) override;
    std::string recv(const std::shared_ptr<WebTransportValue>& wt, int timeout_ms) override;
    bool close(const std::shared_ptr<WebTransportValue>& wt) override;
    bool ready(const std::shared_ptr<WebTransportValue>& wt) const override;

    std::shared_ptr<WebTransportValue> open_stream(const std::shared_ptr<WebTransportValue>& wt, bool bidirectional) override;
    bool stream_send(const std::shared_ptr<WebTransportValue>& stream, const std::string& data) override;
    std::string stream_recv(const std::shared_ptr<WebTransportValue>& stream, int timeout_ms) override;
    bool stream_close(const std::shared_ptr<WebTransportValue>& stream) override;

    std::shared_ptr<WebTransportValue> listen(const std::string& address) override;
    std::shared_ptr<WebTransportValue> accept(const std::shared_ptr<WebTransportValue>& server, int timeout_ms) override;
    bool close_server(const std::shared_ptr<WebTransportValue>& server) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
#endif // CLAW_ENABLE_WEBTRANSPORT

} // namespace vm
} // namespace claw

#endif // CLAW_WEBTRANSPORT_BACKEND_H
