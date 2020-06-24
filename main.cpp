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
#include <unistd.h>
#include <cstdlib>

#include "kernel_event_listener.h"
#include "strand.h"
#include "user_input_subscription_service.h"

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

            const int kBufSize = 1024;
            struct Buffer {
                bool hasData{false};
                char data[kBufSize];
            };

            auto bytes = std::make_shared<int>();
            auto buffer = std::make_shared<Buffer>();

            listener.subscribe(connfd).then([buffer,
                                             connfd](struct kevent kev) {
                switch (
                    static_cast<KernelEventListener::EventType>(kev.filter)) {
                    case KernelEventListener::EventType::kRead: {
                        // std::cout << "received response from server"
                        //          << kev.filter << std::endl;

                        // Read the response from the server.
                        memset(buffer->data, 0, kBufSize);
                        if (read(connfd, buffer->data, kBufSize) < 0)
                            diep("read()");

                        buffer->hasData = true;
                    } break;
                    case KernelEventListener::EventType::kWrite: {
                        // std::cout << "can WRITE" << kev.filter << std::endl;

                        if (buffer->hasData) {
                            // std::cout << "is WRITE" << kev.filter <<
                            // std::endl;
                            // TODO make sure enough data is available
                            if (write(connfd, buffer->data, kBufSize) < 0)
                                diep("write()");
                            buffer->hasData = false;
                        }
                    } break;
                }
            });
        });
}

using Buffer = std::vector<char>;

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

   private:
    KernelEventListener& _listener;
    int _socket;
    std::optional<Promise<Buffer>> _readWaiting;
    std::queue<Buffer> _incomingMessages;
};

void runClient(KernelEventListener& listener) {
    auto serverSocket = tcpopen("0.0.0.0", 8000);
    auto stdinfd = fileno(stdin);

    // Read data from stdin and send it to the server.
    listener.subscribe(stdinfd, KernelEventListener::EventType::kRead)
        .then([&listener, serverSocket](struct kevent kev) {
            const int kBufSize = 1024;
            char buf[kBufSize];

            memset(buf, 0, kBufSize);

            if (read(fileno(stdin), buf, kBufSize) < 0) diep("read()");
            // TODO make sense
            if (write(serverSocket, buf, kBufSize) < 0) diep("write()");
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
}

void runClientBM(KernelEventListener& listener) {
    auto counter = std::make_shared<int>();
    for (auto j = 0; j < 256; ++j) {
        auto serverSocket = tcpopen("0.0.0.0", 8000);

        //    auto stdinfd = fileno(stdin);

        const int kBufSize = 1024;
        char buf[kBufSize];
        memset(buf, 0, kBufSize);

        if (write(serverSocket, buf, kBufSize) < 0) diep("write()");

        struct Buffer {
            bool hasData{false};
            char data[kBufSize];
        };

        auto bytes = std::make_shared<int>();
        auto buffer = std::make_shared<Buffer>();

        listener.subscribe(serverSocket)
            .then([counter, buffer, j, serverSocket](struct kevent kev) {
                switch (
                    static_cast<KernelEventListener::EventType>(kev.filter)) {
                    case KernelEventListener::EventType::kRead: {
                        // std::cout << "received response from server"
                        //          << kev.filter << std::endl;

                        // Read the response from the server.
                        memset(buffer->data, 0, kBufSize);
                        if (read(serverSocket, buffer->data, kBufSize) < 0)
                            diep("read()");

                        buffer->hasData = true;
                    } break;
                    case KernelEventListener::EventType::kWrite: {
                        // std::cout << "can WRITE" << kev.filter << std::endl;

                        if (buffer->hasData) {
                            // std::cout << "is WRITE" << kev.filter <<
                            // std::endl;
                            // TODO make sure enough data is available
                            if (write(serverSocket, buffer->data, kBufSize) < 0)
                                diep("write()");
                            buffer->hasData = false;

                            ++(*counter);
                            if (*counter % 1000 == 0)
                                std::cout << "client j... " << j
                                          << " counter: " << *counter
                                          << std::endl;
                        }
                    } break;
                }
            });
    }
}

void testList() {
    List l(3);
    auto fullList = makeList(3).prepend("hi").prepend(4.2);
    std::cout << "fullList.size(): " << fullList.size() << std::endl;

    fullList.forEach([](auto& x) { std::cout << x << std::endl; });

    std::cout << "\n\n";

    auto newList = makeList(4).append("hi").append(42);

    newList.forEach([](auto& x) { std::cout << x << std::endl; });

    std::cout << "\n\n";

    auto listOfCallables =
        makeList([] { std::cout << "First!" << std::endl; })
            .append([] { std::cout << "Second!" << std::endl; })
            .append([] { std::cout << "Third!" << std::endl; });

    listOfCallables.forEach([](auto& callable) { callable(); });
    listOfCallables.reverse().forEach([](auto& callable) { callable(); });

    std::cout << "\n\n";

    auto listOfNumbers = makeList(3).append(4.2).append(20ull);
    auto sum = listOfNumbers.fold(
        [](auto currentSum, auto next) { return currentSum + next; });

    std::cout << "sum: " << sum << std::endl;

    auto newListOfNumbersWithMoreNumbers =
        std::move(listOfNumbers).appendAll(makeList(5).append(6).append(7));
    std::cout << "newListOfNumbersWithMoreNumbers.size(): "
              << newListOfNumbersWithMoreNumbers.size() << std::endl;
    auto newSum = newListOfNumbersWithMoreNumbers.fold(
        [](auto currentSum, auto next) { return currentSum + next; });
    std::cout << "newSum: " << newSum << std::endl;

    std::cout << "\n\n";
}

void testKernelEventListener(Executor& executor,
                             KernelEventListener& keventListener, int argc) {
    if (argc == 1) {
        runServer(keventListener);
    } else if (argc == 2) {
        std::cout << "Run client!" << std::endl;
        runClient(keventListener);
    } else if (argc > 2) {
        std::cout << "Run client bm!" << std::endl;
        runClientBM(keventListener);
    }
}

template <typename Executor>
void testStrandBasic(Executor& executor) {
    auto chain2 =
        // Strands are your "lazy futures"
        makeStrand()
            .then([&] {
                std::cout << "Strand: Waiting to hear foo... " << std::endl;
                // subscribe returns a Future<std::string> that gets flattened
                return "hello";
            })
            .then([&](std::string s) {
                std::cout << "Strand: Saw input: " << s << std::endl;
                return makeStrand()
                    .then([&] {
                        std::cout << "Strand: Waiting to hear bar...: "
                                  << std::endl;
                        return "hello again";
                    })
                    .then([&](std::string s) {
                        std::cout << "Nested process: stuff " << std::endl;
                        return s;
                    })
                    .execute(executor);
            })
            .then([](std::string s) {
                std::cout << "Strand: Saw input: " << s << std::endl;
                return 17;
            });

    for (auto i = 0; i < 10; ++i) {
        // "Future" is just a one-off event trigger thing that takes a single
        // continuation
        Future<int> fut = chain2.execute(executor);
        fut.then(
            [](int i) { std::cout << "Strand result: " << i << std::endl; });
    }
}

void testStrandAlternative(Executor& executor) {
    for (auto i = 0; i < 10; ++i) {
        // Strands are your "lazy futures"
        executor.schedule([&executor] {
            std::cout << "Strand: Waiting to hear foo... " << std::endl;
            // subscribe returns a Future<std::string> that gets flattened
            std::string s = "hello";
            executor.schedule([&executor, s] {
                std::cout << "Strand: Saw input: " << s << std::endl;

                executor.schedule([&executor] {
                    std::cout << "Strand: Waiting to hear bar...: "
                              << std::endl;
                    std::string s2 = "hello again";

                    executor.schedule([&executor, s2] {
                        std::cout << "Nested process: stuff " << std::endl;

                        executor.schedule([&executor, s2] {
                            std::cout << "Strand: Saw input: " << s2
                                      << std::endl;
                            return 17;
                        });
                    });
                });
            });
        });
    }
}

// class Environment {
//   public:
//    Environment(Executor& executor) : _executor(executor) {}
//
//    class IdAllocator {
//       public:
//        using Id = std::uint64_t;
//
//        Id newId() {
//            if (_freeIds.empty()) {
//                return ++_maxId;
//            } else {
//                auto id = _freeIds.front();
//                _freeIds.pop_front();
//                return id;
//            }
//        }
//
//        void freeId(Id id) { _freeIds.push_back(id); }
//
//       private:
//        std::uint64_t _maxId{0};
//        std::list<Id> _freeIds;
//    };
//
//    class ExecutionContext {
//        using Callable = std::function<void()>;
//
//       public:
//        ExecutionContext() = delete;
//        ExecutionContext(Environment* environment, IdAllocator::Id id)
//            : _environment(environment), _id(id) {}
//
//        ~ExecutionContext() {
//            _environment->_runningExecutionContexts.erase(_id);
//            _environment->_idAllocator.freeId(_id);
//        }
//
//        void schedule(Callable&& callable) { _queue.push(callable); }
//
//        void runNext() {
//            _environment->_executor.schedule(std::move(_queue.front()));
//            _queue.pop();
//        }
//
//       private:
//        Environment* _environment;
//        IdAllocator::Id _id;
//        std::queue<Callable> _queue;
//    };
//
//    std::unique_ptr<ExecutionContext> createContext() {
//        auto id = _idAllocator.newId();
//        auto newContext = std::make_unique<ExecutionContext>(this, id);
//        _runningExecutionContexts.emplace(id, newContext.get());
//        return newContext;
//    }
//
//    void run() {
//        loop(_executor, [this]() mutable {
//            for (auto& [id, executionContext] : _runningExecutionContexts) {
//                executionContext->runNext();
//            }
//            return false;
//        });
//    }
//
//   private:
//    Executor& _executor;
//    IdAllocator _idAllocator;
//    std::unordered_map<IdAllocator::Id, ExecutionContext*>
//        _runningExecutionContexts;
//};

void testUserInputSubscriptionService(
    Executor& executor, UserInputSubscriptionService& inputService) {
    auto chain2 =
        // Strands are your "lazy futures"
        makeStrand()
            .then([&] {
                std::cout << "Strand: Waiting to hear foo... " << std::endl;
                // subscribe returns a Future<std::string> that gets flattened
                return inputService.subscribe("foo");
            })
            .then([&](std::string s) {
                std::cout << "Strand: Saw input: " << s << std::endl;
                return makeStrand()
                    .then([&] {
                        std::cout << "Strand: Waiting to hear bar...: "
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
                std::cout << "Strand: Saw input: " << s << std::endl;
                return 17;
            });

    for (auto i = 0; i < 10; ++i) {
        // "Future" is just a one-off event trigger thing that takes a single
        // continuation
        Future<int> fut = chain2.execute(executor);
        fut.then(
            [](int i) { std::cout << "Strand result: " << i << std::endl; });
    }
}

#define ASYNC(x) makeStrand().then([&] x)
#define CREATE_ASYNC_CHAIN(var, body) auto var = makeStrand().then([&]  body
#define ASYNC_ASSIGN(var, x) \
    return (x);              \
    }).then([&](auto (var)) {
void testUserInputSubscriptionServiceWithMacros(
    Executor& executor, UserInputSubscriptionService& inputService) {
    for (auto i = 0; i < 10; ++i) {
        // Strands are your "lazy futures"
        auto routine = ASYNC({
            std::cout << "Strand: Waiting to hear foo... " << std::endl;

            // subscribe returns a Future<std::string> that gets flattened
            ASYNC_ASSIGN(fooResult, inputService.subscribe("foo"));

            std::cout << "Strand: Saw input: " << fooResult << std::endl;

            auto subroutine = ASYNC({
                std::cout << "Strand: Waiting to hear bar...: " << std::endl;
                ASYNC_ASSIGN(barResult, inputService.subscribe("bar"));
                std::cout << "Nested process: stuff " << std::endl;
                return barResult;
            });

            ASYNC_ASSIGN(barResult, subroutine.execute(executor));

            std::cout << "Strand: Saw input: " << barResult << std::endl;
            return 17;
        });

        // "Future" is just a one-off event trigger thing that takes a single
        // continuation
        routine.execute(executor).then(
            [](int i) { std::cout << "Strand result: " << i << std::endl; });
    }
}

int main(int argc, char** argv) {
    Executor executor;
    // Environment env(executor);

    //    UserInputSubscriptionService inputService(executor);
    //    inputService.run();
    //    testUserInputSubscriptionServiceWithMacros(executor, inputService);

    KernelEventListener keventListener(executor);
    keventListener.run();
    testKernelEventListener(executor, keventListener, argc);

    // testStrandBasic(executor);
    // testStrandAlternative(executor);

    std::cout << "Starting executor" << std::endl;
    executor.run();
}
