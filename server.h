
#pragma once

#include <array>
#include <iostream>
#include <vector>

#include <netdb.h>
#include <netinet/in.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

#include <netdb.h>
#include <unistd.h>
#include <cstdlib>

constexpr auto kMaxConnections = 1024;

struct Connection {
    static const auto kReadBufferSize{1024};

   public:
    void processEvent(const struct kevent& event) {
        assert(event.ident == fd);
        if (event.flags & EV_EOF || event.flags & EV_ERROR) {
            // TODO
            close(event.ident);
            // TODO
            // delete static_cast<Connection*>(event.udata);
            std::cout << "Connection closed!" << std::endl;
        }
        switch (event.filter) {
            case EVFILT_READ: {
                auto bytes_available_to_read = event.data;
                assert(fd == event.ident);
                // TODO
                assert(bytes_available_to_read <= kReadBufferSize);
                int rc = read(fd, buffer, bytes_available_to_read);
                if (rc == -1) {
                    close(event.ident);
                } else {
                    has_data = true;
                    num_bytes_in_buffer = bytes_available_to_read;
                }
            } break;
            case EVFILT_WRITE: {
                auto bytes_available_to_write = event.data;
                assert(bytes_available_to_write > 0);
                if (has_data) {
                    assert(bytes_available_to_write >= num_bytes_in_buffer);
                    int rc = write(fd, buffer, num_bytes_in_buffer);
                    assert(rc != -1);  // TODO
                    has_data = false;
                    num_bytes_in_buffer = 0;
                }
            } break;
            default:
                assert(false);
        }
    }

    int fd;
    char buffer[kReadBufferSize];
    bool has_data{false};
    std::uint64_t num_bytes_in_buffer{0};
};

// struct KernelEventListener {
//    int _kernelQueue;
//};

struct Server {
    const char* host;
    int port;
    int listening_socket_fd;
    int num_current_connections;
    Connection connections[kMaxConnections];
};

struct Client {
    const char* host;
    int port;

    const char* server_host;
    int server_port;

    int socket_fd;
    Connection server_connection;
};

template <typename Callable>
class defer {
   public:
    defer(Callable&& callable) : _callable(std::move(callable)) {}
    ~defer() noexcept { _callable(); };

   private:
    Callable _callable;
};

int tcpbind(const char* host, int port) {
    struct hostent* hp;
    if ((hp = gethostbyname(host)) == NULL) diep("gethostbyname()");

    int sckfd;
    if ((sckfd = socket(PF_INET, SOCK_STREAM, 0)) < 0) diep("socket()");

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr = *((struct in_addr*)hp->h_addr);
    memset(&(server.sin_zero), 0, 8);

    int on = 1;
    setsockopt(sckfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    if (bind(sckfd, (struct sockaddr*)&server, sizeof(server)) == -1) {
        perror("bind");
        return 1;
    }
    // TODO handle backlog in stream
    assert(listen(sckfd, 5) != -1);

    std::cout << "sckfd: " << sckfd << std::endl;

    return sckfd;
}

int serverMain() {
    const auto host = "0.0.0.0";
    const auto port = 8000;

    // Set up socket to listen for connections.
    int listening_socket_fd = tcpbind(host, port);

    int kernelQueue = kqueue();
    defer closeQueue([&] { close(kernelQueue); });

    struct timespec timeout = {0, 0};
    int num_changes = 0;

    // TODO make statically allocated array
    std::vector<struct kevent> change_list;
    // Tell the kernel to listen for READ operations on the listener socket.
    change_list.emplace_back();
    EV_SET(&change_list[0], listening_socket_fd, EVFILT_READ,
           EV_ADD | EV_ENABLE, 0, 0, 0);

    // Main event loop
    while (true) {
        constexpr int kNumEventsPerCycle = 128;
        std::array<struct kevent, kNumEventsPerCycle> events;

        int nev = kevent(kernelQueue, change_list.data(), change_list.size(),
                         &events[0], kNumEventsPerCycle, &timeout);
        change_list.resize(0);

        assert(nev >= 0);
        assert(nev <= kNumEventsPerCycle);
        // TODO Max changes
        // Loop over all events
        for (auto i = 0; i < nev; ++i) {
            const auto& event = events.at(i);
            // TODO

            // Handle new connections
            if (event.ident == listening_socket_fd) {
                assert(!(event.flags & EV_EOF || event.flags & EV_ERROR));
                // New connection
                assert(event.filter == EVFILT_READ);
                std::cout << "Received new connection!" << std::endl;
                struct sockaddr_in cliaddr;
                socklen_t cliaddrlen = sizeof(cliaddr);
                Connection* conn = new Connection;  // TODO heap alloc
                conn->fd = accept(listening_socket_fd,
                                  reinterpret_cast<struct sockaddr*>(&cliaddr),
                                  &cliaddrlen);

                change_list.emplace_back();
                // TODO consider EV_CLEAR/edge-triggered
                EV_SET(&change_list.back(), conn->fd, EVFILT_READ,
                       EV_ADD | EV_ENABLE, 0, 0, (void*)conn);

                change_list.emplace_back();
                EV_SET(&change_list.back(), conn->fd, EVFILT_WRITE,
                       EV_ADD | EV_ENABLE, 0, 0, (void*)conn);

                std::cout << "Connection accepted!" << std::endl;
            } else {
                Connection* conn = static_cast<Connection*>(event.udata);
                // Update existing connection state
                conn->processEvent(event);
            }
        }
    }
}

void clientMain() {}
