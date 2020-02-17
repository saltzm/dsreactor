#include <iostream>
#include <list>
#include <optional>
#include <queue>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <variant>

#include <netdb.h>
#include <netinet/in.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// enum class ErrorCode { Error };
//
// template <typename T>
// class ErrorOr {
//   public:
//    ErrorOr(T t) : _value(std::move(t)) {}
//    ErrorOr(ErrorCode ec) : _value(ec) {}
//
//    T get() {
//        // TODO throws
//        return std::get<T>(_value);
//    }
//
//    ErrorCode code() { return std::get<T>(_value); }
//
//    operator bool() { return std::holds_alternative<T>(_value); }
//
//   private:
//    std::variant<ErrorCode, T> _value;
//};
//
// class Socket {
//   public:
//    static ErrorOr<Socket> create() {
//        int fd = socket(PF_INET, SOCK_STREAM, 0);
//
//        auto port = 4000;
//        struct sockaddr_in* ipv4 = (struct sockaddr_in*)addr;
//        ipv4->sin_family = AF_INET;
//        ipv4->sin_addr.s_addr = htonl(INADDR_ANY);
//        ipv4->sin_port = htons((uint16_t)port);
//
//        if (fd == -1) {
//            return ErrorCode::Error;
//        } else {
//            return Socket(fd);
//        }
//    }
//
//   private:
//    Socket(int fd) : _fd(fd) {}
//
//    int _fd;
//};

class Executor {
    using Callable = std::function<void()>;

   public:
    ~Executor() {}

    void schedule(Callable&& callable) {
        queue.emplace_back(std::move(callable));
    }

    void run() {
        while (!queue.empty()) {
            auto& next = queue.front();
            try {
                next();
            } catch (...) {
            }
            queue.pop_front();
            ++_tasksExecuted;
            if (_tasksExecuted % 10000000 == 0) {
                // std::cout << "_tasksExecuted: " << _tasksExecuted <<
                // std::endl;
            }
        }
    }

   private:
    long long _tasksExecuted{0};
    std::list<Callable> queue;
};

template <typename Head, typename Tail = std::nullptr_t>
class List {
   public:
    constexpr List(Head h) : _head(std::move(h)) {}
    constexpr List(Head h, Tail t) : _head(std::move(h)), _tail(std::move(t)) {}

    template <typename T>
    constexpr auto prepend(T&& newHead) && {
        using ThisType = std::decay_t<decltype(*this)>;
        return List<T, ThisType>(std::move(newHead), std::move(*this));
    }

    template <typename T>
    constexpr auto append(T&& end) && {
        if constexpr (std::is_null_pointer<Tail>::value) {
            return List<Head, List<std::decay_t<T>>>(
                std::move(_head), List<std::decay_t<T>>(std::move(end)));
        } else {
            return std::move(_tail)
                .append(std::move(end))
                .prepend(std::move(_head));
        }
    }

    template <typename L>
    constexpr auto appendAll(L&& otherList) && {
        if constexpr (std::is_null_pointer<Tail>::value) {
            return std::move(otherList).prepend(std::move(_head));
        } else {
            return std::move(_tail).appendAll(otherList).prepend(
                std::move(_head));
        }
    }

    template <typename T, typename Callable>
    constexpr auto fold(T&& currentResult, Callable&& combiner) {
        if constexpr (std::is_null_pointer<Tail>::value) {
            return combiner(currentResult, _head);
        } else {
            return _tail.fold(combiner(currentResult, _head), combiner);
        }
    }

    template <typename Callable>
    constexpr auto fold(Callable&& combiner) {
        if constexpr (std::is_null_pointer<Tail>::value) {
            return _head;
        } else {
            return _tail.fold(_head, std::forward<Callable>(combiner));
        }
    }

    template <typename Callable>
    void forEach(Callable&& callable) {
        callable(_head);
        if constexpr (!std::is_null_pointer<Tail>::value) {
            _tail.forEach(std::forward<Callable>(callable));
        }
    }

    auto reverse() {
        if constexpr (std::is_null_pointer<Tail>::value) {
            return List(_head);
        } else {
            return _tail.reverse().append(_head);
        }
    }

    auto& head() { return _head; }

    auto& tail() { return _tail; }

    auto back() {
        if constexpr (std::is_null_pointer<Tail>::value) {
            return _head;
        } else {
            return _tail.back();
        }
    }

    template <typename Input>
    constexpr auto execute(Executor& executor, Input&& input) {
        auto x = _head(input);
        if constexpr (!std::is_null_pointer<Tail>::value) {
            executor.schedule([&executor, x = std::move(x), this] {
                _tail.execute(executor, std::move(x));
            });
        }
    }

    constexpr auto execute(Executor& executor) {
        auto x = _head();
        if constexpr (!std::is_null_pointer<Tail>::value) {
            executor.schedule([&executor, x = std::move(x), this] {
                _tail.execute(executor, std::move(x));
            });
        }
    }

    size_t size() {
        if constexpr (std::is_null_pointer<Tail>::value) {
            return 1;
        } else {
            return 1 + _tail.size();
        }
    }

   private:
    Head _head;
    Tail _tail;
};

template <typename>
struct IsList : public std::false_type {};

template <typename... T>
struct IsList<List<T...>> : public std::true_type {};

template <typename T>
class Future {
   public:
    using value_type = T;

    void set(T t) {
        if (_state->_continuation) {
            auto& fn = *(_state->_continuation);
            fn(t);
        } else {
            _state->_value.emplace(std::move(t));
        }
    }

    template <typename Callable>
    auto then(Callable&& callable) {
        if (_state->_value) {
            callable(*(_state->_value));
        } else {
            _state->_continuation.emplace(std::move(callable));
        }
    }

   private:
    struct State {
        std::optional<T> _value;
        std::optional<std::function<void(T)>> _continuation;
    };
    std::shared_ptr<State> _state{std::make_shared<State>()};
};

template <>
class Future<void> {
   public:
    void set() {
        if (_state->_continuation) {
            auto& fn = *(_state->_continuation);
            fn();
        } else {
            _state->_isReady = true;
        }
    }

    template <typename Callable>
    auto then(Callable&& callable) {
        if (_state->_isReady) {
            callable();
        } else {
            _state->_continuation.emplace(std::move(callable));
        }
    }

   private:
    struct State {
        bool _isReady{false};
        std::optional<std::function<void()>> _continuation;
    };
    std::shared_ptr<State> _state{std::make_shared<State>()};
};

template <typename T>
class Promise {
   public:
    void set(T t) { _future.set(t); }
    Future<T> getFuture() { return _future; }

   private:
    Future<T> _future;
};

template <>
class Promise<void> {
   public:
    void set() { _future.set(); }
    Future<void> getFuture() { return _future; }

   private:
    Future<void> _future;
};

template <typename>
struct IsFuture : public std::false_type {};

template <typename T>
struct IsFuture<Future<T>> : public std::true_type {};

template <typename T, typename Enable = void>
struct GetReturnTypeImpl {
    using type = T;
};

template <typename T>
struct GetReturnTypeImpl<T, std::enable_if_t<IsFuture<T>::value>> {
    using type = typename T::value_type;
};

template <typename T>
using GetReturnType = typename GetReturnTypeImpl<T>::type;

template <typename T>
auto makeList(T&& t) {
    return List(std::forward<T>(t));
}

// TODO make all these cool and move-y and non leaky
template <typename CallableList, typename Input>
constexpr auto executeImpl(CallableList&& list, Executor& executor,
                           Input&& input) {
    using TailType = std::decay_t<decltype(list.tail())>;

    if constexpr (std::is_void<decltype(list.head()(input))>::value) {
        list.head()(input);
        if constexpr (!std::is_null_pointer<TailType>::value) {
            executor.schedule(
                [list = std::forward<CallableList>(list), &executor]() mutable {
                    executeImpl(list.tail(), executor);
                });
        }
    } else {
        auto x = list.head()(input);
        if constexpr (!std::is_null_pointer<TailType>::value) {
            executor.schedule([list = std::forward<CallableList>(list),
                               &executor, x = std::move(x)]() mutable {
                if constexpr (IsList<decltype(x)>::value) {
                    executeImpl(std::move(x).appendAll(list.tail()), executor);
                } else if constexpr (IsFuture<decltype(x)>::value) {
                    // std::cout << "XXX chaining continuation" << std::endl;
                    x.then([list = std::forward<CallableList>(list),
                            &executor](auto val) mutable {
                        // std::cout << "XXXXXXXXX 1" << std::endl;
                        executeImpl(list.tail(), executor, std::move(val));
                    });
                } else {
                    executeImpl(list.tail(), executor, std::move(x));
                }
            });
        }
    }
}

template <typename CallableList>
constexpr auto executeImpl(CallableList&& list, Executor& executor) {
    using TailType = std::decay_t<decltype(list.tail())>;

    if constexpr (std::is_void<decltype(list.head()())>::value) {
        list.head()();
        if constexpr (!std::is_null_pointer<TailType>::value) {
            executor.schedule(
                [list = std::forward<CallableList>(list), &executor]() mutable {
                    executeImpl(list.tail(), executor);
                });
        }
    } else {
        auto x = list.head()();
        if constexpr (!std::is_null_pointer<TailType>::value) {
            executor.schedule([list = std::forward<CallableList>(list),
                               &executor, x = std::move(x)]() mutable {
                if constexpr (IsList<decltype(x)>::value) {
                    executeImpl(std::move(x).appendAll(list.tail()), executor);
                } else if constexpr (IsFuture<decltype(x)>::value) {
                    x.then([list = std::forward<CallableList>(list),
                            &executor](auto val) mutable {
                        executeImpl(list.tail(), executor, std::move(val));
                    });
                } else {
                    executeImpl(list.tail(), executor, std::move(x));
                }
            });
        }
    }
}

template <typename CallableList>
constexpr auto execute(CallableList&& list, Executor& executor) {
    executor.schedule(
        [list = std::forward<CallableList>(list), &executor]() mutable {
            executeImpl(std::move(list), executor);
        });
}

template <typename ComputationList, typename Result>
class Process {
   public:
    constexpr Process(ComputationList computationList)
        : _list(std::move(computationList)) {}

    template <typename T>
    constexpr auto then(T&& end) && {
        using FlatResult = GetReturnType<Result>;

        auto newComputationList = std::move(_list).append(end);
        return Process<decltype(newComputationList),
                       decltype(end(FlatResult()))>(
            std::move(newComputationList));
    }

    //    template <typename Input>
    //    auto execute(Executor& executor, Input&& input) {
    //        auto x = _head(input);
    //        if constexpr (!std::is_null_pointer<Tail>::value) {
    //            executor.schedule([&executor, x = std::move(x), this] {
    //                _tail.execute(executor, std::move(x));
    //            });
    //        }
    //    }
    //

    Future<Result> execute(Executor& executor) {
        Promise<Result> promise;
        auto fut = promise.getFuture();

        auto newList =
            std::move(_list).append([promise = std::move(promise)](
                                        auto& x) mutable { promise.set(x); });

        ::execute(newList, executor);
        return fut;
    }

   private:
    ComputationList _list;
};

template <typename Callable>
static constexpr auto makeProcess(Callable&& callable) {
    auto initList = List(std::forward<Callable>(callable));
    return Process<decltype(initList), decltype(callable())>(
        std::move(initList));
}

template <typename T>
void print(const T& t) {
    std::cout << t << std::endl;
}

template <typename Do, typename While, typename Then>
void loop(Executor& executor, Do&& fn, While&& condition, Then&& then) {
    fn();
    if (condition()) {
        executor.schedule([&executor, fn = std::forward<Do>(fn),
                           condition = std::forward<While>(condition),
                           then = std::forward<Then>(then)] {
            loop(executor, std::move(fn), std::move(condition),
                 std::move(then));
        });
    } else {
        then();
    }
}

template <typename Do>
constexpr void loop(Executor& executor, Do&& fn) {
    bool shouldContinue = fn();
    if (shouldContinue) {
        executor.schedule([&executor, fn = std::forward<Do>(fn)]() mutable {
            loop(executor, std::move(fn));
        });
    }
}

static std::queue<std::string> userInputQueue;
// static std::thread keyboardInputThread;
//
//

static std::optional<std::string> userInput;

void diep(const char* s) {
    perror(s);
    exit(EXIT_FAILURE);
}

int tcpopen(const char* host, int port) {
    struct sockaddr_in server;
    struct hostent* hp;
    int sckfd;

    if ((hp = gethostbyname(host)) == NULL) diep("gethostbyname()");

    if ((sckfd = socket(PF_INET, SOCK_STREAM, 0)) < 0) diep("socket()");

    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr = *((struct in_addr*)hp->h_addr);
    memset(&(server.sin_zero), 0, 8);

    if (connect(sckfd, (struct sockaddr*)&server, sizeof(struct sockaddr)) < 0)
        diep("connect()");

    // assert(listen(sckfd, 5) != -1);
    // struct sockaddr_in6 addr = {};
    // addr.sin6_len = sizeof(addr);
    // addr.sin6_family = AF_INET6;
    // addr.sin6_addr = in6addr_any;  //(struct in6_addr){}; // 0.0.0.0 / ::
    // addr.sin6_port = htons(9999);

    // int localFd = socket(addr.sin6_family, SOCK_STREAM, 0);
    // assert(localFd != -1);

    // int on = 1;
    // setsockopt(localFd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    // if (bind(localFd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
    //    perror("bind");
    //    return 1;
    //}
    // assert(listen(localFd, 5) != -1);
    std::cout << "sckfd: " << sckfd << std::endl;

    return sckfd;
}

int tcpbind(const char* host, int port) {
    struct sockaddr_in server;
    struct hostent* hp;
    int sckfd;

    if ((hp = gethostbyname(host)) == NULL) diep("gethostbyname()");

    if ((sckfd = socket(PF_INET, SOCK_STREAM, 0)) < 0) diep("socket()");

    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr = *((struct in_addr*)hp->h_addr);
    memset(&(server.sin_zero), 0, 8);

    // assert(listen(sckfd, 5) != -1);
    // struct sockaddr_in6 addr = {};
    // addr.sin6_len = sizeof(addr);
    // addr.sin6_family = AF_INET6;
    // addr.sin6_addr = in6addr_any;  //(struct in6_addr){}; // 0.0.0.0 / ::
    // addr.sin6_port = htons(9999);

    // int localFd = socket(addr.sin6_family, SOCK_STREAM, 0);
    // assert(localFd != -1);

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

void sendbuftosck(int sckfd, const char* buf, int len) {
    int bytessent, pos;

    pos = 0;
    do {
        if ((bytessent = send(sckfd, buf + pos, len - pos, 0)) < 0)
            diep("send()");
        pos += bytessent;
    } while (bytessent > 0);
}

class UserInputSubscriptionService {
   public:
    UserInputSubscriptionService(Executor& e) : _executor(e) {}

    ~UserInputSubscriptionService() {
        if (_monitoringThread.joinable()) {
            _monitoringThread.join();
        }
    }

    Future<std::string> subscribe(std::string filter) {
        if (_subscribers.find(filter) == _subscribers.end()) {
            _subscribers[filter] = {};
        }

        Promise<std::string> p;
        auto f = p.getFuture();
        _subscribers[filter].emplace_back(std::move(p));
        return f;
    }

    void run() {
        _monitoringThread = std::thread([this] {
            while (!_shutdown) {
                std::string input;
                std::cin >> input;
                std::lock_guard lk(_mutex);
                _userInputQueue.push(input);
            }
        });

        loop(_executor, [this]() mutable {
            std::unique_lock lk(_mutex);
            if (_userInputQueue.size() > 0) {
                auto next = _userInputQueue.front();
                lk.unlock();
                if (next == "die") {
                    _shutdown = true;
                    _monitoringThread.join();
                    return false;
                }

                _userInputQueue.pop();
                std::vector<std::string> filtersToRemove;
                for (auto& [k, v] : _subscribers) {
                    if (next.find(k) != std::string::npos) {
                        for (auto& p : v) {
                            p.set(next);
                        }
                        filtersToRemove.push_back(k);
                    }
                }

                for (auto& filter : filtersToRemove) {
                    _subscribers.erase(filter);
                }
            }
            return true;
        });
    }

   private:
    Executor& _executor;
    bool _shutdown{false};
    std::unordered_map<std::string, std::vector<Promise<std::string>>>
        _subscribers;

    std::thread _monitoringThread;

    // Protects input queue
    std::mutex _mutex;
    std::queue<std::string> _userInputQueue;
};

class KernelEventListener {
   public:
    KernelEventListener(Executor& e) : _executor(e), _kernelQueue(kqueue()) {
        assert(_kernelQueue != -1);
    }
    ~KernelEventListener() {
        /* Close kqueue */
        if (close(_kernelQueue) == -1) diep("close()");
    }

    enum class EventType : int {
        kRead = EVFILT_READ,
        kWrite = EVFILT_WRITE,
    };

    Future<struct kevent> subscribe(int fd, EventType type) {
        _eventsToMonitor.emplace_back();
        _monitoredEventPromises[fd];
        _eventsReceived.emplace_back();
        EV_SET(&_eventsToMonitor.back(), fd, int(type), EV_ADD | EV_ENABLE, 0,
               0, 0);
        return _monitoredEventPromises[fd].getFuture();
    }

    void run() {
        // Just poll - never block.
        struct timespec timeout = {0, 0};
        loop(_executor, [this, timeout]() mutable {
            // std::cout << "LOOP: " << _eventsToMonitor.size() << std::endl;

            if (_eventsToMonitor.size() > 0) {
                // std::cout << "about to block on kevent" << std::endl;

                int nev =
                    kevent(_kernelQueue, _eventsToMonitor.data(),
                           _eventsToMonitor.size(), _eventsReceived.data(),
                           _eventsReceived.size(), &timeout);

                // std::cout << "done blocking on kevent" << std::endl;

                if (nev < 0) {
                    diep("kevent()");
                } else if (nev > 0) {
                    for (auto i = 0; i < nev; i++) {
                        if (_eventsReceived[i].flags & EV_EOF ||
                            _eventsReceived[i].flags & EV_ERROR) {
                            /* Report errors */
                            fprintf(stderr, "EV_ERROR: %s\n",
                                    strerror(_eventsReceived[i].data));
                            exit(EXIT_FAILURE);
                        }
                        std::cout << "_monitoredEventPromises.size(): "
                                  << _monitoredEventPromises.size()
                                  << std::endl;

                        _monitoredEventPromises[_eventsReceived[i].ident].set(
                            _eventsReceived[i]);
                    }
                }
            }
            return true;
        });
    }

   private:
    Executor& _executor;

    int _kernelQueue;

    std::vector<struct kevent> _eventsToMonitor;
    std::unordered_map<int, Promise<struct kevent>> _monitoredEventPromises;
    std::vector<struct kevent> _eventsReceived;
};

void runServer(KernelEventListener& listener) {
    auto sockfd = tcpbind("0.0.0.0", 8000);

    listener.subscribe(sockfd, KernelEventListener::EventType::kRead)
        .then([&listener, sockfd](struct kevent kev) {
            std::cout << "Received new connection!" << std::endl;

            struct sockaddr_in cliaddr;
            socklen_t cliaddrlen = sizeof(cliaddr);

            int connfd =
                accept(sockfd, (struct sockaddr*)&cliaddr, &cliaddrlen);

            std::cout << "Connection accepted!" << std::endl;

            listener.subscribe(connfd, KernelEventListener::EventType::kRead)
                .then([connfd](struct kevent kev) {
                    const int kBufSize = 1024;
                    char buf[kBufSize];

                    // Read data from the client.
                    memset(buf, 0, kBufSize);
                    if (read(connfd, buf, kBufSize) < 0) diep("read()");
                    // Echo it back.
                    // TODO make also async
                    if (write(connfd, buf, kBufSize) < 0) diep("write()");

                    std::cout << "Client said: " << std::string(buf, kBufSize)
                              << std::endl;
                });
        });
}

using Buffer = std::vector<char>;
// class Buffer {
//   public:
//    Buffer(std::uint64_t size)
//        : _size(size), _data(malloc(size * sizeof(char))) {}
//
//    char* data() { return _data; }
//
//   private:
//    std::uint64_t _size;
//    char* _data;
//};

// template <typename T>
// class CircularBuffer {
//    public:
//     CircularBuffer(size_t size) : _data(size) {}
//
//     void push_back(T t) {
//         auto nextIndex = (_endIdx + 1) % data.size();
//         // TODO handle wrapping
//         assert(nextIndex < _startIdx);
//     }
//
//     void size() { return _endIdx - _startIdx + 1; }
//
//    private:
//     std::uint64_t _startIdx;
//     std::uint64_t _endIdx;
//     std::vector<T> _data;
// };

class TCPConnection {
   public:
    TCPConnection(KernelEventListener& listener, int socket)
        : _listener(listener), _socket(socket), _incomingMessages() {
        listener.subscribe(socket, KernelEventListener::EventType::kRead)
            .then([this, socket](const struct kevent& kev) {
                auto bytesReady = kev.data;
                Buffer buf(bytesReady);
                if (::read(socket, buf.data(), bytesReady) < 0) {
                    diep("read()");
                }
                if (_readWaiting) {
                    _readWaiting->set(std::move(buf));
                    _readWaiting.reset();
                } else {
                    _incomingMessages.emplace(std::move(buf));
                }
            });
    }

    Future<Buffer> read(std::uint64_t numBytes) {
        // If there's anything in _incomingMessages, pop it and return it
        // immediately.
        if (!_incomingMessages.empty()) {
            Promise<Buffer> promise;
            promise.set(std::move(_incomingMessages.front()));
            _incomingMessages.pop();
            return promise.getFuture();
        } else {
            _readWaiting.emplace();
            return _readWaiting->getFuture();
        }
    }

    // Future<void> write() {}

   private:
    KernelEventListener& _listener;
    int _socket;
    std::optional<Promise<Buffer>> _readWaiting;
    std::queue<Buffer> _incomingMessages;
};

void runClient(KernelEventListener& listener) {
    auto serverSocket = tcpopen("0.0.0.0", 8000);
    auto stdinfd = fileno(stdin);

    //    TCPConnection connection(serverSocket);

    //    loop(executor, [connection = std::move(connection)] {
    //        execute(makeList([] { return connection.read(); })
    //                    .append([](const Buffer& buf) {
    //                        if (write(serverSocket, buf, kBufSize) < 0)
    //                            diep("write()");
    //                    }),
    //                executor);
    //    });

    // A few useful things:
    //  1) On every event, call a callback. These do not have to synchronize
    //     with each other.
    //
    //     Example: Accepting connections on a socket. Processing those
    //     connections can happen concurrently with each other.
    //
    //  2) Read a sequential stream of data and do something for each data item.
    //     E.g. read from a connection and process each message in order.
    //
    //  3) Call a callback once on completion of an event. E.g. write a message
    //     when data is ready. Most of the time this could probably also be a
    //     stream/channel?

    // Read data from stdin and send it to the server.
    listener.subscribe(stdinfd, KernelEventListener::EventType::kRead)
        .then([&listener, serverSocket](struct kevent kev) {
            const int kBufSize = 1024;
            char buf[kBufSize];

            memset(buf, 0, kBufSize);

            if (read(fileno(stdin), buf, kBufSize) < 0) diep("read()");

            //            listener
            //                .onNextEvent(serverSocket,
            //                             KernelEventListener::EventType::kWrite)
            //                .then([serverSocket, buf, kBufSize] {
            if (write(serverSocket, buf, kBufSize) < 0) diep("write()");
            //                });
        });

    listener.subscribe(serverSocket, KernelEventListener::EventType::kRead)
        .then([serverSocket](struct kevent kev) {
            std::cout << "received response from server" << std::endl;
            const int kBufSize = 1024;
            char buf[kBufSize];

            // Read the response from the server.
            memset(buf, 0, kBufSize);
            if (read(serverSocket, buf, kBufSize) < 0) diep("read()");

            std::cout << std::string(buf, kBufSize) << std::endl;
        });
    // Ideal API:

    // tcpListener.openReadStream(serverSocket,
    // KernelEventListener::EventType::kRead)
    //    TCPListener::connect(serverSocket)
    //        .incoming()
    //        .forEach([](const Buffer& buf) {
    //            Process::create(executor)
    //                .then([]() {
    //                    auto msg = parse(buf);
    //                    return msg;
    //                })
    //                .then([](const Request& parsedRequest) {
    //                    return storageEngine.get(parsedRequest.key);
    //                })
    //                .then([](const Value& val) {
    //
    //                })
    //        });
}

void getKeyboardInputAsync() {
    std::thread t([] {
        std::cout << "Inside thread " << std::endl;
        while (true) {
            std::string input;
            std::cin >> input;
            std::cout << "Inside thread: got input " << std::endl;
            userInputQueue.push(input);
        }
    });
    t.detach();
}

Future<std::string> getKeyboardInput(Executor& e) {
    Promise<std::string> p;
    Future<std::string> f = p.getFuture();
    loop(e, [p]() mutable {
        // std::cout << "looping" << std::endl;
        if (userInput) {
            p.set(*userInput);
            return false;
        }
        return true;
    });
    return f;
}

int main(int argc, char** argv) {
    // getKeyboardInputAsync();
    //    auto keyboardInput = getKeyboardInput();
    //    keyboardInput.then([](std::string input) {
    //        std::cout << "got input! " << input << std::endl;
    //    });

    //    ReadyFuture<int> f(3);
    //    f.then(
    //        [](int i) { std::cout << "Did something with i: " << i <<
    //        std::endl; });
    //
    //    auto futWithCont = Future<int>().then(
    //        [](int i) { std::cout << "Did something with i: " << i <<
    //        std::endl; });
    //
    //    futWithCont.set(5);

    // futWith

    // List l(3);
    // auto fullList = makeList(3).prepend("hi").prepend(4.2);
    // std::cout << "fullList.size(): " << fullList.size() << std::endl;

    // fullList.forEach([](auto& x) { std::cout << x << std::endl; });

    // std::cout << "\n\n";

    // auto newList = makeList(4).append("hi").append(42);

    // newList.forEach([](auto& x) { std::cout << x << std::endl; });

    // std::cout << "\n\n";

    // auto listOfCallables =
    //    makeList([] { std::cout << "First!" << std::endl; })
    //        .append([] { std::cout << "Second!" << std::endl; })
    //        .append([] { std::cout << "Third!" << std::endl; });

    // listOfCallables.forEach([](auto& callable) { callable(); });
    // listOfCallables.reverse().forEach([](auto& callable) { callable(); });

    // std::cout << "\n\n";

    // auto listOfNumbers = makeList(3).append(4.2).append(20ull);
    // auto sum = listOfNumbers.fold(
    //    [](auto currentSum, auto next) { return currentSum + next; });

    // std::cout << "sum: " << sum << std::endl;

    // auto newListOfNumbersWithMoreNumbers =
    //    std::move(listOfNumbers).appendAll(makeList(5).append(6).append(7));
    // std::cout << "newListOfNumbersWithMoreNumbers.size(): "
    //          << newListOfNumbersWithMoreNumbers.size() << std::endl;
    // auto newSum = newListOfNumbersWithMoreNumbers.fold(
    //    [](auto currentSum, auto next) { return currentSum + next; });
    // std::cout << "newSum: " << newSum << std::endl;

    // std::cout << "\n\n";

    Executor executor;
    UserInputSubscriptionService inputService(executor);
    inputService.run();
    // KernelEventListener keventListener(executor);
    // keventListener.run();

    //    if (argc == 1) {
    //        runServer(keventListener);
    //    } else if (argc > 1) {
    //        std::cout << "Run client!" << std::endl;
    //        runClient(keventListener);
    //    }

    //    auto chain =
    //        makeList([&] {
    //            std::cout << "Process: Waiting to hear foo... " << std::endl;
    //            return inputService.subscribe("foo");
    //        })
    //            .append([&](std::string s) {
    //                std::cout << "Process: Saw input: " << s << std::endl;
    //                std::cout << "Process: Waiting to hear bar...: " <<
    //                std::endl; return inputService.subscribe("bar");
    //            })
    //            .append([](std::string s) {
    //                std::cout << "Process: Saw input: " << s << std::endl;
    //            });

    auto chain2 =
        // Processes are your "lazy futures"
        makeProcess([&] {
            std::cout << "Process: Waiting to hear foo... " << std::endl;
            // subscribe returns a Future<std::string> that gets flattened
            return inputService.subscribe("foo");
        })
            .then([&](std::string s) {
                std::cout << "Process: Saw input: " << s << std::endl;
                return makeProcess([&] {
                           std::cout << "Process: Waiting to hear bar...: "
                                     << std::endl;
                           return inputService.subscribe("bar");
                       })
                    .then([&](std::string s) {
                        std::cout << "Nested process: stuff " << std::endl;
                        return s;
                    })
                    .execute(executor);
            })
            .then([](std::string s) {
                std::cout << "Process: Saw input: " << s << std::endl;
                return 17;
            });
    for (auto i = 0; i < 10; ++i) {
        // "Future" is just a one-off event trigger thing that takes a single
        // continuation
        Future<int> fut = chain2.execute(executor);
        fut.then(
            [](int i) { std::cout << "Process result: " << i << std::endl; });
    }

    // auto nestedChain =
    //    makeList([] { return 3; })
    //        .append([&](int i) {
    //            std::cout << "Second in chain, i = " << i << std::endl;
    //            return makeList([&] { return 5; }).append([&](int i) {
    //                std::cout << "in nested thingie" << std::endl;
    //                return makeList([] { return 10.0;
    //                }).append([&](float x) {
    //                    std::cout << "in double nested thingie: x = " <<
    //                    x
    //                              << std::endl;
    //                    return inputService.subscribe("hello");
    //                });
    //            });
    //        })
    //        .append([](std::string s) {
    //            std::cout << s << std::endl;
    //            std::cout << "Third in nested chain" << std::endl;
    //            // TODO this should work w/ void return... works
    //            // in wandbox return 3;
    //        })
    //        .append([] { return 10; });
    //
    //    std::cout << "\n\n";
    //
    //    //    executor.schedule([&executor] {
    //    //        auto i = 3;
    //    //        executor.schedule([i, &executor] {
    //    //            std::cout << "X  Second in chain, i = " << i <<
    //    std::endl;
    //    //            auto s = std::string("i made it");
    //    //
    //    //            executor.schedule([s] {
    //    //                std::cout << s << std::endl;
    //    //                std::cout << "X Third in chain" << std::endl;
    //    //                // TODO this should work w/ void return...
    //    works in
    //    //                // wandbox
    //    //                // return 3;
    //    //            });
    //    //        });
    //    //    });
    //
    //    //    auto i = 0;
    //    // loop(executor,
    //    //     [&i] {
    //    //         std::cout << "looping" << std::endl;
    //    //         ++i;
    //    //     },
    //    //     [&i] { return i < 5; }, [] { std::cout << "moving on" <<
    //    std::endl;
    //    //     });
    //    //
    //    // for (auto i = 0; i < 10; ++i) {
    //    //    execute(chain, executor);
    //    //}
    //

    // for (auto i = 0; i < 10; ++i) {
    //        //
    //  execute(chain, executor);
    //        // execute(nestedChain, executor);
    // }
    //    auto i = 0;
    //

    std::cout << "Starting executor" << std::endl;
    executor.run();
    //    while (true) {
    //    }
    // sleep(10000);
    // keyboardInputThread.join();
    // std::cout << result << std::endl;
}
