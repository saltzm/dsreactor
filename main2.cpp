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
        }
    }

   private:
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

    auto head() { return _head; }

    auto tail() { return _tail; }

    template <typename Input>
    auto execute(Executor& executor, Input&& input) {
        auto x = _head(input);
        if constexpr (!std::is_null_pointer<Tail>::value) {
            executor.schedule([&executor, x = std::move(x), this] {
                _tail.execute(executor, std::move(x));
            });
        }
    }

    auto execute(Executor& executor) {
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

template <typename T>
class Future {
   public:
    void set(T t) {
        if (_state->_continuation) {
            auto& fn = *(_state->_continuation);
            fn(t);
        } else {
            _state->_value.emplace(std::move(t));
        }
    }

    template <typename Callable>
    auto then(Callable callable) {
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

template <typename T>
class Promise {
   public:
    void set(T t) { _future.set(t); }
    Future<T> getFuture() { return _future; }

   private:
    Future<T> _future;
};

template <typename>
struct IsList : public std::false_type {};

template <typename... T>
struct IsList<List<T...>> : public std::true_type {};

template <typename>
struct IsFuture : public std::false_type {};

template <typename T>
struct IsFuture<Future<T>> : public std::true_type {};

// TODO make all these cool and move-y and non leaky
template <typename CallableList, typename Input>
constexpr auto executeImpl(CallableList&& list, Executor& executor,
                           Input&& input) {
    if constexpr (std::is_void<decltype(list.head()(input))>::value) {
        list.head()(input);
        if constexpr (!std::is_null_pointer<decltype(list.tail())>::value) {
            executor.schedule(
                [list = std::forward<CallableList>(list), &executor]() mutable {
                    executeImpl(list.tail(), executor);
                });
        }
    } else {
        auto x = list.head()(input);
        if constexpr (!std::is_null_pointer<decltype(list.tail())>::value) {
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
    if constexpr (std::is_void<decltype(list.head()())>::value) {
        list.head()();
        if constexpr (!std::is_null_pointer<decltype(list.tail())>::value) {
            executor.schedule(
                [list = std::forward<CallableList>(list), &executor]() mutable {
                    executeImpl(list.tail(), executor);
                });
        }
    } else {
        auto x = list.head()();
        if constexpr (!std::is_null_pointer<decltype(list.tail())>::value) {
            executor.schedule([list = std::forward<CallableList>(list),
                               &executor, x = std::move(x)]() mutable {
                if constexpr (IsList<decltype(x)>::value) {
                    executeImpl(std::move(x).appendAll(list.tail()), executor);
                } else if constexpr (IsFuture<decltype(x)>::value) {
                    //           std::cout << "XXX chaining continuation 000" <<
                    //           std::endl;
                    x.then([list = std::forward<CallableList>(list),
                            &executor](auto val) mutable {
                        //               std::cout << "XXXXXXXXX 0" <<
                        //               std::endl;
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

template <typename T>
auto makeList(T&& t) {
    return List(std::forward<T>(t));
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
        _monitoredEventPromises.emplace_back();
        _eventsReceived.emplace_back();
        EV_SET(&_eventsToMonitor.back(), fd, int(type), EV_ADD | EV_ENABLE, 0,
               0, 0);
        return _monitoredEventPromises.back().getFuture();
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

                        _monitoredEventPromises[i].set(_eventsReceived[i]);
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
    std::vector<Promise<struct kevent>> _monitoredEventPromises;
    std::vector<struct kevent> _eventsReceived;
};

void doNetworkStuff(KernelEventListener& listener) {
    // auto sockfd = tcpopen("0.0.0.0", 8000);
    auto sockfd = fileno(stdin);

    auto dataReady =
        listener.subscribe(sockfd, KernelEventListener::EventType::kRead);

    dataReady.then([sockfd](struct kevent kev) {
        std::cout << "CLLABCK" << std::endl;
        const int kBufSize = 1024;
        char buf[kBufSize];

        /* We have data from the host */
        memset(buf, 0, kBufSize);
        //        if (read(sockfd, buf, kBufSize) < 0) diep("read()");
        fgets(buf, kBufSize, stdin);
        std::cout << std::string(buf, kBufSize) << std::endl;
        // fputs(buf, stdout);
    });
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

int main() {
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
    //    UserInputSubscriptionService inputService(executor);
    //    inputService.run();
    KernelEventListener keventListener(executor);
    keventListener.run();
    doNetworkStuff(keventListener);

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

    //    auto nestedChain =
    //        makeList([] { return 3; })
    //            .append([&](int i) {
    //                std::cout << "Second in chain, i = " << i << std::endl;
    //                return makeList([&] { return 5; }).append([&](int i) {
    //                    std::cout << "in nested thingie" << std::endl;
    //                    return makeList([] { return 10.0; }).append([&](float
    //                    x) {
    //                        std::cout << "in double nested thingie: x = " << x
    //                                  << std::endl;
    //                        return inputService.subscribe("hello");
    //                    });
    //                });
    //            })
    //            .append([](std::string s) {
    //                std::cout << s << std::endl;
    //                std::cout << "Third in nested chain" << std::endl;
    //                // TODO this should work w/ void return... works
    //                // in wandbox return 3;
    //            })
    //            .append([] { return 10; });
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
    //    //                // TODO this should work w/ void return... works in
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
    //    for (auto i = 0; i < 10; ++i) {
    //        //
    // execute(chain, executor);
    //        // execute(nestedChain, executor);
    //    }
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
