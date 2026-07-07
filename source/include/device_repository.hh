#ifndef DEVICE_REPOSITORY_HH
#define DEVICE_REPOSITORY_HH

#include <map>
#include <chrono>
#include <optional>
#include <expected>
#include "device_repository.hh"
#include "netlink_monitor.hh"

using namespace std::chrono_literals;

auto packetConverter(const std::function<void(ndisc::NetlinkPacketView)> CALLBACK)
{
    return [CALLBACK](std::span<uint8_t> packet) -> void
    {
        CALLBACK(ndisc::packetViewParser(packet));
    };
}

enum class ReaderState
{
    IDLE,
    READING,
    ERRORED,
};

struct DeviceReader
{

    std::unique_ptr<ndisc::NetlinkSocket> device_reader;
    std::optional<unsigned int> device_sequence_number = std::nullopt;
    ReaderState device_reader_state = ReaderState::IDLE;
    std::chrono::time_point<std::chrono::steady_clock> scheduled_link_dump = std::chrono::steady_clock::now();

    bool devices_updated = false;

    DeviceReader()
    {
    }

    void Tick()
    {
        if (std::chrono::steady_clock::now() > scheduled_link_dump)
        {
            device_reader->SendGetLinkDumpMessage();
            device_sequence_number = device_reader->GetSequenceNumber();
            device_reader_state = ReaderState::READING;
            scheduled_link_dump = std::chrono::steady_clock::now() + 2min;
        }
    }

    void ExpiditeLinkDump()
    {
        scheduled_link_dump = std::chrono::steady_clock::now() + 2s;
    }

    void UpdateDeviceList(std::map<unsigned int, ndisc::DeviceData> &devices, ndisc::NetlinkPacketView packet)
    {
        if (device_reader_state != ReaderState::READING)
        {
            return;
        }
        if (!device_sequence_number.has_value())
        {
            return;
        }
        unsigned int sequence_number = std::visit([&](auto packet)
                                                  { return packet.header->nlmsg_seq; }, packet);
        if (sequence_number != device_sequence_number.value())
        {
            return;
        }
        if (ndisc::LinkView *link_message = std::get_if<ndisc::LinkView>(&packet))
        {
            if (link_message->header->nlmsg_type == RTM_NEWLINK && link_message->content.interface_info->ifi_type == ARPHRD_ETHER)
            {
                int index = link_message->content.interface_info->ifi_index;
                ndisc::DeviceData &device = devices[index];
                device.if_index = index;
                for (const ndisc::TLVView attribute : link_message->content.attributes)
                {
                    if (attribute.attribute_header->rta_type == IFLA_IFNAME)
                    {
                        if (attribute.value.size() > 1)
                        {

                            device.interface_name = std::string(attribute.value.begin(), attribute.value.end());
                            if (device.interface_name->size() > 0 && device.interface_name->back() == '\0')
                            {
                                device.interface_name->resize(device.interface_name->size() - 1);
                            }
                        }
                    }
                    else if (attribute.attribute_header->rta_type == IFLA_ADDRESS)
                    {
                        if (attribute.value.size() == 6)
                        {
                            device.mac_address = std::array<uint8_t, 6>{};
                            std::copy(attribute.value.begin(), attribute.value.end(), device.mac_address.value().begin());
                        }
                        else
                        {
                            std::ios_base::fmtflags f(std::cerr.flags());
                            std::cerr << "Unexpected size for address payload " << attribute.value.size() << "\n";
                            std::cerr << "Payload:";
                            for (int x : attribute.value)
                            {
                                std::cerr << " " << std::setfill('0') << std::setw(2) << std::hex << x << std::dec;
                            }
                            std::cerr << "\n";
                            std::cerr.flags(f);
                        }
                    }
                }
                // expiditeAddrDump();
                devices_updated = true;
            }
        }
        else if (std::get_if<ndisc::DoneView>(&packet) != nullptr)
        {
            device_sequence_number = std::nullopt;
            device_reader_state = ReaderState::IDLE;
        }
        else if (std::get_if<ndisc::ErrorView>(&packet) != nullptr)
        {
            // std::cerr << "Failed to get link dump. Retrying...\n";
            device_reader_state = ReaderState::ERRORED;
        }
    }
};

struct IpReader
{
    ReaderState ip_reader_state = ReaderState::IDLE;
    std::optional<unsigned int> ip_sequence_number = std::nullopt;
    std::chrono::time_point<std::chrono::steady_clock> scheduled_addr_dump = std::chrono::steady_clock::now() + 2min;
    std::unique_ptr<ndisc::NetlinkSocket> ip_reader;

    void Tick()
    {
        if (std::chrono::steady_clock::now() > scheduled_addr_dump)
        {
            ip_reader->SendGetAddrMessage();
            ip_sequence_number = ip_reader->GetSequenceNumber();
            ip_reader_state = ReaderState::READING;
            scheduled_addr_dump = std::chrono::steady_clock::now() + 10s;
        }
    }

    void ExpiditeAddrDump()
    {
        scheduled_addr_dump = std::chrono::steady_clock::now() + 2s;
    }

    void UpdateAddressList(std::map<unsigned int, ndisc::DeviceData> &devices, ndisc::NetlinkPacketView packet)
    {
        if (ip_reader_state != ReaderState::READING)
        {
            return;
        }
        if (!ip_sequence_number.has_value())
        {
            return;
        }
        unsigned int sequence_number = std::visit([&](auto packet)
                                                  { return packet.header->nlmsg_seq; }, packet);
        if (sequence_number > ip_sequence_number.value())
        {
            std::cerr << "Sequence number somehow larger\n";
        }
        if (sequence_number != ip_sequence_number.value())
        {
            return;
        }
        if (ndisc::AddrView *link_message = std::get_if<ndisc::AddrView>(&packet))
        {
            if (link_message->header->nlmsg_type == RTM_NEWADDR)
            {
                if (link_message->content.address_info->ifa_family != AF_INET)
                {
                    return;
                }
                if ((link_message->content.address_info->ifa_flags & IFA_F_SECONDARY) != 0)
                {
                    return;
                }
                unsigned int index = link_message->content.address_info->ifa_index;
                devices[index].if_index = index;
                for (const ndisc::TLVView attribute : link_message->content.attributes)
                {
                    if (attribute.attribute_header->rta_type == IFA_ADDRESS)
                    {
                        std::cout << "Address found for " << devices[index].if_index << "\n";
                        if (attribute.value.size() == 4)
                        {
                            devices[index].ip_address = std::array<uint8_t, 4>{};
                            std::copy(attribute.value.begin(), attribute.value.end(), devices[index].ip_address.value().begin());
                        }
                    }
                }
            }
        }
        else if (std::get_if<ndisc::DoneView>(&packet))
        {
            ip_sequence_number = std::nullopt;
            ip_reader_state = ReaderState::IDLE;
        }
        else if (ndisc::ErrorView *error_view = std::get_if<ndisc::ErrorView>(&packet))
        {
            std::cerr << "Failed to get address dump. " << error_view->error->error << " Retrying...\n";
            ip_reader_state = ReaderState::ERRORED;
            ExpiditeAddrDump();
        }
    }
};

// TODO: Prevent type erasure on NetlinkSockets
struct DeviceRepository
{

    std::unique_ptr<ndisc::NetlinkSocket> monitor;

    DeviceReader device_reader;
    IpReader ip_reader;
    std::map<unsigned int, ndisc::DeviceData> devices;

    static std::expected<DeviceRepository, int> Create(ndisc::EventManager &manager)
    {
        DeviceRepository repository;
        std::expected<std::unique_ptr<ndisc::NetlinkSocket>, int> monitor_socket = ndisc::NetlinkSocket::Create(packetConverter([&repository](ndisc::NetlinkPacketView packet)
                                                                                                                                { repository.HandleMonitorPackets(packet); }),
                                                                                                                RTMGRP_LINK | RTMGRP_IPV4_IFADDR);
        if (!monitor_socket.has_value() || monitor_socket.value() == nullptr)
        {
            return std::unexpected(monitor_socket.error());
        }
        repository.monitor = std::move(monitor_socket.value());
        std::expected<std::unique_ptr<ndisc::NetlinkSocket>, int> device_socket = ndisc::NetlinkSocket::Create(packetConverter([&repository](ndisc::NetlinkPacketView packet)
                                                                                                                               { repository.UpdateDeviceList(packet); }),
                                                                                                               0);

        if (!device_socket.has_value() || device_socket.value() == nullptr)
        {
            return std::unexpected(device_socket.error());
        }
        repository.device_reader.device_reader = std::move(device_socket.value());

        std::expected<std::unique_ptr<ndisc::NetlinkSocket>, int> ip_socket = ndisc::NetlinkSocket::Create(packetConverter([&repository](ndisc::NetlinkPacketView packet)
                                                                                                                           { repository.UpdateAddressList(std::move(packet)); }),
                                                                                                           0);
        if (!ip_socket.has_value() || ip_socket.value() == nullptr)
        {
            return std::unexpected(ip_socket.error());
        }
        manager.Add(*repository.device_reader.device_reader);
        manager.Add(*repository.monitor);
        manager.Add(*repository.ip_reader.ip_reader);

        return repository;
    }

    void Tick()
    {
        device_reader.Tick();
        ip_reader.Tick();
    }

    void HandleMonitorPackets(ndisc::NetlinkPacketView packet)
    {
        if (std::get_if<ndisc::LinkView>(&packet) != nullptr || std::get_if<ndisc::AddrView>(&packet) != nullptr)
        {
            device_reader.ExpiditeLinkDump();
        }
    }

    void UpdateDeviceList(ndisc::NetlinkPacketView packet)
    {
        device_reader.UpdateDeviceList(devices, std::move(packet));
        if (device_reader.devices_updated)
        {
            device_reader.devices_updated = false;
            ip_reader.ExpiditeAddrDump();
        }
    }

    void UpdateAddressList(ndisc::NetlinkPacketView packet)
    {
        ip_reader.UpdateAddressList(devices, std::move(packet));
    }
};

#endif // DEVICE_REPOSITORY_HH