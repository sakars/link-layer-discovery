#ifndef EVENT_HANDLERS_HH
#define EVENT_HANDLERS_HH

#include <array>
#include <expected>
#include <iostream>
#include <linux/version.h>
#include <map>
#include <memory>
#include <optional>
#include <sys/epoll.h>
#include <unistd.h>
#include <vector>

static_assert(LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 9), "epoll_ctl bug https://man7.org/linux/man-pages/man2/epoll_ctl.2.html#BUGS");

static_assert(LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 27), "epoll_create1 missing");

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
        std::map<uint64_t, std::weak_ptr<EventHandler>> registered_events_;
        uint64_t event_id_counter_ = 1;
        int epfd_ = -1;

        EventManager(int epfd) : epfd_(epfd)
        {
        }

    public:
        EventManager(const EventManager &) = delete;

        EventManager(EventManager &&other) noexcept
        {
            if (epfd_ >= 0)
            {
                close(epfd_);
            }
            epfd_ = other.epfd_;
            other.epfd_ = -1;
            registered_events_ = std::move(other.registered_events_);
            event_id_counter_ = other.event_id_counter_;
            other.registered_events_.clear();
            other.event_id_counter_ = 1;
        }

        EventManager &operator=(const EventManager &) = delete;

        EventManager &operator=(EventManager &&other) noexcept
        {
            if (epfd_ >= 0)
            {
                close(epfd_);
            }
            epfd_ = other.epfd_;
            other.epfd_ = -1;
            registered_events_ = std::move(other.registered_events_);
            event_id_counter_ = other.event_id_counter_;
            other.registered_events_.clear();
            other.event_id_counter_ = 1;
            return *this;
        }

        ~EventManager()
        {
            if (epfd_ >= 0)
            {
                close(epfd_);
            }
        }

        static std::expected<EventManager, int> Create()
        {
            int epfd = epoll_create1(0);
            if (epfd == -1)
            {
                return std::unexpected(errno);
            }

            return EventManager(epfd);
        }

        std::expected<uint64_t, int> Add(const std::shared_ptr<EventHandler> &handler)
        {
            while (registered_events_.contains(event_id_counter_))
            {
                event_id_counter_++;
            }
            epoll_event event{};
            event.data.u64 = event_id_counter_;
            event.events = handler->GetEvents();
            int return_value = epoll_ctl(epfd_, EPOLL_CTL_ADD, handler->GetSocket(), &event);
            if (return_value != 0)
            {
                return std::unexpected(errno);
            }
            this->registered_events_[event_id_counter_] = handler;
            return event_id_counter_++;
        }

        std::expected<void, int> Remove(uint64_t handler_id)
        {
            if (!registered_events_.contains(handler_id))
            {
                return std::unexpected(ENOENT);
            }
            if (std::shared_ptr<EventHandler> event_handler = registered_events_[handler_id].lock())
            {
                int return_value = epoll_ctl(epfd_, EPOLL_CTL_DEL, event_handler->GetSocket(), nullptr);
                if (return_value != 0)
                {
                    return std::unexpected(errno);
                }
            }
            registered_events_.erase(handler_id);
            return {};
        }

        static constexpr int MAX_CONCURRENT_EVENTS = 10;
        static constexpr int EPOLL_TIMEOUT = 100;
        void Wait()
        {
            std::vector<uint64_t> expired_handlers{};
            for (const auto &[handler_id, event_handler] : registered_events_)
            {
                if (event_handler.expired())
                {
                    expired_handlers.push_back(handler_id);
                }
            }
            for (const uint64_t &handler_id : expired_handlers)
            {
                std::expected<void, int> remove_result = Remove(handler_id);
                if (!remove_result.has_value())
                {
                    std::cerr << "Failed to remove handler: " << remove_result.error() << "\n";
                }
            }
            std::array<epoll_event, MAX_CONCURRENT_EVENTS> events{};
            int fds_ready = epoll_wait(epfd_, events.data(), MAX_CONCURRENT_EVENTS, EPOLL_TIMEOUT);
            if (fds_ready < 0)
            {
                std::cerr << "Epoll wait errored: " << errno << "\n";
            }
            for (int i = 0; i < fds_ready; i++)
            {
                uint64_t handle = events.at(i).data.u64;
                if (registered_events_.contains(handle))
                {
                    std::shared_ptr<EventHandler> handler = registered_events_.at(handle).lock();
                    if (handler != nullptr)
                    {
                        handler->Call();
                    }
                }
            }
        }
    };
} // namespace ndisc

#endif