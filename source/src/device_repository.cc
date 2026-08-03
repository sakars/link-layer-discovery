
#include "device_repository.hh"

#include <iostream>
#include <linux/if.h>
#include <net/if_arp.h>

namespace ndisc
{
    static constexpr int DEVICE_REPOSITORY_SYNC_TIMEOUT = 120;
    static constexpr int DEVICE_REPOSITORY_EXPEDITE_TIMEOUT = 2;

    void DeviceRepository::ScheduleResync()
    {
        sync_timeout_ = DEVICE_REPOSITORY_SYNC_TIMEOUT;
    }

    void DeviceRepository::ScheduleExpediteResync()
    {
        sync_timeout_ = DEVICE_REPOSITORY_EXPEDITE_TIMEOUT;
    }

    void DeviceRepository::RequestDeviceDump()
    {
        if (device_reader_ != nullptr)
        {
            std::expected<void, int> dump_result = device_reader_->TriggerDump();
            if (!dump_result.has_value())
            {
                std::cerr << "Failed to issue device dump. " << dump_result.error() << "\n";
            }
        }
        else
        {
            std::cerr << "Device reader in invalid state\n";
        }
    }

    void DeviceRepository::RequestIpDump()
    {
        if (ip_reader_ != nullptr)
        {
            std::expected<void, int> dump_result = ip_reader_->TriggerDump();
            if (!dump_result.has_value())
            {
                std::cerr << "Failed to issue ip dump. " << dump_result.error() << "\n";
            }
        }
        else
        {
            std::cerr << "Ip reader in invalid state\n";
        }
    }

    void DeviceRepository::DeviceReaderFinished()
    {
        RequestIpDump();
    }

    void DeviceRepository::DeviceReaderErrored()
    {
        std::cerr << "Device reader errored.\n";
        ScheduleExpediteResync();
    }

    void DeviceRepository::IpReaderFinished()
    {
        if (synchronization_callback_.has_value())
        {
            (*synchronization_callback_)(devices_);
        }
        else
        {
            std::cerr << "Warning: DeviceRepository Sync callback is empty.\n";
        }
    }

    void DeviceRepository::IpReaderErrored()
    {
        std::cerr << "Ip reader errored\n";
        ScheduleExpediteResync();
    }

    std::expected<std::unique_ptr<DeviceRepository>, int> DeviceRepository::Create(EventManager &manager)
    {
        std::expected<std::shared_ptr<netlink::NetlinkSocket>, int> monitor_socket = netlink::NetlinkSocket::Create(RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR);
        if (!monitor_socket.has_value() || *monitor_socket == nullptr)
        {
            std::cerr << "Failed to create monitor socket\n";
            return std::unexpected(monitor_socket.error_or(0));
        }
        std::expected<size_t, int> add_result = manager.Add(*monitor_socket);
        if (!add_result.has_value())
        {
            std::cerr << "Failed to add monitor socket\n";
            return std::unexpected(add_result.error());
        }
        std::expected<std::unique_ptr<netlink::DeviceReader>, int> device_reader = netlink::DeviceReader::Create(manager);
        if (!device_reader.has_value())
        {
            std::cerr << "Failed to create device reader\n";
            return std::unexpected(device_reader.error());
        }
        std::expected<std::unique_ptr<netlink::IpReader>, int> ip_reader = netlink::IpReader::Create(manager);
        if (!ip_reader.has_value())
        {
            std::cerr << "Failed to create ip reader\n";
            return std::unexpected(ip_reader.error());
        }
        return std::make_unique<DeviceRepository>(DeviceRepository(*monitor_socket, std::move(*device_reader), std::move(*ip_reader)));
    }

    void DeviceRepository::Tick(const uint64_t &delta_seconds)
    {
        if (device_reader_ == nullptr || ip_reader_ == nullptr)
        {
            std::cerr << "DeviceRepository called Tick on a moved-out-of value\n";
            return;
        }
        device_reader_->Tick(delta_seconds);
        ip_reader_->Tick(delta_seconds);
        if (sync_timeout_ > static_cast<int>(delta_seconds))
        {
            sync_timeout_ -= static_cast<int>(delta_seconds);
        }
        else
        {
            sync_timeout_ = 0;
            RequestDeviceDump();
            ScheduleResync();
        }
    }

    void DeviceRepository::HandleMonitorPackets(const netlink::NetlinkPacketView & /*unused*/)
    {
        std::cout << "Received monitor packet\n";
        ScheduleExpediteResync();
    }

    void DeviceRepository::HandleLinkPacket(const netlink::LinkView &link_message)
    {
        if (link_message.header.nlmsg_type != RTM_NEWLINK || link_message.content.interface_info.ifi_type != ARPHRD_ETHER)
        {
            return;
        }
        int index = link_message.content.interface_info.ifi_index;
        netlink::DeviceData &device = devices_[index];
        device.if_index = index;
        device.device_operational = (link_message.content.interface_info.ifi_flags & IFF_UP) != 0 && (link_message.content.interface_info.ifi_flags & IFF_LOWER_UP) != 0;
        for (const netlink::TLVView attribute : link_message.content.attributes)
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
                    std::ranges::copy(attribute.value, device.mac_address.value().begin());
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

    void DeviceRepository::HandleAddressPacket(const netlink::AddrView &address_message)
    {
        if (address_message.header.nlmsg_type == RTM_NEWADDR)
        {
            if (address_message.content.address_info.ifa_family != AF_INET &&
                address_message.content.address_info.ifa_family != AF_INET6)
            {
                return;
            }
            if ((address_message.content.address_info.ifa_flags & IFA_F_SECONDARY) != 0)
            {
                return;
            }
            unsigned int index = address_message.content.address_info.ifa_index;
            devices_[index].if_index = index;
            for (const netlink::TLVView attribute : address_message.content.attributes)
            {
                if (attribute.attribute_header.rta_type == IFA_ADDRESS)
                {
                    if (address_message.content.address_info.ifa_family == AF_INET &&
                        attribute.value.size() == sizeof(in_addr))
                    {
                        std::optional<std::array<std::byte, sizeof(in_addr)>> &device_ip = devices_[index].ipv4_address;
                        device_ip = std::array<std::byte, sizeof(in_addr)>{};
                        std::ranges::copy(attribute.value, device_ip->begin());
                    }
                    else if (address_message.content.address_info.ifa_family == AF_INET6 &&
                             attribute.value.size() == sizeof(in6_addr))
                    {
                        std::optional<std::array<std::byte, sizeof(in6_addr)>> &device_ipv6 = devices_[index].ipv6_address;
                        device_ipv6 = std::array<std::byte, sizeof(in6_addr)>{};
                        std::ranges::copy(attribute.value, device_ipv6->begin());
                    }
                }
            }
        }
    }
} // namespace ndisc