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

#include "owned_file_descriptor.hh"

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
        void Wait();
    };
} // namespace ndisc

#endif