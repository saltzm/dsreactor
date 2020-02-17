
#pragma once

#include "executor.h"
#include "future.h"

void diep(const char* s) {
    perror(s);
    exit(EXIT_FAILURE);
}

class KernelEventListener {
   public:
    KernelEventListener(Executor& e) : _executor(e), _kernelQueue(kqueue()) {
        assert(_kernelQueue != -1);
    }
    ~KernelEventListener() {
        /* Close kqueue */
        if (close(_kernelQueue) == -1) diep("close()");
    }

    enum class EventType : int {
        kRead = EVFILT_READ,
        kWrite = EVFILT_WRITE,
    };

    Future<struct kevent> subscribe(int fd, EventType type) {
        _eventsToMonitor.emplace_back();
        _monitoredEventPromises[fd];
        _eventsReceived.emplace_back();
        EV_SET(&_eventsToMonitor.back(), fd, int(type), EV_ADD | EV_ENABLE, 0,
               0, 0);
        return _monitoredEventPromises[fd].getFuture();
    }

    void run() {
        // Just poll - never block.
        struct timespec timeout = {0, 0};
        loop(_executor, [this, timeout]() mutable {
            // std::cout << "LOOP: " << _eventsToMonitor.size() << std::endl;

            if (_eventsToMonitor.size() > 0) {
                // std::cout << "about to block on kevent" << std::endl;

                int nev =
                    kevent(_kernelQueue, _eventsToMonitor.data(),
                           _eventsToMonitor.size(), _eventsReceived.data(),
                           _eventsReceived.size(), &timeout);

                // std::cout << "done blocking on kevent" << std::endl;

                if (nev < 0) {
                    diep("kevent()");
                } else if (nev > 0) {
                    for (auto i = 0; i < nev; i++) {
                        if (_eventsReceived[i].flags & EV_EOF ||
                            _eventsReceived[i].flags & EV_ERROR) {
                            /* Report errors */
                            fprintf(stderr, "EV_ERROR: %s\n",
                                    strerror(_eventsReceived[i].data));
                            exit(EXIT_FAILURE);
                        }
                        std::cout << "_monitoredEventPromises.size(): "
                                  << _monitoredEventPromises.size()
                                  << std::endl;

                        _monitoredEventPromises[_eventsReceived[i].ident].set(
                            _eventsReceived[i]);
                    }
                }
            }
            return true;
        });
    }

   private:
    Executor& _executor;

    int _kernelQueue;

    std::vector<struct kevent> _eventsToMonitor;
    std::unordered_map<int, Promise<struct kevent>> _monitoredEventPromises;
    std::vector<struct kevent> _eventsReceived;
};

