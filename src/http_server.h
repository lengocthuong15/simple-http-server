// Defines the HTTP server object with some constants and structs
// useful for request handling and improving performance

#ifndef HTTP_SERVER_H_
#define HTTP_SERVER_H_

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <atomic>

#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <utility>

#include "http_message.h"
#include "uri.h"
#include <storage.h>

namespace simple_http_server
{

    // Maximum size of an HTTP message is limited by how much bytes
    // we can read or send via socket each time
    constexpr size_t kMaxBufferSize = 4096;

    struct EventData
    {
        EventData() : fd(0), length(0), cursor(0), buffer() {}
        int fd;
        size_t length;
        size_t cursor;
        char buffer[kMaxBufferSize];
    };

    // A request handler should expect a request as argument and returns a response
    using HttpRequestHandler_t = std::function<HttpResponse(const HttpRequest &, Storage *)>;

    // The server consists of:
    // - 1 main thread
    // - 1 listener thread that is responsible for accepting new connections
    // - Possibly many threads that process HTTP messages and communicate with
    // clients via socket.
    //   The number of workers is defined by a constant
    class HttpServer
    {
    public:
        explicit HttpServer(const std::string &host, std::uint16_t port);
        ~HttpServer() = default;

        HttpServer() = default;
        HttpServer(HttpServer &&) = default;
        HttpServer &operator=(HttpServer &&) = default;

        void Start();
        void Stop();
        void RegisterHttpRequestHandler(const std::string &path, HttpMethod method,
                                        const HttpRequestHandler_t callback)
        {
            Uri uri(path);
            request_handlers_[uri].insert(std::make_pair(method, std::move(callback)));
        }
        void RegisterHttpRequestHandler(const Uri &uri, HttpMethod method,
                                        const HttpRequestHandler_t callback)
        {
            request_handlers_[uri].insert(std::make_pair(method, std::move(callback)));
        }

        std::string host() const { return host_; }
        std::uint16_t port() const { return port_; }
        bool running() const { return running_; }

    private:
        static constexpr int BACK_LOG_SIZE = 5000;
        static constexpr int MAX_EVENTS = 10000;
        static constexpr int THREAD_POOL_SIZE = 5;

        std::string host_;
        std::uint16_t port_;
        int sock_fd_;
        std::atomic_bool running_;
        std::thread listener_thread_;
        std::thread storage_watcher_;
        std::thread worker_threads_[THREAD_POOL_SIZE];
        int worker_epoll_fd_[THREAD_POOL_SIZE];
        epoll_event worker_events_[THREAD_POOL_SIZE][MAX_EVENTS];
        std::map<Uri, std::map<HttpMethod, HttpRequestHandler_t>> request_handlers_;
        Storage *storage;

        void CreateSocket();
        void SetUpEpoll();
        void Listen();
        void Watch_Storage();
        void setStorage(Storage *inStorage);
        void ProcessEvents(int worker_id);
        void HandleEpollEvent(int epoll_fd, EventData *event, std::uint32_t events);
        void HandleHttpData(const EventData &request, EventData *response);
        HttpResponse HandleHttpRequest(const HttpRequest &request);

        void controlEpollEvent(int epoll_fd, int op, int fd,
                                 std::uint32_t events = 0, void *data = nullptr);
    };

} // namespace simple_http_server

#endif // HTTP_SERVER_H_
