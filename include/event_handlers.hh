#ifndef EVENT_HANDLERS_HH
#define EVENT_HANDLERS_HH
#include <vector>
#include <sys/epoll.h>
#include <optional>
#include <iostream>
#include <unistd.h>

namespace ndisc
{
    class EventHandler
    {
    public:
        virtual void Call() = 0;
        virtual void Register(int epfd) = 0;
        virtual void Deregister(int epfd) = 0;
        EventHandler() = default;
        EventHandler(const EventHandler &) = default;
        EventHandler(EventHandler &&) = default;
        EventHandler &operator=(const EventHandler &) = default;
        EventHandler &operator=(EventHandler &&) = default;
        virtual ~EventHandler() = default;
    };

    class EventManager
    {
        std::vector<EventHandler> registered_handlers_;

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
    };
} // namespace ndisc

#endif