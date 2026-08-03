#include "clock_handler.hh"

#include <cstring>
#include <sys/timerfd.h>

namespace ndisc
{
    constexpr int DUMP_INFO_TIMEOUT = 5;

    constexpr std::string_view CLEAR_SCREEN = "\033[2J";

    static void dumpDevice(const netlink::DeviceData &device)
    {
        std::cout << device.if_index << '\t'
                  << device.interface_name.value_or("---") << '\t';
        if (!device.device_operational)
        {
            std::cout << "Device down\n";
            return;
        }
        if (device.mac_address.has_value())
        {
            lldp::logMac(*device.mac_address);
        }
        else
        {
            std::cout << "None";
        }
        std::cout << "\t";
        lldp::logIpv4(device.ipv4_address);
        std::cout << "\t";
        lldp::logIpv6(device.ipv6_address);
        std::cout << "\n";
    }

    static void dumpNeighbourEntry(const ndisc::NeighbourEntry &neighbour)
    {
        lldp::logPort(neighbour.port_id);
        std::cout << "\t";
        std::cout << neighbour.time_to_live << "\t";
        lldp::logIpv4(neighbour.ipv4_address);
        std::cout << "\t";
        lldp::logIpv6(neighbour.ipv6_address);
        std::cout << "\t";
    }

    void ClockHandler::DumpInfo()
    {
        std::cout << CLEAR_SCREEN;
        std::cout << "My Id: " << netlink::getMachineId() << "\n";
        std::cout << "Device data:\n";
        std::cout << "IDX\t"
                  << "Name\t"
                  << "MAC\t"
                  << "IPv4\t"
                  << "IPv6\t"
                  << '\n';
        for (const auto &[idx, sender] : lldp_repository_->GetDeviceInfo())
        {
            dumpDevice(sender.GetDeviceData());
        }
        std::cout << "\n\n";
        std::cout << "Found " << neighbour_list_->chassis_map.size() << " chassis\n";
        std::cout << "\n\nNeighbours:\n";
        std::cout << "Chassis\t"
                  << "Port\t"
                  << "TTL\t"
                  << "IPv4\t"
                  << "IPv6\t"
                  << '\n';

        for (const auto &[chassis_id, port_map] : neighbour_list_->chassis_map)
        {
            std::string_view chassis{reinterpret_cast<const char *>(chassis_id.data()), chassis_id.size()};
            for (const auto &[port_id, neighbour] : port_map)
            {
                std::cout << chassis << "\t";
                dumpNeighbourEntry(neighbour);
                std::cout << '\n';
            }
        }
    }

    void ClockHandler::Call()
    {
        uint64_t times_triggered = 0;
        ssize_t bytes_received = read(*socket_fd_, &times_triggered, sizeof(times_triggered));
        if (bytes_received < 0 || times_triggered == 0)
        {
            return;
        }
        neighbour_list_->Tick(times_triggered);
        lldp_repository_->Tick(times_triggered);
        if (dumping_enabled_)
        {
            if (dump_timer_ > times_triggered)
            {
                dump_timer_ -= times_triggered;
            }
            else
            {
                dump_timer_ = DUMP_INFO_TIMEOUT;
                DumpInfo();
            }
        }
    }

    uint32_t ClockHandler::GetEvents() const
    {
        return EPOLLIN;
    }

    int ClockHandler::GetSocket() const
    {
        return *socket_fd_;
    }

    std::expected<std::unique_ptr<ClockHandler>, int> ClockHandler::Create(NeighbourList &neighbour_list, LldpRepository &lldp_repository, bool verbose)
    {
        int socket_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
        if (socket_fd == -1)
        {
            return std::unexpected(errno);
        }
        itimerspec timer_spec{};
        timer_spec.it_value.tv_nsec = 0;
        timer_spec.it_value.tv_sec = 1;
        timer_spec.it_interval.tv_nsec = 0;
        timer_spec.it_interval.tv_sec = 1;
        if (timerfd_settime(socket_fd, 0, &timer_spec, nullptr) == -1)
        {
            return std::unexpected(errno);
        }
        return std::make_unique<ClockHandler>(ClockHandler(socket_fd, &neighbour_list, &lldp_repository, verbose));
    }
} // namespace ndisc