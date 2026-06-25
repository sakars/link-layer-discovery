#ifndef EVENT_HANDLERS_HH
#define EVENT_HANDLERS_HH
#include <vector>
#include <sys/epoll.h>
#include <optional>
#include <iostream>
#include <unistd.h>
#include <memory>
#include <array>

namespace ndisc
{
    class EventHandler
    {
    public:
        virtual int GetSocket() const = 0;
        virtual void Call() = 0;
        virtual uint32_t GetEvents() const = 0;
        EventHandler() = default;
        EventHandler(const EventHandler &) = default;
        EventHandler(EventHandler &&) = default;
        EventHandler &operator=(const EventHandler &) = default;
        EventHandler &operator=(EventHandler &&) = default;
        virtual ~EventHandler() = default;
    };

    class EventManager
    {
        std::vector<std::unique_ptr<EventHandler>> registered_handlers_;

        int epfd_ = -1;

        EventManager(int epfd) : epfd_(epfd)
        {
        }

    public:
        EventManager(const EventManager &) = delete;

        EventManager(EventManager &&other)
        {
            std::swap(epfd_, other.epfd_);
        }

        EventManager &operator=(const EventManager &) = delete;

        EventManager &operator=(EventManager &&other)
        {
            std::swap(epfd_, other.epfd_);
            return *this;
        }

        ~EventManager()
        {
            if (epfd_ >= 0)
            {
                close(epfd_);
            }
        }

        static std::optional<EventManager> Create()
        {
            int epfd = epoll_create1(0);
            if (epfd == -1)
            {
                std::cerr << "Failed to create event manager: ";
                switch (errno)
                {
                case EINVAL:
                    std::cerr << "EINVAL";
                    break;
                case EMFILE:
                    std::cerr << "EMFILE";
                    break;
                case ENFILE:
                    std::cerr << "ENFILE";
                    break;
                case ENOMEM:
                    std::cerr << "ENOMEM";
                    break;
                default:
                    std::cerr << "Other (" << errno << ")";
                }
                std::cerr << "\n";
                return std::nullopt;
            }

            return EventManager(epfd);
        }

        void Add(EventHandler &handler)
        {
            epoll_event event{};
            event.data.ptr = &handler;
            event.events = handler.GetEvents();

            epoll_ctl(epfd_, EPOLL_CTL_ADD, handler.GetSocket(), &event);
        }

        static constexpr int MAX_CONCURRENT_EVENTS = 10;
        static constexpr int EPOLL_TIMEOUT = 100;
        void Wait()
        {
            std::array<epoll_event, MAX_CONCURRENT_EVENTS> events{};
            int fds_ready = epoll_wait(epfd_, events.data(), MAX_CONCURRENT_EVENTS, EPOLL_TIMEOUT);
            for (int i = 0; i < fds_ready; i++)
            {
                reinterpret_cast<EventHandler *>(events.at(i).data.ptr)->Call();
            }
        }
    };
} // namespace ndisc

#endif