
#pragma once

#include <iostream>
#include <queue>
#include <thread>
#include <unordered_map>

#include "executor.h"

class UserInputSubscriptionService {
   public:
    UserInputSubscriptionService(Executor& e) : _executor(e) {}

    ~UserInputSubscriptionService() {
        if (_monitoringThread.joinable()) {
            _monitoringThread.join();
        }
    }

    Future<std::string> subscribe(std::string filter) {
        if (_subscribers.find(filter) == _subscribers.end()) {
            _subscribers[filter] = {};
        }

        Promise<std::string> p;
        auto f = p.getFuture();
        _subscribers[filter].emplace_back(std::move(p));
        return f;
    }

    void run() {
        _monitoringThread = std::thread([this] {
            while (!_shutdown) {
                std::string input;
                std::cin >> input;
                std::lock_guard lk(_mutex);
                _userInputQueue.push(input);
            }
        });

        loop(_executor, [this]() mutable {
            std::unique_lock lk(_mutex);
            if (_userInputQueue.size() > 0) {
                auto next = _userInputQueue.front();
                lk.unlock();
                if (next == "die") {
                    _shutdown = true;
                    _monitoringThread.join();
                    return false;
                }

                _userInputQueue.pop();
                std::vector<std::string> filtersToRemove;
                for (auto& [k, v] : _subscribers) {
                    if (next.find(k) != std::string::npos) {
                        for (auto& p : v) {
                            p.set(next);
                        }
                        filtersToRemove.push_back(k);
                    }
                }

                for (auto& filter : filtersToRemove) {
                    _subscribers.erase(filter);
                }
            }
            return true;
        });
    }

   private:
    Executor& _executor;
    bool _shutdown{false};
    std::unordered_map<std::string, std::vector<Promise<std::string>>> _subscribers;

    std::thread _monitoringThread;

    // Protects input queue
    std::mutex _mutex;
    std::queue<std::string> _userInputQueue;
};

