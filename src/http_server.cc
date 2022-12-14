#include "http_server.h"

#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <functional>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

#include "http_message.h"
#include "uri.h"

namespace simple_http_server
{

    HttpServer::HttpServer(const std::string &host, std::uint16_t port)
        : host_(host),
          port_(port),
          sock_fd_(0),
          running_(false),
          worker_epoll_fd_()
    {
        CreateSocket();
        storage = new Storage({"/index.html", "/overview.png", "/local_file.png", "/local_ram.png", "/cpu_idle.png", "/cpu_loading.png"});
    }

    void HttpServer::Start()
    {
        int opt = 1;
        sockaddr_in server_address;

        if (setsockopt(sock_fd_, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
                       sizeof(opt)) < 0)
        {
            throw std::runtime_error("Failed to set socket options");
        }

        server_address.sin_family = AF_INET;
        server_address.sin_addr.s_addr = INADDR_ANY;
        inet_pton(AF_INET, host_.c_str(), &(server_address.sin_addr.s_addr));
        server_address.sin_port = htons(port_);

        if (bind(sock_fd_, (sockaddr *)&server_address, sizeof(server_address)) < 0)
        {
            throw std::runtime_error("Failed to bind to socket");
        }

        if (listen(sock_fd_, BACK_LOG_SIZE) < 0)
        {
            std::ostringstream msg;
            msg << "Failed to listen on port " << port_;
            throw std::runtime_error(msg.str());
        }

        SetUpEpoll();
        running_ = true;
        listener_thread_ = std::thread(&HttpServer::Listen, this);
        this->storage_watcher_ = std::thread(&HttpServer::Watch_Storage, this);
        for (int i = 0; i < THREAD_POOL_SIZE; i++)
        {
            worker_threads_[i] = std::thread(&HttpServer::ProcessEvents, this, i);
        }

        auto say_hello = [](const HttpRequest &request, Storage *storage) -> HttpResponse
        {
            HttpResponse response(HttpStatusCode::Ok);
            response.SetHeader("Content-Type", "text/plain");
            response.SetContent("Hello, world\n");
            return response;
        };
        auto send_html = [](const HttpRequest &request, Storage *inStorage) -> HttpResponse
        {
            HttpResponse response(HttpStatusCode::Ok);
            std::string path = request.uri().path();
            std::string content;
            inStorage->getResource(path, content);
            response.SetHeader("Content-Type", "text/html");
            response.SetContent(content);
            return response;
        };

        auto send_image = [](const HttpRequest &request, Storage *inStorage) -> HttpResponse
        {
            HttpResponse response(HttpStatusCode::Ok);
            std::string path = request.uri().path();
            std::string content;
            inStorage->getResource(path, content);
            response.SetHeader("Content-Type", "image/png");
            response.SetContent(content);
            return response;
        };

        this->RegisterHttpRequestHandler("/", HttpMethod::GET, say_hello);
        this->RegisterHttpRequestHandler("/index.html", HttpMethod::GET, send_html);
        this->RegisterHttpRequestHandler("/overview.png", HttpMethod::GET, send_image);
        this->RegisterHttpRequestHandler("/local_file.png", HttpMethod::GET, send_image);
        this->RegisterHttpRequestHandler("/local_ram.png", HttpMethod::GET, send_image);
        this->RegisterHttpRequestHandler("/cpu_idle.png", HttpMethod::GET, send_image);
        this->RegisterHttpRequestHandler("/cpu_loading.png", HttpMethod::GET, send_image);
    }

    void HttpServer::Stop()
    {
        running_ = false;
        listener_thread_.join();
        storage_watcher_.join();
        if (storage)
        {
            delete storage;
        }
        for (int i = 0; i < THREAD_POOL_SIZE; i++)
        {
            worker_threads_[i].join();
        }
        for (int i = 0; i < THREAD_POOL_SIZE; i++)
        {
            close(worker_epoll_fd_[i]);
        }
        close(sock_fd_);
    }

    void HttpServer::CreateSocket()
    {
        if ((sock_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) < 0)
        {
            throw std::runtime_error("Failed to create a TCP socket");
        }
    }

    void HttpServer::SetUpEpoll()
    {
        for (int i = 0; i < THREAD_POOL_SIZE; i++)
        {
            if ((worker_epoll_fd_[i] = epoll_create1(0)) < 0)
            {
                throw std::runtime_error(
                    "Failed to create epoll file descriptor for worker");
            }
        }
    }

    void HttpServer::setStorage(Storage *inStorage)
    {
        this->storage = inStorage;
    }

    void HttpServer::Watch_Storage()
    {
        while (running_)
        {
            sleep(5);
            if (storage) {
                this->storage->updateResource();
            }
        }
    }

    void HttpServer::Listen()
    {
        EventData *client_data;
        sockaddr_in client_address;
        socklen_t client_len = sizeof(client_address);
        int client_fd;
        int current_worker = 0;

        // accept new connections and distribute tasks to worker threads
        fd_set rfds;
        struct timeval tv;
        struct pollfd pfds[1];
        while (running_)
        {
            // FD_ZERO(&rfds);
            // FD_SET(sock_fd_, &rfds);
            // tv.tv_sec = 5;
            // tv.tv_usec = 0;
            // int retval = select(FD_SETSIZE, &rfds, NULL, NULL, &tv);
            // if (retval && FD_ISSET(sock_fd_, &rfds)) {
            pfds[0].fd = sock_fd_;
            pfds[0].events = POLLIN;
            poll(pfds, 1, 5);
            if (pfds[0].revents & POLLIN)
            {
                client_fd = accept4(sock_fd_, (sockaddr *)&client_address, &client_len,
                                    SOCK_NONBLOCK);
                if (client_fd < 0)
                    continue;

                client_data = new EventData();
                client_data->fd = client_fd;
                controlEpollEvent(worker_epoll_fd_[current_worker], EPOLL_CTL_ADD,
                                    client_fd, EPOLLIN, client_data);
                current_worker++;
                if (current_worker == HttpServer::THREAD_POOL_SIZE)
                    current_worker = 0;
            }
        }
    }

    void HttpServer::ProcessEvents(int worker_id)
    {
        EventData *data;
        int epoll_fd = worker_epoll_fd_[worker_id];
        while (running_)
        {
            int nfds = epoll_wait(worker_epoll_fd_[worker_id],
                                  worker_events_[worker_id], HttpServer::MAX_EVENTS, 5);
            if (nfds < 0)
            {
                continue;
            }

            for (int i = 0; i < nfds; i++)
            {
                const epoll_event &current_event = worker_events_[worker_id][i];
                data = reinterpret_cast<EventData *>(current_event.data.ptr);
                if ((current_event.events & EPOLLHUP) ||
                    (current_event.events & EPOLLERR))
                {
                    controlEpollEvent(epoll_fd, EPOLL_CTL_DEL, data->fd);
                    close(data->fd);
                    delete data;
                }
                else if ((current_event.events == EPOLLIN) ||
                         (current_event.events == EPOLLOUT))
                {
                    HandleEpollEvent(epoll_fd, data, current_event.events);
                }
                else
                { // something unexpected
                    controlEpollEvent(epoll_fd, EPOLL_CTL_DEL, data->fd);
                    close(data->fd);
                    delete data;
                }
            }
        }
    }

    void HttpServer::HandleEpollEvent(int epoll_fd, EventData *data,
                                      std::uint32_t events)
    {
        int fd = data->fd;
        EventData *request, *response;

        if (events == EPOLLIN)
        {
            request = data;
            char buffer[kMaxBufferSize];
            ssize_t byte_count = recv(fd, buffer, kMaxBufferSize, 0);
            request->buffer = buffer;
            if (byte_count > 0)
            { // we have fully received the message
                response = new EventData();
                response->fd = fd;
                HandleHttpData(*request, response);
                controlEpollEvent(epoll_fd, EPOLL_CTL_MOD, fd, EPOLLOUT, response);
                delete request;
            }
            else if (byte_count == 0)
            { // client has closed connection
                controlEpollEvent(epoll_fd, EPOLL_CTL_DEL, fd);
                close(fd);
                delete request;
            }
            else
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                { // retry
                    request->fd = fd;
                    controlEpollEvent(epoll_fd, EPOLL_CTL_MOD, fd, EPOLLIN, request);
                }
                else
                { // other error
                    controlEpollEvent(epoll_fd, EPOLL_CTL_DEL, fd);
                    close(fd);
                    delete request;
                }
            }
        }
        else
        {
            response = data;
            // auto len = response->buffer.length();
            // char* buffer = new char[len];
            ssize_t byte_count =
                send(fd, response->buffer.c_str() + response->cursor, response->length, 0);
            if (byte_count >= 0)
            {
                if (byte_count < response->length)
                { // there are still bytes to write
                    response->cursor += byte_count;
                    response->length -= byte_count;
                    controlEpollEvent(epoll_fd, EPOLL_CTL_MOD, fd, EPOLLOUT, response);
                }
                else
                { // we have written the complete message
                    request = new EventData();
                    request->fd = fd;
                    controlEpollEvent(epoll_fd, EPOLL_CTL_MOD, fd, EPOLLIN, request);
                    delete response;
                }
            }
            else
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                { // retry
                    controlEpollEvent(epoll_fd, EPOLL_CTL_ADD, fd, EPOLLOUT, response);
                }
                else
                { // other error
                    controlEpollEvent(epoll_fd, EPOLL_CTL_DEL, fd);
                    close(fd);
                    delete response;
                }
            }
        }
    }

    void HttpServer::HandleHttpData(const EventData &raw_request,
                                    EventData *raw_response)
    {
        std::string request_string(raw_request.buffer), response_string;
        HttpRequest http_request;
        HttpResponse http_response;

        try
        {
            http_request = stringToRequest(request_string);
            http_response = HandleHttpRequest(http_request);
        }
        catch (const std::invalid_argument &e)
        {
            http_response = HttpResponse(HttpStatusCode::BadRequest);
            http_response.SetContent(e.what());
        }
        catch (const std::logic_error &e)
        {
            http_response = HttpResponse(HttpStatusCode::HttpVersionNotSupported);
            http_response.SetContent(e.what());
        }
        catch (const std::exception &e)
        {
            http_response = HttpResponse(HttpStatusCode::InternalServerError);
            http_response.SetContent(e.what());
        }

        // Set response to write to client
        response_string =
            to_string(http_response, http_request.method() != HttpMethod::HEAD);
        std::size_t rpLen = response_string.length();
        // std::size_t  cpLen = rpLen > kMaxBufferSize ? kMaxBufferSize : rpLen;
        // memcpy(raw_response->buffer, response_string.c_str(), rpLen);
        raw_response->buffer = response_string;
        raw_response->length = rpLen;
    }

    HttpResponse HttpServer::HandleHttpRequest(const HttpRequest &request)
    {
        auto it = request_handlers_.find(request.uri());
        if (it == request_handlers_.end()) // this uri is not registered
        {
            HttpResponse response(HttpStatusCode::NotFound);
            response.SetHeader("Content-Type", "text/plain");
            response.SetContent("NOT FOUND\n");
            return response;
        }
        auto callback_it = it->second.find(request.method());
        if (callback_it == it->second.end())
        { // no handler for this method
            HttpResponse response(HttpStatusCode::MethodNotAllowed);
            response.SetHeader("Content-Type", "text/plain");
            response.SetContent("NOT ALLOWED\n");
            return response;
        }
        return callback_it->second(request, this->storage); // call handler to process the request
    }

    void HttpServer::controlEpollEvent(int epoll_fd, int op, int fd,
                                         std::uint32_t events, void *data)
    {
        if (op == EPOLL_CTL_DEL)
        {
            if (epoll_ctl(epoll_fd, op, fd, nullptr) < 0)
            {
                throw std::runtime_error("Failed to remove file descriptor");
            }
        }
        else
        {
            epoll_event ev;
            ev.events = events;
            ev.data.ptr = data;
            if (epoll_ctl(epoll_fd, op, fd, &ev) < 0)
            {
                throw std::runtime_error("Failed to add file descriptor");
            }
        }
    }

} // namespace simple_http_server
