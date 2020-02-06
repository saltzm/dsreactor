#include <iostream>
#include <list>
#include <type_traits>
#include <variant>

#include <netinet/in.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

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
template <typename>
struct IsList : public std::false_type {};

template <typename... T>
struct IsList<List<T...>> : public std::true_type {};

template <typename CallableList, typename Input>
auto executeImpl(CallableList&& list, Executor& executor, Input&& input) {
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
                } else {
                    executeImpl(list.tail(), executor, std::move(x));
                }
            });
        }
    }
}

template <typename CallableList>
auto executeImpl(CallableList&& list, Executor& executor) {
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
                } else {
                    executeImpl(list.tail(), executor, std::move(x));
                }
            });
        }
    }
}

template <typename CallableList>
auto execute(CallableList&& list, Executor& executor) {
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

int main() {
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

    auto chain = makeList([] { return 3; })
                     .append([](int i) {
                         std::cout << "Second in chain, i = " << i << std::endl;
                         return std::string("i made it");
                     })
                     .append([](std::string s) {
                         std::cout << s << std::endl;
                         std::cout << "Third in chain" << std::endl;
                         // TODO this should work w/ void return... works in
                         // wandbox
                         // return 3;
                     })
                     .append([] { return 10; });

    auto nestedChain =
        makeList([] { return 3; })
            .append([](int i) {
                std::cout << "Second in chain, i = " << i << std::endl;
                return makeList([] { return 5; }).append([](int i) {
                    return "in nested thingie";
                });
            })
            .append([](std::string s) {
                std::cout << s << std::endl;
                std::cout << "Third in nested chain" << std::endl;
                // TODO this should work w/ void return... works
                // in wandbox return 3;
            })
            .append([] { return 10; });

    std::cout << "\n\n";

    Executor executor;

    executor.schedule([&executor] {
        auto i = 3;
        executor.schedule([i, &executor] {
            std::cout << "X  Second in chain, i = " << i << std::endl;
            auto s = std::string("i made it");

            executor.schedule([s] {
                std::cout << s << std::endl;
                std::cout << "X Third in chain" << std::endl;
                // TODO this should work w/ void return... works in
                // wandbox
                // return 3;
            });
        });
    });

    //    auto i = 0;
    //    loop(executor,
    //         [&i] {
    //             std::cout << "looping" << std::endl;
    //             ++i;
    //         },
    //         [&i] { return i < 5; }, [] { std::cout << "moving on" <<
    //         std::endl; });
    //
    //    for (auto i = 0; i < 10; ++i) {
    //        execute(chain, executor);
    //    }

    execute(nestedChain, executor);

    std::cout << "Starting executor" << std::endl;
    executor.run();
    // std::cout << result << std::endl;
}
