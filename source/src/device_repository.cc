
#include "device_repository.hh"

#include <iostream>
#include <net/if_arp.h>

namespace ndisc
{
    std::function<void(std::span<uint8_t>)> packetConverter(std::function<void(ndisc::NetlinkPacketView)> CALLBACK)
    {
        return [CALLBACK](std::span<uint8_t> packet) -> void
        {
            CALLBACK(ndisc::packetViewParser(packet));
        };
    }

    void DeviceReader::Tick()
    {
        if (std::chrono::steady_clock::now() > scheduled_link_dump)
        {
            device_reader->SendGetLinkDumpMessage();
            device_sequence_number = device_reader->GetSequenceNumber();
            device_reader_state = ReaderState::READING;
            scheduled_link_dump = std::chrono::steady_clock::now() + 2min;
        }
    }

    void DeviceReader::ExpediteLinkDump()
    {
        scheduled_link_dump = std::chrono::steady_clock::now() + 2s;
    }

    void DeviceReader::UpdateDeviceList(std::map<unsigned int, ndisc::DeviceData> &devices, ndisc::NetlinkPacketView packet)
    {
        if (device_reader_state != ReaderState::READING)
        {
            std::cerr << "Bad device reader state: ";
            if (device_reader_state == ReaderState::ERRORED)
            {
                std::cerr << "errored\n";
            }
            else if (device_reader_state == ReaderState::IDLE)
            {
                std::cerr << "idle\n";
            }
            else
            {
                std::cerr << "weird " << uint64_t(device_reader_state) << "\n";
            }
            return;
        }
        if (!device_sequence_number.has_value())
        {
            std::cerr << "No seq number?\n";
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
                            if (!device.interface_name->empty() && device.interface_name->back() == '\0')
                            {
                                device.interface_name->resize(device.interface_name->size() - 1);
                            }
                        }
                    }
                    else if (attribute.attribute_header->rta_type == IFLA_ADDRESS)
                    {
                        if (attribute.value.size() == ETH_ALEN)
                        {
                            device.mac_address = std::array<uint8_t, ETH_ALEN>{};
                            std::copy(attribute.value.begin(), attribute.value.end(), device.mac_address.value().begin());
                        }
                        else
                        {
                            std::ios_base::fmtflags flags(std::cerr.flags());
                            std::cerr << "Unexpected size for address payload " << attribute.value.size() << "\n";
                            std::cerr << "Payload:";
                            for (int byte : attribute.value)
                            {
                                std::cerr << " " << std::setfill('0') << std::setw(2) << std::hex << byte << std::dec;
                            }
                            std::cerr << "\n";
                            std::cerr.flags(flags);
                        }
                    }
                }
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

    void IpReader::Tick()
    {
        if (std::chrono::steady_clock::now() > scheduled_addr_dump)
        {
            ip_reader->SendGetAddrMessage();
            ip_sequence_number = ip_reader->GetSequenceNumber();
            ip_reader_state = ReaderState::READING;
            scheduled_addr_dump = std::chrono::steady_clock::now() + 10s;
        }
    }

    void IpReader::ExpediteAddrDump()
    {
        scheduled_addr_dump = std::chrono::steady_clock::now() + 2s;
    }

    void IpReader::UpdateAddressList(std::map<unsigned int, ndisc::DeviceData> &devices, ndisc::NetlinkPacketView &packet)
    {
        if (ip_reader_state != ReaderState::READING || !ip_sequence_number.has_value())
        {
            return;
        }
        unsigned int sequence_number = std::visit([&](auto packet)
                                                  { return packet.header->nlmsg_seq; }, packet);
        if (sequence_number != ip_sequence_number.value())
        {
            return;
        }
        if (ndisc::AddrView *link_message = std::get_if<ndisc::AddrView>(&packet))
        {
            if (link_message->header->nlmsg_type == RTM_NEWADDR)
            {
                if (link_message->content.address_info->ifa_family != AF_INET || (link_message->content.address_info->ifa_flags & IFA_F_SECONDARY) != 0)
                {
                    return;
                }
                unsigned int index = link_message->content.address_info->ifa_index;
                devices[index].if_index = index;
                for (const ndisc::TLVView attribute : link_message->content.attributes)
                {
                    if (attribute.attribute_header->rta_type == IFA_ADDRESS)
                    {
                        if (attribute.value.size() == sizeof(in_addr))
                        {
                            std::optional<std::array<uint8_t, sizeof(in_addr)>> &device_ip = devices[index].ip_address;
                            device_ip = std::array<uint8_t, sizeof(in_addr)>{};
                            std::copy(attribute.value.begin(), attribute.value.end(), device_ip->begin());
                        }
                    }
                }
            }
        }
        else if (std::get_if<ndisc::DoneView>(&packet) != nullptr)
        {
            ip_sequence_number = std::nullopt;
            ip_reader_state = ReaderState::IDLE;
        }
        else if (ndisc::ErrorView *error_view = std::get_if<ndisc::ErrorView>(&packet))
        {
            std::cerr << "Failed to get address dump. " << error_view->error->error << " Retrying...\n";
            ip_reader_state = ReaderState::ERRORED;
            ExpediteAddrDump();
        }
    }

    std::expected<std::unique_ptr<DeviceRepository>, int> DeviceRepository::Create(ndisc::EventManager &manager)
    {
        std::unique_ptr<DeviceRepository> repository_handle = std::make_unique<DeviceRepository>();
        DeviceRepository &repository = *repository_handle;
        std::expected<std::unique_ptr<ndisc::NetlinkSocket>, int> monitor_socket = ndisc::NetlinkSocket::Create(packetConverter([&repository](ndisc::NetlinkPacketView packet)
                                                                                                                                { repository.HandleMonitorPackets(std::move(packet)); }),
                                                                                                                RTMGRP_LINK | RTMGRP_IPV4_IFADDR);
        if (!monitor_socket.has_value() || monitor_socket.value() == nullptr)
        {
            return std::unexpected(monitor_socket.error());
        }
        repository.monitor = std::move(monitor_socket.value());
        std::expected<std::unique_ptr<ndisc::NetlinkSocket>, int> device_socket = ndisc::NetlinkSocket::Create(packetConverter([&repository](ndisc::NetlinkPacketView packet)
                                                                                                                               { repository.UpdateDeviceList(std::move(packet)); }),
                                                                                                               0);

        if (!device_socket.has_value() || device_socket.value() == nullptr)
        {
            return std::unexpected(device_socket.error());
        }
        repository.device_reader.device_reader = std::move(device_socket.value());

        std::expected<std::unique_ptr<ndisc::NetlinkSocket>, int> ip_socket = ndisc::NetlinkSocket::Create(packetConverter([&repository](ndisc::NetlinkPacketView packet)
                                                                                                                           { repository.UpdateAddressList(packet); }),
                                                                                                           0);
        if (!ip_socket.has_value() || ip_socket.value() == nullptr)
        {
            return std::unexpected(ip_socket.error());
        }
        repository.ip_reader.ip_reader = std::move(ip_socket.value());

        std::expected<size_t, int> add_device_reader_result = manager.Add(repository.device_reader.device_reader);
        if (!add_device_reader_result.has_value())
        {
            return std::unexpected(add_device_reader_result.error());
        }
        std::expected<size_t, int> add_monitor_result = manager.Add(repository.monitor);
        if (!add_monitor_result.has_value())
        {
            return std::unexpected(add_monitor_result.error());
        }
        std::expected<size_t, int> ip_reader_result = manager.Add(repository.ip_reader.ip_reader);
        if (!ip_reader_result.has_value())
        {
            return std::unexpected(ip_reader_result.error());
        }

        return repository_handle;
    }

    void DeviceRepository::Tick()
    {
        device_reader.Tick();
        ip_reader.Tick();
    }

    void DeviceRepository::HandleMonitorPackets(ndisc::NetlinkPacketView packet)
    {
        if (std::get_if<ndisc::LinkView>(&packet) != nullptr || std::get_if<ndisc::AddrView>(&packet) != nullptr)
        {
            device_reader.ExpediteLinkDump();
        }
    }

    void DeviceRepository::UpdateDeviceList(ndisc::NetlinkPacketView packet)
    {
        device_reader.UpdateDeviceList(devices, std::move(packet));
        if (device_reader.devices_updated)
        {
            device_reader.devices_updated = false;
            ip_reader.ExpediteAddrDump();
        }
    }

    void DeviceRepository::UpdateAddressList(ndisc::NetlinkPacketView &packet)
    {
        ip_reader.UpdateAddressList(devices, packet);
    }
} // namespace ndisc