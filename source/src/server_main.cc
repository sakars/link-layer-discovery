
#include <chrono>
#include <execinfo.h>
#include <iomanip>
#include <iostream>
#include <poll.h>
#include <queue>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/signalfd.h>
#include <thread>
#include <unistd.h>

#include "client.hh"
#include "clock_handler.hh"
#include "device_repository.hh"
#include "lldp_monitor.hh"
#include "lldp_repository.hh"
#include "netlink_monitor.hh"
#include "owned_file_descriptor.hh"

using namespace std::chrono_literals;

class InterruptHandler : public ndisc::EventHandler
{
    ndisc::OwnedFileDescriptor socket_;
    bool *interrupt_flag_;

    InterruptHandler(ndisc::OwnedFileDescriptor &&socket, bool *interrupt_flag) : socket_(std::move(socket)), interrupt_flag_(interrupt_flag)
    {
    }

public:
    static std::expected<std::shared_ptr<InterruptHandler>, int> Create(bool *interrupt)
    {
        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGINT);
        sigaddset(&mask, SIGTERM);

        if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1)
        {
            std::cerr << "Failed to block default handling for signals";
            return std::unexpected(errno);
        }
        int socket = signalfd(-1, &mask, 0);
        if (socket == -1)
        {
            std::cerr << "Failed to create signalfd.";
            return std::unexpected(errno);
        }
        return std::make_shared<InterruptHandler>(InterruptHandler(socket, interrupt));
    }

    int GetSocket() const override
    {
        return *socket_;
    }
    void Call() override
    {
        signalfd_siginfo signal{};
        ssize_t bytes_read = read(*socket_, &signal, sizeof(signal));
        if (bytes_read < 0)
        {
            std::cerr << "Interrupt handler failed to read signal, errno: " << errno << "\n";
            return;
        }
        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGINT);
        sigaddset(&mask, SIGTERM);

        if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1)
        {
            std::cerr << "Failed to unblock default handling for signals\n";
        }
        *interrupt_flag_ = true;
    }
    uint32_t GetEvents() const override
    {
        return POLLIN;
    }
};

template <typename T, typename S>
inline T unwrapOrLog(std::expected<T, int> value, const S &message)
{
    if (!value.has_value())
    {
        std::cerr << message << "\terrno: " << value.error() << "\n"; // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        std::cerr.flush();
        exit(-1);
    }
    return std::move(value.value());
}

template <typename S>
inline void unwrapOrLog(std::expected<void, int> value, const S &message)
{
    if (!value.has_value())
    {
        std::cerr << message << "\terrno: " << value.error() << "\n"; // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        std::cerr.flush();
        exit(-1);
    }
}

std::vector<std::shared_ptr<ndisc::EventHandler>> initializeHandlers(
    ndisc::EventManager &manager,
    ndisc::NeighbourList &neighbour_list,
    ndisc::LldpRepository &lldp,
    bool &interrupt_flag)
{
    std::shared_ptr<ndisc::EthernetLldpMonitor> monitor =
        unwrapOrLog(
            ndisc::EthernetLldpMonitor::Create(std::bind_front(ndisc::lldpFrameParser, std::ref(neighbour_list))),
            "Failed to create ethernet lldp monitor");

    std::shared_ptr<client::ClientRepository> dtr =
        unwrapOrLog(
            client::ClientRepository::Create(manager),
            "Failed to create ClientRepository");

    std::shared_ptr<client::ClientListenSocket> dtls =
        unwrapOrLog(
            client::ClientListenSocket::Create(neighbour_list, *dtr),
            "Failed to create ClientListenSocket");

    std::shared_ptr<ndisc::ClockHandler> clock =
        unwrapOrLog(
            ndisc::ClockHandler::Create(neighbour_list, lldp),
            "Failed to create ClockHandler");

    std::shared_ptr<InterruptHandler> interrupt_handler =
        unwrapOrLog(
            InterruptHandler::Create(&interrupt_flag),
            "Failed to create InterruptHandler");

    std::vector<std::shared_ptr<ndisc::EventHandler>> handlers{
        clock,
        monitor,
        dtr,
        dtls,
        interrupt_handler,
    };
    for (const std::shared_ptr<ndisc::EventHandler> &handler : handlers)
    {
        unwrapOrLog(
            manager.Add(handler),
            "Failed to add event handler to event manager");
    }

    return handlers;
}

int main()
{
    ndisc::EventManager manager =
        unwrapOrLog(
            ndisc::EventManager::Create(),
            "Failed to initialize EventManager");

    ndisc::NeighbourList neighbour_list;

    ndisc::LldpRepository lldp =
        unwrapOrLog(
            ndisc::LldpRepository::Create(manager),
            "Failed to create LldpRepository");

    bool interrupt_flag = false;

    std::vector<std::shared_ptr<ndisc::EventHandler>> handlers = initializeHandlers(manager, neighbour_list, lldp, interrupt_flag);

    while (!interrupt_flag) // NOLINT(bugprone-infinite-loop)
    {
        manager.ProcessEvents();
    }

    for (const std::shared_ptr<ndisc::EventHandler> &handler : handlers)
    {
        unwrapOrLog(
            manager.Remove(handler),
            "Failed to remove event from event manager");
    }

    std::cout << "\n\nExiting gracefully...\n";
    std::cout.flush();
}
