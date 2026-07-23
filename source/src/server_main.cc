
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

#include "clock_handler.hh"
#include "data_transport.hh"
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

int main()
{
    std::expected<ndisc::EventManager, int> manager_create_result = ndisc::EventManager::Create();

    if (!manager_create_result.has_value())
    {
        std::cerr << "Failed to initialize Event manager. Errno: " << manager_create_result.error() << "\n";
        return -1;
    }
    ndisc::EventManager manager = std::move(manager_create_result.value());
    std::cout << "Event manager initialized" << "\n";
    std::expected<std::unique_ptr<ndisc::DeviceRepository>, int> repository_result = ndisc::DeviceRepository::Create(manager);
    std::cout << "Device repository initialized" << "\n";
    if (!repository_result.has_value())
    {
        std::cerr << "Failed to create device repository with errno " << repository_result.error();
    }
    std::unique_ptr<ndisc::DeviceRepository> repository = std::move(repository_result.value());
    std::cout << "Device repository initialized" << "\n";

    ndisc::NeighbourList neighbour_list;

    std::shared_ptr<ndisc::EthernetLldpMonitor> monitor = ndisc::EthernetLldpMonitor::Create(std::bind_front(ndisc::lldpFrameParser, std::ref(neighbour_list)));
    if (monitor == nullptr)
    {
        std::cerr << "Monitor is null\n";
        return -1;
    }
    std::expected<uint64_t, int> monitor_add_handle = manager.Add(monitor);
    if (!monitor_add_handle.has_value())
    {
        std::cerr << "Failed to add Ethernet monitor, errno: " << monitor_add_handle.error() << "\n";
        return -1;
    }

    std::cout << "Ethernet LLDP monitor initialized" << "\n";

    std::expected<std::unique_ptr<ndisc::data::DataTransportRepository>, int> dtr_result = ndisc::data::DataTransportRepository::Create(manager);
    if (!dtr_result.has_value())
    {
        std::cerr << "Failed to create DataTransportRepository, errno: " << dtr_result.error() << "\n";
        return -1;
    }
    if (dtr_result.value() == nullptr)
    {
        std::cerr << "DTR for some reason nullptr\n";
        return -1;
    }
    std::shared_ptr<ndisc::data::DataTransportRepository> dtr = std::move(dtr_result.value());

    std::cout << "Data Transport Repository initialized" << "\n";

    std::expected<std::unique_ptr<ndisc::data::DataTransportListenSocket>, int> dtls_result = ndisc::data::DataTransportListenSocket::Create(*repository, neighbour_list, *dtr);
    if (!dtls_result.has_value())
    {
        std::cerr << "Failed to create DataTransportListenSocket, errno: " << dtls_result.error() << "\n";
        return -1;
    }
    if (dtls_result.value() == nullptr)
    {
        std::cerr << "DTLS somehow nullptr\n";
        return -1;
    }
    std::shared_ptr<ndisc::data::DataTransportListenSocket> dtls = std::move(*dtls_result);

    std::cout << "DTLS initialized" << "\n";
    std::expected<ndisc::LldpRepository, int> lldp_create_result = ndisc::LldpRepository::Create(std::move(repository));
    if (!lldp_create_result.has_value())
    {
        std::cerr << "Failed to create LldpRepository\n";
    }
    ndisc::LldpRepository lldp = std::move(*lldp_create_result);

    std::shared_ptr<ndisc::ClockHandler> clock = ndisc::ClockHandler::Create(neighbour_list, lldp);
    if (clock == nullptr)
    {
        std::cerr << "Clock is nullptr\n";
        return -1;
    }
    std::expected<uint64_t, int> clock_add_handle = manager.Add(clock);
    if (!clock_add_handle.has_value())
    {
        std::cerr << "Failed to add Clock monitor, errno: " << clock_add_handle.error() << "\n";
        return -1;
    }

    std::cout << "Clock handler initialized" << "\n";

    std::expected<uint64_t, int> dtr_handle = manager.Add(dtr);
    std::expected<uint64_t, int> dtls_handle = manager.Add(dtls);
    if (!dtr_handle.has_value())
    {
        std::cerr << "Failed to add DTR, errno: " << dtr_handle.error() << "\n";
        return -1;
    }
    if (!dtls_handle.has_value())
    {
        std::cerr << "Failed to add DTLS, errno: " << dtls_handle.error() << "\n";
        return -1;
    }

    std::cout << "Handlers initialized" << "\n";

    bool interrupt_flag = false;
    std::expected<std::shared_ptr<InterruptHandler>, int> interrupt_handler = InterruptHandler::Create(&interrupt_flag);
    if (!interrupt_handler.has_value())
    {
        std::cerr << "Interrupt handler create failed, errno: " << interrupt_handler.error() << "\n";
        return -1;
    }
    std::expected<size_t, int> add_result = manager.Add(*interrupt_handler);
    if (!add_result.has_value())
    {
        std::cerr << "Failed to add interrupt handler.\n";
        return -1;
    }
    while (!interrupt_flag)
    {
        manager.Wait();
    }
    std::cout << "\n\nExiting gracefully...\n";
    std::cout.flush();
}
