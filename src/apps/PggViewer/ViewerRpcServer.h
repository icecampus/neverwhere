#pragma once

// TCP RPC server for PggViewer (--serve), stage A1 of
// docs/pgg/agent_tooling_plan.md. Line-delimited JSON with the same envelope
// as the map editor (docs/editor_rpc.md): a request is one line
//   {"op":"<name>","args":{...}}
// answered by one line
//   {"ok":true,"data":{...}}  |  {"ok":false,"error":{"kind":"...","message":"..."}}
// Command handlers are std::function callbacks registered by main.cpp and
// invoked from poll() on the GUI thread, so the server itself never touches
// viewer globals. POSIX sockets; WinSock2 under _WIN32. The listen socket and
// every client are non-blocking: poll() (called once per frame()) accepts new
// clients, drains recv buffers and dispatches complete lines.

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

class ViewerRpcServer {
public:
    static constexpr uint16_t kDefaultPort = 9878;

    // A handler answers with the response data, or with std::nullopt to defer
    // the reply (the two-phase render answers later from frame() via
    // reply/replyError with the clientId). Throwing via fail() answers
    // {"ok":false,"error":{kind,message}}.
    using Handler =
        std::function<std::optional<nlohmann::json>(uint64_t clientId, const nlohmann::json& args)>;

    ViewerRpcServer();
    ~ViewerRpcServer();  // stop()

    // Binds and listens on host:port (default 127.0.0.1:9878). false on
    // failure (port busy etc.) — the caller logs and continues without the
    // server, like the map editor does.
    bool start(const std::string& host = "127.0.0.1", uint16_t port = kDefaultPort);
    void stop();
    bool running() const;

    // Registers the handler of `op` (replaces an existing one).
    void on(const std::string& op, Handler handler);

    // Accepts new clients, reads available bytes, dispatches every complete
    // request line and flushes pending writes. Call once per frame.
    void poll();

    // Deferred answers (render phase 2). Silently dropped when the client
    // disconnected in the meantime.
    void reply(uint64_t clientId, const nlohmann::json& data);
    void replyError(uint64_t clientId, const std::string& kind, const std::string& message);

    // Shorthand for handlers: answers {"ok":false,"error":{kind,message}}.
    [[noreturn]] static void fail(const std::string& kind, const std::string& message);

private:
    struct RpcError {
        std::string kind;
        std::string message;
    };
    struct Client {
        uint64_t id = 0;
        uintptr_t fd = 0;  // socket handle (int on POSIX, SOCKET on WinSock)
        std::string inBuf;
        std::string outBuf;
    };

    void dispatch(Client& client, const std::string& line);
    void enqueue(Client& client, const nlohmann::json& response);
    void dropClient(uint64_t id);

    uintptr_t m_listenFd = 0;
    bool m_running = false;
    bool m_wsaUp = false;
    uint64_t m_nextClientId = 1;
    std::map<uint64_t, Client> m_clients;
    std::map<std::string, Handler> m_handlers;
};
