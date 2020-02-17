
#pragma once

#include "future.h"
#include "list.h"

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

template <typename ComputationList, typename Result>
class Strand {
   public:
    constexpr Strand(ComputationList computationList)
        : _list(std::move(computationList)) {}

    template <typename T>
    constexpr auto then(T&& end) && {
        using FlatResult = GetReturnType<Result>;

        if constexpr (std::is_void_v<FlatResult>) {
            auto newComputationList = std::move(_list).append(end);
            return Strand<decltype(newComputationList), decltype(end())>(
                std::move(newComputationList));
        } else {
            auto newComputationList = std::move(_list).append(end);
            return Strand<decltype(newComputationList),
                          decltype(end(FlatResult()))>(
                std::move(newComputationList));
        }
    }

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

static constexpr auto makeStrand() {
    auto initList = List([] {});
    return Strand<decltype(initList), void>(std::move(initList));
}

