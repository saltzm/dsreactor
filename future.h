
#pragma once

#include <optional>

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

