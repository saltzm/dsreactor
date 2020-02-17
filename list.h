
#pragma once

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

template <typename T>
auto makeList(T&& t) {
    return List(std::forward<T>(t));
}

template <typename>
struct IsList : public std::false_type {};

template <typename... T>
struct IsList<List<T...>> : public std::true_type {};

