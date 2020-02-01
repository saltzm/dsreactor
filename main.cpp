#include <functional>
#include <iostream>
#include <list>
#include <type_traits>

class Executor {
    using Callable = std::function<void()>;

   public:
    void schedule(Callable &&callable) {
        queue.emplace_back(std::move(callable));
    }

    void run() {
        while (!queue.empty()) {
            auto &next = queue.front();
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

template <typename T>
struct IsFuture;

template <typename T, typename Continuation>
class AwaitableWithContinuation {
    explicit AwaitableWithContinuation(Executor &e, Continuation &&continuation)
        : _executor(e),
          _continuation(std::forward<Continuation>(continuation)) {}

    void set(T &&t) {
        if (!_hasValue) {
            _hasValue = true;
            _t = t;
            _executor.schedule(
                [val = std::move(_t), continuation = std::move(_continuation)] {
                    return continuation(val);
                });
        }
    }

   private:
    bool _hasValue{false};
    T _t;
    Continuation _continuation;
    // TODO &
    Executor &_executor;
};

template <typename T>
class Awaitable {
   public:
    explicit Awaitable(Executor &e) : _executor(e) {}

    void set(T &&t) {
        _hasValue = true;
        _t = t;
    }

    template <typename Callable>
    auto then(Callable &&continuation) {
        if (_hasValue) {
            _executor.schedule(
                [val = std::move(_t),
                 continuation = std::forward<Callable>(continuation)] {
                    return continuation(val);
                });
        } else {
            return AwaitableWithContinuation<T, Callable>(
                _executor, std::forward<Callable>(continuation));
        }
    }

   private:
    bool _hasValue{false};
    T _t;
    // TODO &
    Executor &_executor;
};

template <typename MyCallable, typename InputChain = std::nullptr_t>
class Future {
   public:
    Future(MyCallable &&myCallable) : _callable(std::move(myCallable)) {}

    Future(MyCallable &&myCallable, InputChain &&inputChain)
        : _callable(std::forward<MyCallable>(myCallable)),
          _inputChain(std::forward<InputChain>(inputChain)) {}

    template <typename Continuation>
    auto then(Continuation &&continuation) && {
        using ThisType = std::decay_t<decltype(*this)>;

        // if constexpr (std::is_void<decltype(go())>::value) {
        // if constexpr (IsFuture<decltype(continuation())>::value) {
        //    return Future(
        //        [continuation] {
        //            return continuation().then([](auto &x) { return x; });
        //        },
        //        std::forward<ThisType>(*this));

        //    //            return this->then(
        //    //                [this, continuation =
        //    //                std::forward<Continuation>(continuation)](
        //    //                    decltype(go()) &&x) { return
        //    //                    x.then(continuation); });
        //} else {
        return Future<Continuation, ThisType>(
            std::forward<Continuation>(continuation),
            std::forward<ThisType>(*this));
        //}
        //}
    }

    auto go() {
        if constexpr (std::is_null_pointer<InputChain>::value) {
            return _callable();
        } else {
            if constexpr (std::is_void<decltype(_inputChain.go())>::value) {
                _inputChain.go();
                return _callable();
            } else {
                return _callable(_inputChain.go());
            }
        }
    }

    void go(Executor &executor) {
        executor.schedule([this, &executor] {
            if constexpr (std::is_null_pointer<InputChain>::value) {
                return _callable();
            } else {
                if constexpr (std::is_void<decltype(_inputChain.go())>::value) {
                    return _inputChain.goImpl(
                        executor, [this]() mutable { return _callable(); });
                } else {
                    return _inputChain.goImpl(
                        executor, [this](auto &&inputResult) mutable {
                            return _callable(
                                std::forward<decltype(inputResult)>(
                                    inputResult));
                        });
                }
            }
        });
    }

    template <typename Callback>
    auto goImpl(Executor &executor, Callback &&callback) {
        if constexpr (std::is_null_pointer<InputChain>::value) {
            if constexpr (std::is_invocable_v<Callback>) {
                _callable();
                executor.schedule([this, &executor, callback]() mutable {
                    return callback();
                });
            } else {
                auto result = _callable();
                executor.schedule([this, &executor, callback,
                                   result = std::move(result)]() mutable {
                    return callback(std::move(result));
                });
            }
        } else {
            if constexpr (std::is_invocable_v<MyCallable>) {
                if constexpr (std::is_invocable_v<Callback>) {
                    return _inputChain.goImpl(
                        executor, [this, &executor, callback]() mutable {
                            _callable();
                            executor.schedule(
                                [this, &executor, callback]() mutable {
                                    return callback();
                                });
                        });
                } else {
                    return _inputChain.goImpl(
                        executor, [this, &executor, callback]() mutable {
                            auto result = _callable();
                            executor.schedule(
                                [this, &executor, callback,
                                 result = std::move(result)]() mutable {
                                    return callback(std::move(result));
                                });
                        });
                }
            } else {
                if constexpr (std::is_invocable_v<Callback>) {
                    return _inputChain.goImpl(
                        executor, [this, &executor,
                                   callback](auto &&inputResult) mutable {
                            _callable(std::forward<decltype(inputResult)>(
                                inputResult));
                            executor.schedule(
                                [this, &executor, callback]() mutable {
                                    return callback();
                                });
                        });

                } else {
                    return _inputChain.goImpl(
                        executor, [this, &executor,
                                   callback](auto &&inputResult) mutable {
                            auto result =
                                _callable(std::forward<decltype(inputResult)>(
                                    inputResult));
                            executor.schedule(
                                [this, &executor, callback,
                                 result = std::move(result)]() mutable {
                                    return callback(std::move(result));
                                });
                        });
                }
            }
        }
    }

   private:
    MyCallable _callable;
    InputChain _inputChain;
};

template <typename>
struct IsFuture : public std::false_type {};

template <typename T>
struct IsFuture<Future<T>> : public std::true_type {};

template <class Callable>
auto makeFuture(Callable &&callable) {
    return Future<Callable>(std::forward<Callable>(callable));
}

// Problem: Can't type the types!!!

// Future<> loop() {
//    return makeFuture([]() { std::cout << "step 0" << std::endl; }).then([] {
//        return loop();
//    });
//}

auto makeTestFutureChain(Executor &executor) {
    return makeFuture([]() { std::cout << "step 0" << std::endl; })
        .then([&executor]() {
            std::cout << "step 1" << std::endl;
            return makeFuture([]() {
                std::cout << "step 1.1" << std::endl;
                return 4;
            });
            //                .then([](int &&i) {
            //                    std::cout << "step 1.2" << std::endl;
            //                    return i;
            //                });
        })
        .then([](auto &&fut) { std::move(fut).then([](int &&i) {}); });
    //        .then([](int &&i) {
    //            std::cout << "step 2, i : " << i << std::endl;
    //            return std::string("something");
    //        })
    //        .then([](std::string &&s) {
    //            std::cout << "step 3, s:  " << s << std::endl;
    //        })
    //        .then([]() { std::cout << "step 4" << std::endl; });
}

int main() {
    Executor executor;
    for (int i = 0; i < 3; ++i) {
        makeTestFutureChain(executor).go(executor);
    }

    executor.run();
}
