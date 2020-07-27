
#pragma once

#include <deque>
#include <functional>

class Executor {
    using Callable = std::function<void()>;

   public:
    ~Executor() {}

    void schedule(Callable&& callable) {
        queue.emplace_back(std::move(callable));
    }

    void run() {
        while (!queue.empty()) {
            // std::cout << "queue size: " << queue.size() << std::endl;
            auto& next = queue.front();
            try {
                next();
            } catch (...) {
            }
            queue.pop_front();
            ++_tasksExecuted;
            if (_tasksExecuted % 10000000 == 0) {
                // std::cout << "_tasksExecuted: " << _tasksExecuted <<
                // std::endl;
            }
        }
    }

   private:
    long long _tasksExecuted{0};
    std::deque<Callable> queue;
};

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
