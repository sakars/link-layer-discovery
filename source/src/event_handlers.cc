

#include "event_handlers.hh"

#include <array>
#include <iostream>
#include <linux/version.h>
#include <optional>
#include <sys/epoll.h>
#include <vector>

static_assert(LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 9), "epoll_ctl bug https://man7.org/linux/man-pages/man2/epoll_ctl.2.html#BUGS");

static_assert(LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 27), "epoll_create1 missing");

namespace ndisc
{
    EventManager::EventManager(EventManager &&other) noexcept : registered_events_(std::move(other.registered_events_)),
                                                                event_id_counter_(other.event_id_counter_),
                                                                epfd_(std::move(other.epfd_))
    {
        other.event_id_counter_ = 1;
        for (const auto &[handle, event_handler] : registered_events_)
        {
            if (std::shared_ptr<EventHandler> handler = event_handler.lock())
            {
                handler->event_manager_ = this;
                handler->handle_ = handle;
            }
        }
    }

    EventManager &EventManager::operator=(EventManager &&other) noexcept
    {
        registered_events_ = std::move(other.registered_events_);
        event_id_counter_ = other.event_id_counter_;
        epfd_ = std::move(other.epfd_);
        other.event_id_counter_ = 1;
        for (const auto &[handle, event_handler] : registered_events_)
        {
            if (std::shared_ptr<EventHandler> handler = event_handler.lock())
            {
                handler->event_manager_ = this;
                handler->handle_ = handle;
            }
        }
        return *this;
    }

    std::expected<EventManager, int> EventManager::Create()
    {
        OwnedFileDescriptor epfd{epoll_create1(0)};
        if (!epfd.IsValid())
        {
            return std::unexpected(errno);
        }

        return EventManager(std::move(epfd));
    }

    std::expected<size_t, int> EventManager::Add(const std::shared_ptr<EventHandler> &handler)
    {
        if (handler->event_manager_ != nullptr && handler->handle_ != 0)
        {
            std::expected<void, int> remove_result = handler->event_manager_->Remove(handler->handle_);
            if (!remove_result.has_value())
            {
                std::cerr << "Failed to remove event handler from old event manager, errno: " << remove_result.error() << "\n";
            }
        }
        while (registered_events_.contains(event_id_counter_))
        {
            event_id_counter_++;
        }
        epoll_event event{};
        event.data.u64 = event_id_counter_;
        event.events = handler->GetEvents();
        int return_value = epoll_ctl(*epfd_, EPOLL_CTL_ADD, handler->GetSocket(), &event);
        if (return_value != 0)
        {
            return std::unexpected(errno);
        }
        this->registered_events_[event_id_counter_] = handler;
        handler->event_manager_ = this;
        handler->handle_ = event_id_counter_;
        return event_id_counter_++;
    }

    std::expected<void, int> EventManager::Remove(size_t handler_id)
    {
        if (!registered_events_.contains(handler_id))
        {
            return std::unexpected(ENOENT);
        }
        if (std::shared_ptr<EventHandler> event_handler = registered_events_[handler_id].lock())
        {
            int return_value = epoll_ctl(*epfd_, EPOLL_CTL_DEL, event_handler->GetSocket(), nullptr);
            if (return_value != 0)
            {
                return std::unexpected(errno);
            }
            event_handler->event_manager_ = nullptr;
            event_handler->handle_ = 0;
        }
        registered_events_.erase(handler_id);
        return {};
    }

    void EventManager::ProcessEvents()
    {
        std::vector<size_t> expired_handlers{};
        for (const auto &[handler_id, event_handler] : registered_events_)
        {
            if (event_handler.expired())
            {
                expired_handlers.push_back(handler_id);
            }
        }
        for (const size_t &handler_id : expired_handlers)
        {
            std::expected<void, int> remove_result = Remove(handler_id);
            if (!remove_result.has_value())
            {
                std::cerr << "Failed to remove handler: " << remove_result.error() << "\n";
            }
        }
        std::array<epoll_event, MAX_CONCURRENT_EVENTS> events{};
        int fds_ready = epoll_wait(*epfd_, events.data(), MAX_CONCURRENT_EVENTS, EPOLL_TIMEOUT);
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
} // namespace ndisc