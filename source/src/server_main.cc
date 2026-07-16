
#include <chrono>
#include <execinfo.h>
#include <iomanip>
#include <iostream>
#include <queue>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <thread>
#include <unistd.h>

#include "clock_handler.hh"
#include "data_transport.hh"
#include "device_repository.hh"
#include "lldp_monitor.hh"
#include "lldp_repository.hh"
#include "netlink_monitor.hh"

using namespace std::chrono_literals;

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

    ndisc::LldpRepository lldp;

    std::shared_ptr<ndisc::ClockHandler> clock = ndisc::ClockHandler::Create(neighbour_list, *repository, lldp);
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

    while (true)
    {
        manager.Wait();
    }
}
