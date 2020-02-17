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
    } else if (argc > 1) {
        std::cout << "Run client!" << std::endl;
        runClient(keventListener);
    }
}

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

int main(int argc, char** argv) {
    Executor executor;

    UserInputSubscriptionService inputService(executor);
    inputService.run();
    testUserInputSubscriptionService(executor, inputService);

    // KernelEventListener keventListener(executor);
    // keventListener.run();
    // testKernelEventListener(executor, keventListener, argc);

    std::cout << "Starting executor" << std::endl;
    executor.run();
}
