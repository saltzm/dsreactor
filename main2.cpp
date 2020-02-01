#include <iostream>
#include <list>
#include <type_traits>

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
                executeImpl(list.tail(), executor, std::move(x));
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
                executeImpl(list.tail(), executor, std::move(x));
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

    // Compose all of the functions in the list so they run in order,
    // supporting void-returning callables in the middle
    //    auto composition = chain.fold([](auto current, auto next) {
    //        return [current, next] {
    //            if constexpr (std::is_invocable<decltype(next),
    //                                            decltype(current())>::value) {
    //                return next(current());
    //            } else {
    //                current();
    //                return next();
    //            }
    //        };
    //    });

    //    auto result = composition();
    //    std::cout << result << std::endl;

    //    executor.schedule([executor]() {
    //        // First thing in list
    //        // auto res0 = head();
    //        executor.schedule([executor, res0]() {
    //            auto res1 = head(res0);
    //            executor.schedule([...] {
    //
    //            });
    //        });
    //    });

    std::cout << "\n\n";

    Executor executor;
    // Compose all of the functions in the list so they run in order,
    // supporting void-returning callables in the middle
    //    auto compositionOnExecutor =
    //        chain.reverse().fold([&executor](auto current, auto next) {
    //            return [&executor, current, next] {
    //                // if constexpr
    //                // (!std::is_void_v<std::decay_t<decltype(current())>>) {
    //
    //                if constexpr (std::is_invocable<decltype(current),
    //                                                decltype(next())>::value)
    //                                                {
    //                    auto result = next();
    //                    executor.schedule([current, input = std::move(result)]
    //                    {
    //                        current(input);
    //                    });
    //                    // return next(current());
    //                } else {
    //                    //                    current();
    //                    //                    executor.schedule([next] {
    //                    next(); });
    //                }
    //            };
    //        });
    //
    //    compositionOnExecutor();

    // chain.execute(executor);
    for (auto i = 0; i < 10; ++i) {
        execute(chain, executor);
    }

    std::cout << "Starting executor" << std::endl;
    executor.run();
    // std::cout << result << std::endl;
}
