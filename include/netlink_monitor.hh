#ifndef NETLINK_MONITOR_HH
#define NETLINK_MONITOR_HH

#include "scheduler.hh"
#include "event_handlers.hh"

#include <vector>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if_arp.h>
#include <map>
#include <string>
#include <memory>

namespace ndisc
{
    class NetlinkSocketReader
    {
        int socket_fd_;
        std::vector<uint8_t> data_buffer_;
        std::span<uint8_t> remaining_data_;

    public:
        std::optional<std::span<uint8_t>> Next();

        bool IsReadable() const
        {
            size_t buffer_size = remaining_data_.size();
            const nlmsghdr *header = reinterpret_cast<const nlmsghdr *>(remaining_data_.data());
            if (NLMSG_OK(header, buffer_size))
            {
                return true;
            }
            sockaddr_nl source_address{};
            uint8_t tmp_data = 0;
            iovec buffer_data{
                .iov_base = &tmp_data,
                .iov_len = 1,
            };
            msghdr header_buffer{
                .msg_name = &source_address,
                .msg_namelen = sizeof(source_address),
                .msg_iov = &buffer_data,
                .msg_iovlen = 1,
                .msg_control = nullptr,
                .msg_controllen = 0,
                .msg_flags = 0,
            };

            const long peek_data_length = recvmsg(socket_fd_, &header_buffer, MSG_PEEK | MSG_TRUNC);
            return peek_data_length > 0;
        }

        NetlinkSocketReader(int socket_fd);
    };

    struct TLVView
    {
        rtattr *attribute_header;
        std::span<uint8_t> value;
    };

    struct MessageContentView
    {
        std::span<uint8_t> content;
    };

    struct MessageView
    {
        nlmsghdr *header;
        MessageContentView content;
    };

    struct LinkContentView
    {
        ifinfomsg *interface_info;
        std::vector<TLVView> attributes;
    };

    struct LinkView
    {
        nlmsghdr *header;
        LinkContentView content;
    };

    struct AddrContentView
    {
        ifaddrmsg *address_info;
        std::vector<TLVView> attributes;
    };

    struct AddrView
    {
        nlmsghdr *header;
        AddrContentView content;
    };

    struct DoneView
    {
        nlmsghdr *header;
        int *error;
    };

    // using NetLinkPacketContentView =
    //     std::variant<
    //         MessageContentView,
    //         LinkContentView,
    //         AddrContentView>;

    struct ErrorView
    {
        nlmsghdr *header;
        nlmsgerr *error;
        std::optional<MessageContentView> original_content;
        std::vector<TLVView> attributes;
    };

    using NetlinkPacketView =
        std::variant<MessageView, LinkView, AddrView, ErrorView, DoneView>;

    class NetlinkPacketReader
    {
        NetlinkSocketReader socket_reader_;
        int socket_fd_;

        NetlinkPacketReader(int socket_fd);

    public:
        std::optional<std::function<void(NetlinkPacketReader &)>> packet_handler = std::nullopt;

        static std::unique_ptr<NetlinkPacketReader> Create(unsigned int subscribed_groups);

        std::optional<NetlinkPacketView> Next();
    };

    struct DeviceData
    {
        std::optional<std::array<uint8_t, 6>> mac_address;
        std::optional<std::array<uint8_t, 4>> ip_address;
        std::optional<std::string> interface_name;
    };

    void LoadDeviceDump(std::map<unsigned int, DeviceData> &device_registry, NetlinkPacketReader &reader)
    {
    }

    class NetlinkDeviceStateMonitor
    {
        std::unique_ptr<NetlinkPacketReader> device_change_monitor_;
        std::unique_ptr<NetlinkPacketReader> device_state_socket_;
        std::unique_ptr<NetlinkPacketReader> network_state_socket_;

    public:
        NetlinkDeviceStateMonitor() : device_change_monitor_(NetlinkPacketReader::Create(RTMGRP_LINK | RTMGRP_IPV4_IFADDR)),
                                      device_state_socket_(NetlinkPacketReader::Create(0)),
                                      network_state_socket_(NetlinkPacketReader::Create(0))
        {
        }
    };

    // class NetlinkDeviceRepository
    // {
    //     std::map<unsigned int, DeviceData> device_registry_;
    //     int socket_fd_;
    //     NetlinkPacketReader reader_;
    //     unsigned int source_pid_;
    //     unsigned int sequence_number_;

    // public:
    //     NetlinkDeviceRepository();

    //     enum class DispatchResult : uint8_t
    //     {
    //         OK = 0,
    //         INVALID_FD,
    //         SEND_UNSUCCESSFUL
    //     };

    //     DispatchResult DispatchDeviceDumpPackets();
    //     DispatchResult DispatchAddressDumpPackets();

    //     void ParseDeviceDump(unsigned int sequence_number, Scheduler &scheduler);

    //     // void UpdateRepositoryState();
    // };

    // void ReloadDeviceDumpTask(NetlinkDeviceRepository &repository, Scheduler &scheduler);

} // namespace ndisc

#endif