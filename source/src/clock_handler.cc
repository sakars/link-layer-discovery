#include "clock_handler.hh"

#include <cstring>
#include <sys/timerfd.h>

namespace ndisc
{
    void ClockHandler::DumpInfo()
    {
        std::cout << "\033[2J";
        std::cout << "Device data:\n";
        std::cout << std::left
                  << std::setw(4) << "IDX"
                  << std::setw(24) << "Name"
                  << std::setw(18) << "MAC"
                  << std::setw(16) << "IPv4"
                  << "IPv6"
                  << '\n';
        for (const auto &[idx, sender] : lldp_repository_->GetDeviceInfo())
        {
            const netlink::DeviceData &device = sender.GetDeviceData();
            std::cout << std::left << std::setw(4) << device.if_index
                      << std::setw(24) << device.interface_name.value_or("---");
            if (device.mac_address.has_value())
            {
                std::cout << std::right << std::hex << std::setfill('0')
                          << std::setw(2) << (uint16_t)(*device.mac_address)[0] << ":"
                          << std::setw(2) << (uint16_t)(*device.mac_address)[1] << ":"
                          << std::setw(2) << (uint16_t)(*device.mac_address)[2] << ":"
                          << std::setw(2) << (uint16_t)(*device.mac_address)[3] << ":"
                          << std::setw(2) << (uint16_t)(*device.mac_address)[4] << ":"
                          << std::setw(2) << (uint16_t)(*device.mac_address)[5] << " "
                          << std::dec << std::setfill(' ');
            }
            else
            {
                std::cout << std::setw(18) << "";
            }
            if (device.ipv4_address.has_value())
            {
                std::cout << std::left
                          << std::setw(3) << (uint16_t)(*device.ipv4_address)[0] << '.'
                          << std::setw(3) << (uint16_t)(*device.ipv4_address)[1] << '.'
                          << std::setw(3) << (uint16_t)(*device.ipv4_address)[2] << '.'
                          << std::setw(3) << (uint16_t)(*device.ipv4_address)[3] << ' ';
            }
            else
            {
                std::cout << std::left << std::setw(16) << "Unknown";
            }
            if (device.ipv6_address.has_value())
            {
                std::cout << std::left << std::hex;
                std::cout << std::to_integer<uint16_t>((*device.ipv6_address)[0])
                          << std::to_integer<uint16_t>((*device.ipv6_address)[1]);
                for (size_t i = 2; i < device.ipv6_address->size(); i += 2)
                {
                    std::cout << "::"
                              << std::to_integer<uint16_t>((*device.ipv6_address)[i])
                              << std::to_integer<uint16_t>((*device.ipv6_address)[i + 1]);
                }
                std::cout << std::dec;
            }
            else
            {
                std::cout << "Unknown";
            }
            std::cout << "\n";
        }
        std::cout << "\n\n";
        std::cout << "Found " << neighbour_list_->chassis_map.size() << " chassis\n";
        std::cout << "\n\nNeighbours:\n";
        std::cout << std::left
                  << std::setw(36) << "Chassis"
                  << std::setw(18) << "Port"
                  << std::setw(12) << "TTL"
                  << std::setw(16) << "IPv4"
                  << "IPv6"
                  << '\n';

        for (const auto &[chassis_id, port_map] : neighbour_list_->chassis_map)
        {
            std::string chassis;
            chassis.resize(chassis_id.size() - 2);
            std::memcpy(chassis.data(), chassis_id.data(), chassis_id.size() - 2);
            std::cout << "Chassis: " << chassis << "\n";
            for (const auto &[port_id, neighbour] : port_map)
            {
                std::cout << std::setw(36) << chassis;
                std::cout << std::right << std::hex << std::setfill('0')
                          << std::setw(2) << (uint16_t)(neighbour.port_id)[0] << ":"
                          << std::setw(2) << (uint16_t)(neighbour.port_id)[1] << ":"
                          << std::setw(2) << (uint16_t)(neighbour.port_id)[2] << ":"
                          << std::setw(2) << (uint16_t)(neighbour.port_id)[3] << ":"
                          << std::setw(2) << (uint16_t)(neighbour.port_id)[4] << ":"
                          << std::setw(2) << (uint16_t)(neighbour.port_id)[5] << " "
                          << std::dec << std::setfill(' ');
                std::cout << std::setw(12) << neighbour.time_to_live;
                if (neighbour.ipv4_address.has_value())
                {
                    std::cout << std::left
                              << std::setw(3) << (uint16_t)(*neighbour.ipv4_address)[0] << '.'
                              << std::setw(3) << (uint16_t)(*neighbour.ipv4_address)[1] << '.'
                              << std::setw(3) << (uint16_t)(*neighbour.ipv4_address)[2] << '.'
                              << std::setw(3) << (uint16_t)(*neighbour.ipv4_address)[3] << ' ';
                }
                else
                {
                    std::cout << std::left << std::setw(16) << "Unknown";
                }
                if (neighbour.ipv6_address.has_value())
                {
                    std::cout << std::left << std::hex;
                    std::cout << std::to_integer<uint16_t>((*neighbour.ipv6_address)[0])
                              << std::to_integer<uint16_t>((*neighbour.ipv6_address)[1]);
                    for (size_t i = 2; i < neighbour.ipv6_address->size(); i += 2)
                    {
                        std::cout << "::"
                                  << std::to_integer<uint16_t>((*neighbour.ipv6_address)[i])
                                  << std::to_integer<uint16_t>((*neighbour.ipv6_address)[i + 1]);
                    }
                    std::cout << std::dec;
                }
                else
                {
                    std::cout << "Unknown";
                }
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
        if (dump_timer_ > times_triggered)
        {
            dump_timer_ -= times_triggered;
        }
        else
        {
            dump_timer_ = 5;
            DumpInfo();
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

    std::unique_ptr<ClockHandler> ClockHandler::Create(NeighbourList &neighbour_list, LldpRepository &lldp_repository)
    {
        int socket_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
        if (socket_fd < 0)
        {
            return nullptr;
        }
        itimerspec timer_spec{};
        timer_spec.it_value.tv_nsec = 0;
        timer_spec.it_value.tv_sec = 1;
        timer_spec.it_interval.tv_nsec = 0;
        timer_spec.it_interval.tv_sec = 1;
        timerfd_settime(socket_fd, 0, &timer_spec, nullptr);
        return std::make_unique<ClockHandler>(ClockHandler(socket_fd, &neighbour_list, &lldp_repository));
    }
} // namespace ndisc