
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
        _monitoredEventPromises[fd];
        ++_numRegisteredEvents;

        struct kevent events[1];
        EV_SET(&events[0], fd, int(type), EV_ADD | EV_ENABLE, 0, 0, 0);
        // TODO handle error
        assert(::kevent(_kernelQueue, events, 1, 0, 0, 0) != -1);
        return _monitoredEventPromises[fd].getFuture();
    }

    Future<struct kevent> subscribe(int fd) {
        _monitoredEventPromises[fd];
        ++_numRegisteredEvents;

        constexpr int kNumEventsToListenFor = 2;
        struct kevent events[kNumEventsToListenFor];
        // TODO consider EV_CLEAR
        EV_SET(&events[0], fd, int(EventType::kRead), EV_ADD | EV_ENABLE, 0, 0,
               0);
        EV_SET(&events[1], fd, int(EventType::kWrite), EV_ADD | EV_ENABLE, 0, 0,
               0);
        // TODO handle error
        assert(::kevent(_kernelQueue, events, kNumEventsToListenFor, 0, 0, 0) !=
               -1);
        return _monitoredEventPromises[fd].getFuture();
    }

    void run() {
        // Just poll - never block.
        struct timespec timeout = {0, 0};
        loop(_executor, [this, timeout]() mutable {
            if (_numRegisteredEvents > 0) {
                constexpr int kNumEventsPerCycle = 128;
                struct kevent events[kNumEventsPerCycle];
                int nev = kevent(_kernelQueue, 0, 0, events, kNumEventsPerCycle,
                                 &timeout);

                if (nev < 0) {
                    diep("kevent()");
                } else if (nev > 0) {
                    for (auto i = 0; i < nev; i++) {
                        //                        std::cout << "has event: " <<
                        //                        (int)events[i].filter
                        //                                  << std::endl;
                        if (events[i].flags & EV_EOF ||
                            events[i].flags & EV_ERROR) {
                            /* Report errors */
                            fprintf(stderr, "EV_ERROR: %s\n",
                                    strerror(events[i].data));
                            exit(EXIT_FAILURE);
                        }

                        _monitoredEventPromises[events[i].ident].set(events[i]);
                    }
                }
            }
            return true;
        });
    }

   private:
    Executor& _executor;

    int _kernelQueue;

    std::uint64_t _numRegisteredEvents{0};
    std::unordered_map<int, Promise<struct kevent>> _monitoredEventPromises;
};

