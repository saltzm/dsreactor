#include <iostream>
#include <list>
#include <type_traits>

/*
template <typename InputCallable, typename... Callables>
class Future : public Future<Callables...> {
public:
    Future(InputCallable &&input, Callables... callables) :
_input(std::move(input)) {}

    template <typename Continuation>
    auto then(Continuation && callable) {
        return Future<InputCallable, Continuation>(std::move(_input),
std::move(callable));
    }

private:
    InputCallable _input;
};
*/

// template <typename InputCallable, typename InputCallable2>
// class Future2;
//
// template <typename InputCallable>
// class Future {
//   public:
//    Future(InputCallable &&input) : _input(std::move(input)) {}
//
//    template <typename Continuation>
//    auto then(Continuation &&callable) {
//        return Future2<InputCallable, Continuation>(std::move(_input),
//                                                    std::move(callable));
//    }
//
//    auto go() { return _input(); }
//
//   private:
//    InputCallable _input;
//};
//
// template <typename InputCallable, typename InputCallable2>
// class Future2 : public Future<InputCallable> {
//   public:
//    Future2(InputCallable &&input, InputCallable2 &&input2)
//        : Future<InputCallable>(std::move(input)), _input2(std::move(input2))
//        {}
//
//    /*
//    template <typename Continuation>
//    auto then(Continuation && callable) {
//        return Future<InputCallable, Continuation>(std::move(_input),
//    std::move(callable));
//    }
//    */
//
//    auto go() {
//        auto inputResult = Future<InputCallable>::go();
//        return _input2(inputResult);
//    }
//
//   private:
//    // InputCallable _input;
//    InputCallable2 _input2;
//};
//
// template <typename InputCallable, typename InputCallable2,
//          typename InputCallable3>
// class Future3 : public Future2<InputCallable, InputCallable2> {
//   public:
//    Future3(InputCallable &&input, InputCallable2 &&input2,
//            InputCallable3 &&input3)
//        : Future2<InputCallable, InputCallable2>(std::move(input)),
//          _input(std::move(input3)) {}
//
//    /*
//    template <typename Continuation>
//    auto then(Continuation && callable) {
//        return Future<InputCallable, Continuation>(std::move(_input),
//    std::move(callable));
//    }
//    */
//
//    auto go() {
//        auto inputResult = Future2<InputCallable, InputCallable2>::go();
//        return _input(inputResult);
//    }
//
//   private:
//    // InputCallable _input;
//    InputCallable3 _input;
//};
//

// class Future {
//    public:
//     Future() {}
//
//     template <typename Continuation>
//     auto then(Continuation &&continuation);
//
//     auto go() { return _callable(); }
//
//    private:
//     // InputCallable _input;
//     MyCallable _callable;
// };

// template <typename MyCallable>
// class Future {
//    public:
//     Future(MyCallable &&myCallable) : _callable(std::move(myCallable)) {}
//
//     template <typename Continuation>
//     auto then(Continuation &&continuation) {
//         return Future<Continuation, MyCallable>(
//             std::forward<Continuation>(continuation), std::move(_callable));
//     }
//     auto go() { return _callable(); }
//
//    private:
//     // InputCallable _input;
//     MyCallable _callable;
// };
//

// template <typename... Callables>
// class Future {};
//
// template <typename MyCallable, typename... Callables>
// class Future;

// template <typename MyCallable, typename... Callables>
// class Future : public Future<Callables...> {
//   public:
//    Future(MyCallable &&myCallable, Callables &&... callables)
//        : Future<Callables...>(std::forward<Callables...>(callables...)),
//          _callable(std::move(myCallable)) {}
//
//    template <typename Continuation>
//    auto then(Continuation &&continuation) && {
//        return Future<Continuation, MyCallable, Callables...>(
//            std::forward<Continuation>(continuation), std::move(_callable));
//    }
//
//    auto go() {
//        auto inputResult = Future<Callables...>::go();
//        return _callable(inputResult);
//    }
//
//   private:
//    // InputCallable _input;
//    MyCallable _callable;
//};
//
// template <typename MyCallable>
// class Future<MyCallable> {
//   public:
//    Future(MyCallable &&myCallable)
//        : _callable(std::forward<MyCallable>(myCallable)) {}
//
//    template <typename Continuation>
//    auto then(Continuation &&continuation) && {
//        return Future<Continuation, MyCallable>(
//            std::forward<Continuation>(continuation),
//            std::forward<MyCallable>(_callable));
//    }
//
//    auto go() { return _callable(); }
//
//   private:
//    // InputCallable _input;
//    MyCallable _callable;
//};

//
// template <typename MyCallable>
// class Future<MyCallable, void> {
//   public:
//    Future(MyCallable &&myCallable)
//        : _callable(std::forward<MyCallable>(myCallable)) {}
//
//    template <typename Continuation>
//    auto then(Continuation &&continuation) && {
//        return Future<Continuation, std::decay_t<decltype(*this)>>(
//            std::forward<Continuation>(continuation), std::move(*this));
//    }
//
//    auto go() { return _callable(); }
//
//   private:
//    // InputCallable _input;
//    MyCallable _callable;
//};

// template <typename MyCallable, typename InputCallable, typename
// PreviousInputCallable> class Future : public Future<InputCallable,
// PreviousInputCallable> {
//   public:
//    Future(MyCallable &&myCallable, InputCallable& callable)
//        : Future<InputCallable>(std::forward<InputCallable>(callable)),
//          _callable(std::move(myCallable)) {}
//
//    template <typename Continuation>
//    auto then(Continuation &&continuation) && {
//        return Future<Continuation, MyCallable, InputCallable>(
//            std::forward<Continuation>(continuation), _callable);
//    }
//
//    auto go() {
//        auto inputResult = Future<Callables...>::go();
//        return _callable(inputResult);
//    }
//
//   private:
//    // InputCallable _input;
//    MyCallable _callable;
//};

// template <typename MyCallable, typename InputCallable>
// class Future : public Future<InputCallable> {
//   public:
//    Future(MyCallable &&myCallable, InputCallable& callable)
//        : Future<InputCallable>(std::forward<InputCallable>(callable)),
//          _callable(std::move(myCallable)) {}
//
//    template <typename Continuation>
//    auto then(Continuation &&continuation) && {
//        return Future<Continuation, MyCallable, InputCallable>(
//            std::forward<Continuation>(continuation), _callable);
//    }
//
//    auto go() {
//        auto inputResult = Future<Callables...>::go();
//        return _callable(inputResult);
//    }
//
//   private:
//    // InputCallable _input;
//    MyCallable _callable;
//};
//
// template <typename MyCallable>
// class Future<MyCallable> {
//   public:
//    Future(MyCallable &&myCallable)
//        : _callable(std::forward<MyCallable>(myCallable)) {}
//
//    template <typename Continuation>
//    auto then(Continuation &&continuation) && {
//        return Future<Continuation, MyCallable>(
//            std::forward<Continuation>(continuation),
//            std::forward<MyCallable>(_callable));
//    }
//
//    auto go() { return _callable(); }
//
//   private:
//    // InputCallable _input;
//    MyCallable _callable;
//};

// template <>
// class Future<std::function<void(void)>> {
//   public:
//    Future(std::function<void(void)> &&) {
//    }  //: _callable(std::move(myCallable)) {}
//
//    template <typename Continuation>
//    auto then(Continuation &&continuation) {
//        return Future<Continuation>(std::forward<Continuation>(continuation));
//    }
//
//    auto go() { return 0; }
//
//   private:
//    //    MyCallable _callable;
//};
//

class Executor {
    using Callable = std::function<void()>;

   public:
    void schedule(Callable &&callable) {
        // std::cout << "Scheduling" << std::endl;
        queue.emplace_back(std::move(callable));
    }

    void run() {
        while (!queue.empty()) {
            auto &next = queue.front();
            try {
                //       std::cout << "Processing next" << std::endl;
                next();
            } catch (...) {
            }
            queue.pop_front();
        }
    }

   private:
    std::list<Callable> queue;
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

        return Future<Continuation, ThisType>(
            std::forward<Continuation>(continuation),
            std::forward<ThisType>(*this));
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
            if constexpr (std::is_void<decltype(_callable())>::value) {
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

template <class Callable>
auto makeFuture(Callable &&callable) {
    return Future<Callable>(std::forward<Callable>(callable));
}

auto makeTestFutureChain2(Executor &executor) {
    return makeFuture([]() { std::cout << "step 0" << std::endl; })
        .then([&executor]() {
            std::cout << "step 1" << std::endl;
            return 2;
        })
        .then([](int &&i) {
            //           std::cout << "step 2" << std::endl;
            std::cout << "in a future with i : " << i << std::endl;
            return std::string("something");
        })
        .then([](std::string &&s) {
            //            std::cout << "step 3" << std::endl;
            std::cout << "input s:  " << s << std::endl;
        });
}

// auto voidThenValue(Executor &executor) {
//    return makeFuture([]() { std::cout << "step 0" << std::endl; })
//        .then([&executor]() {
//            std::cout << "step 1" << std::endl;
//            return 2;
//        })
//        .then([](int &&i) {
//            //           std::cout << "step 2" << std::endl;
//            std::cout << "in a future with i : " << i << std::endl;
//            return std::string("something");
//        })
//        .then([](std::string &&s) {
//            //            std::cout << "step 3" << std::endl;
//            std::cout << "input s:  " << s << std::endl;
//        })
//        .then([](std::string &&s) {
//            //            std::cout << "step 3" << std::endl;
//            std::cout << "input s:  " << s << std::endl;
//        })
//        .go(executor);
//}

auto makeTestFutureChain(Executor &executor) {
    return makeFuture([]() { std::cout << "step 0" << std::endl; })
        .then([&executor]() {
            std::cout << "step 1" << std::endl;
            makeFuture([]() {
                std::cout << "step 1.1" << std::endl;
                return 4;
            })
                .then([](int &&i) { std::cout << "step 1.2" << std::endl; })
                .go(executor);
            return 2;
        })
        .then([](int &&i) {
            std::cout << "step 2, i : " << i << std::endl;
            return std::string("something");
        })
        .then([](std::string &&s) {
            std::cout << "step 3, s:  " << s << std::endl;
        })
        .then([]() { std::cout << "step 4" << std::endl; });
}
int main() {
    // std::cout << "Hello World!\n";

    Executor executor;
    for (int i = 0; i < 3; ++i) {
        makeTestFutureChain(executor).go(executor);
    }

    executor.run();
}
// for (int i = 0; i < 100000; ++i) {
//    std::cout << "Something asdfasdf" << i << std::endl;
//    std::cout << "Something asdfasdf" << i << std::endl;
//    std::cout << "Something asdfasdf" << i << std::endl;
//    std::cout << "Something asdfasdf" << i << std::endl;
//    std::cout << "Something asdfasdf" << i << std::endl;
//}

//    makeTestFutureChain(executor).go(executor);
//    makeTestFutureChain(executor).go(executor);

// makeFuture()
//    .then([]() {
//        std::cout << "step 1" << std::endl;
//        return 2;
//    })
//    .then([](int i) {
//        std::cout << "in a future with i : " << i << std::endl;
//    })
//    .then([] {
//        std::cout << "in a continuation:  " << std::endl;
//        return std::string("something");
//    })
//    .then(
//        [](std::string &s) { std::cout << "input s:  " << s <<
//        std::endl;
//        })
//    .go();
