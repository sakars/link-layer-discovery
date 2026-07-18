
#include "device_repository.hh"

#include <iostream>
#include <net/if_arp.h>

namespace ndisc
{
    constexpr int DEVICE_READER_DUMP_TIMER = 120;
    constexpr int DEVICE_READER_EXPEDITE_TIMER = 2;
    constexpr int IP_READER_DUMP_TIMER = 30;
    constexpr int IP_READER_EXPEDITE_TIMER = 2;

    std::expected<std::shared_ptr<DeviceReader>, int> DeviceReader::Create(EventManager &manager)
    {
        std::expected<std::unique_ptr<NetlinkSocket>, int> device_socket_result = ndisc::NetlinkSocket::Create(0);

        if (!device_socket_result.has_value() || device_socket_result.value() == nullptr)
        {
            return std::unexpected(device_socket_result.error());
        }
        std::shared_ptr<NetlinkSocket> device_socket = std::move(*device_socket_result);
        std::expected<size_t, int> add_result = manager.Add(device_socket);
        if (!add_result.has_value())
        {
            return std::unexpected(add_result.error());
        }
        return std::make_shared<DeviceReader>(DeviceReader(std::move(device_socket)));
    }

    void DeviceReader::ReceivePacket(NetlinkPacketView packet)
    {
        if (reader_state_ != ReaderState::READING)
        {
            std::cerr << "Bad device reader state: ";
            if (reader_state_ == ReaderState::ERRORED)
            {
                std::cerr << "errored\n";
            }
            else if (reader_state_ == ReaderState::IDLE)
            {
                std::cerr << "idle\n";
            }
            else
            {
                std::cerr << "weird " << uint64_t(reader_state_) << "\n";
            }
            return;
        }
        if (!request_sequence_number_.has_value())
        {
            std::cerr << "No seq number?\n";
            return;
        }
        unsigned int sequence_number = std::visit([]<typename T>(NetlinkMessage<T> packet)
                                                  { return packet.header.nlmsg_seq; }, packet);
        if (sequence_number != request_sequence_number_.value())
        {
            std::cerr << "Device sequence number mismatch. Possible bug..\n";
            return;
        }
        if (ndisc::LinkView *link_message = std::get_if<ndisc::LinkView>(&packet))
        {
            if (callback_.has_value())
            {
                (*callback_)(*link_message);
            }
            else
            {
                std::cout << "Link message got skipped as no callback is assigned.\n";
            }
        }
        else if (std::get_if<ndisc::DoneView>(&packet) != nullptr)
        {
            request_sequence_number_ = std::nullopt;
            reader_state_ = ReaderState::IDLE;
        }
        else if (std::get_if<ndisc::ErrorView>(&packet) != nullptr)
        {
            std::cerr << "Failed to get link dump. Retrying...\n";
            reader_state_ = ReaderState::ERRORED;
            ExpediteLinkDump();
        }
    }

    void DeviceReader::BindCallback()
    {
        std::weak_ptr<DeviceReader> weak = weak_from_this();
        reader_socket_->SetCallback(
            [weak](NetlinkPacketView packet)
            {
                std::shared_ptr<DeviceReader> shared = weak.lock();
                if (shared != nullptr)
                {
                    shared->ReceivePacket(std::move(packet));
                }
                else
                {
                    std::cerr << "DeviceReader has been deallocated or not on a shared_ptr\n";
                }
            });
    }

    void DeviceReader::Tick()
    {
        dump_timer_--;
        if (dump_timer_ <= 0)
        {
            reader_socket_->SendGetLinkDumpMessage();
            request_sequence_number_ = reader_socket_->GetSequenceNumber();
            reader_state_ = ReaderState::READING;
            dump_timer_ = DEVICE_READER_DUMP_TIMER;
        }
    }

    void DeviceReader::ExpediteLinkDump()
    {
        dump_timer_ = DEVICE_READER_EXPEDITE_TIMER;
    }

    std::expected<std::shared_ptr<IpReader>, int> IpReader::Create(EventManager &manager)
    {
        std::expected<std::unique_ptr<NetlinkSocket>, int> netlink_socket_result = NetlinkSocket::Create(0);
        if (!netlink_socket_result.has_value() || *netlink_socket_result == nullptr)
        {
            return std::unexpected(netlink_socket_result.error_or(0));
        }
        std::shared_ptr<NetlinkSocket> netlink_socket = std::move(*netlink_socket_result);
        std::expected<size_t, int> add_result = manager.Add(netlink_socket);
        if (!add_result.has_value())
        {
            return std::unexpected(add_result.error());
        }

        return std::make_shared<IpReader>(std::move(IpReader(std::move(netlink_socket))));
    }

    void IpReader::Tick()
    {
        dump_timer_--;
        if (dump_timer_ <= 0)
        {
            reader_socket_->SendGetAddrMessage();
            sequence_number_ = reader_socket_->GetSequenceNumber();
            reader_state_ = ReaderState::READING;
            dump_timer_ = IP_READER_DUMP_TIMER;
        }
    }

    void IpReader::ReceivePacket(NetlinkPacketView packet)
    {
        if (reader_state_ != ReaderState::READING || !sequence_number_.has_value())
        {
            return;
        }
        unsigned int packet_sequence_number = std::visit([&]<typename T>(NetlinkMessage<T> packet)
                                                         { return packet.header.nlmsg_seq; }, packet);
        if (packet_sequence_number != sequence_number_.value())
        {
            return;
        }
        if (ndisc::AddrView *link_message = std::get_if<ndisc::AddrView>(&packet))
        {
            if (!callback_.has_value())
            {
                (*callback_)(*link_message);
            }
            else
            {
                std::cout << "Link message got skipped as no callback is assigned.\n";
            }
        }
        else if (std::get_if<ndisc::DoneView>(&packet) != nullptr)
        {
            sequence_number_ = std::nullopt;
            reader_state_ = ReaderState::IDLE;
        }
        else if (ndisc::ErrorView *error_view = std::get_if<ndisc::ErrorView>(&packet))
        {
            std::cerr << "Failed to get address dump. " << error_view->content.message_error.error << " Retrying...\n";
            reader_state_ = ReaderState::ERRORED;
            ExpediteAddrDump();
        }
    }

    void IpReader::BindCallback()
    {
        std::weak_ptr<IpReader> weak = weak_from_this();
        reader_socket_->SetCallback(
            [weak](NetlinkPacketView packet)
            {
                std::shared_ptr<IpReader> shared = weak.lock();
                if (shared != nullptr)
                {
                    shared->ReceivePacket(std::move(packet));
                }
                else
                {
                    std::cerr << "IpReader has been deallocated or not on a shared_ptr\n";
                }
            });
    }

    void IpReader::ExpediteAddrDump()
    {
        dump_timer_ = IP_READER_EXPEDITE_TIMER;
    }
    std::expected<std::unique_ptr<DeviceRepository>, int> DeviceRepository::Create(EventManager &manager)
    {
        std::expected<std::shared_ptr<NetlinkSocket>, int> monitor_socket = NetlinkSocket::Create(RTMGRP_LINK | RTMGRP_IPV4_IFADDR);
        if (!monitor_socket.has_value() || *monitor_socket == nullptr)
        {
            return std::unexpected(monitor_socket.error_or(0));
        }
        std::expected<std::shared_ptr<DeviceReader>, int> device_reader = DeviceReader::Create(manager);
        if (!device_reader.has_value())
        {
            return std::unexpected(device_reader.error());
        }
        std::expected<std::shared_ptr<IpReader>, int> ip_reader = IpReader::Create(manager);
        if (!ip_reader.has_value())
        {
            return std::unexpected(ip_reader.error());
        }
        return std::make_unique<DeviceRepository>(DeviceRepository(*monitor_socket, *device_reader, *ip_reader));
    }

    void DeviceRepository::Tick()
    {
        device_reader_->Tick();
        ip_reader_->Tick();
    }

    void DeviceRepository::HandleMonitorPackets(ndisc::NetlinkPacketView packet)
    {
        if (std::get_if<LinkView>(&packet) != nullptr)
        {
            device_reader_->ExpediteLinkDump();
        }
        else if (std::get_if<AddrView>(&packet) != nullptr)
        {
            ip_reader_->ExpediteAddrDump();
        }
    }

    void DeviceRepository::HandleLinkPacket(const LinkView &link_message)
    {
        if (link_message.header.nlmsg_type != RTM_NEWLINK || link_message.content.interface_info.ifi_type != ARPHRD_ETHER)
        {
            return;
        }
        int index = link_message.content.interface_info.ifi_index;
        ndisc::DeviceData &device = devices_[index];
        device.if_index = index;
        for (const ndisc::TLVView attribute : link_message.content.attributes)
        {
            if (attribute.attribute_header.rta_type == IFLA_IFNAME && attribute.value.size() > 1)
            {
                device.interface_name = std::string(reinterpret_cast<char *>(attribute.value.data()), attribute.value.size());
                if (!device.interface_name->empty() && device.interface_name->back() == '\0')
                {
                    device.interface_name->resize(device.interface_name->size() - 1);
                }
            }
            else if (attribute.attribute_header.rta_type == IFLA_ADDRESS)
            {
                if (attribute.value.size() == ETH_ALEN)
                {
                    device.mac_address = std::array<std::byte, ETH_ALEN>{};
                    std::copy(attribute.value.begin(), attribute.value.end(), device.mac_address.value().begin());
                }
                else
                {
                    std::ios_base::fmtflags flags(std::cerr.flags());
                    std::cerr << "Unexpected size for address payload " << attribute.value.size() << "\n";
                    std::cerr << "Payload:";
                    for (std::byte byte : attribute.value)
                    {
                        std::cerr << " " << std::setfill('0') << std::setw(2) << std::hex << std::to_integer<unsigned int>(byte) << std::dec;
                    }
                    std::cerr << "\n";
                    std::cerr.flags(flags);
                }
            }
        }
    }

    void DeviceRepository::HandleAddressPacket(const AddrView &address_message)
    {
        if (address_message.header.nlmsg_type == RTM_NEWADDR)
        {
            if (address_message.content.address_info.ifa_family != AF_INET || (address_message.content.address_info.ifa_flags & IFA_F_SECONDARY) != 0)
            {
                return;
            }
            unsigned int index = address_message.content.address_info.ifa_index;
            devices_[index].if_index = index;
            for (const ndisc::TLVView attribute : address_message.content.attributes)
            {
                if (attribute.attribute_header.rta_type == IFA_ADDRESS)
                {
                    if (attribute.value.size() == sizeof(in_addr))
                    {
                        std::optional<std::array<std::byte, sizeof(in_addr)>> &device_ip = devices_[index].ip_address;
                        device_ip = std::array<std::byte, sizeof(in_addr)>{};
                        std::copy(attribute.value.begin(), attribute.value.end(), device_ip->begin());
                    }
                }
            }
        }
    }
} // namespace ndisc