
#pragma once

#include "executor.h"
#include "future.h"

void diep(const char* s) {
    perror(s);
    exit(EXIT_FAILURE);
}

/**
 * Service that listens to kernel events with kevent/kqueue and allows callers
 * to subscribe to certain events on file descriptors.
 */
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

    /**
     * Returns a Future that will be triggered *each time* an event with the
     * specified type occurs. The Future will contain the corresponding kevent
     * type.
     *
     * TODO: Use a different type for this (e.g. UnorderedStream) that is
     * actually supposed to be multi-use. Right now it's just a coincidence of
     * the Future implementation that it'll work several times.
     */
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
            if (_eventsToMonitor.size() > 0) {
                int nev =
                    kevent(_kernelQueue, _eventsToMonitor.data(),
                           _eventsToMonitor.size(), _eventsReceived.data(),
                           _eventsReceived.size(), &timeout);

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

