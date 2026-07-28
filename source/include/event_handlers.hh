#ifndef EVENT_HANDLERS_HH
#define EVENT_HANDLERS_HH

#include <expected>
#include <iostream>
#include <map>
#include <memory>
#include <unistd.h>

#include "owned_file_descriptor.hh"

namespace ndisc
{
    class EventHandler;

    class EventManager
    {
        std::map<uint64_t, std::weak_ptr<EventHandler>> registered_events_;
        uint64_t event_id_counter_ = 1;
        OwnedFileDescriptor epfd_;

        EventManager(OwnedFileDescriptor &&epfd) : epfd_(std::move(epfd))
        {
        }

    public:
        static std::expected<EventManager, int> Create();

        std::expected<size_t, int> Add(const std::shared_ptr<EventHandler> &handler);

        std::expected<void, int> Remove(size_t handler_id);

        static constexpr int MAX_CONCURRENT_EVENTS = 10;
        static constexpr int EPOLL_TIMEOUT = 100;
        void ProcessEvents();
    };

    class EventHandler
    {
        friend class EventManager;
        EventManager *event_manager_ = nullptr;
        uint64_t handle_ = 0;

    public:
        virtual int GetSocket() const = 0;
        virtual void Call() = 0;
        virtual uint32_t GetEvents() const = 0;
        EventHandler() = default;
        EventHandler(const EventHandler &) = delete;
        EventHandler(EventHandler &&other) noexcept : event_manager_(other.event_manager_),
                                                      handle_(other.handle_)
        {
            other.event_manager_ = nullptr;
            other.handle_ = 0;
        }
        EventHandler &operator=(const EventHandler &) = delete;
        EventHandler &operator=(EventHandler &&other) noexcept
        {
            event_manager_ = other.event_manager_;
            handle_ = other.handle_;
            other.event_manager_ = nullptr;
            other.handle_ = 0;
            return *this;
        }
        virtual ~EventHandler()
        {
            if (event_manager_ != nullptr && handle_ != 0)
            {
                std::expected<void, int> remove_result = event_manager_->Remove(handle_);
                if (!remove_result.has_value())
                {
                    std::cerr << "Failed to remove event_manager, errno: " << remove_result.error() << "\n";
                }
            }
        }
    };

} // namespace ndisc

#endif