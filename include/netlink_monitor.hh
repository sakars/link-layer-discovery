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
#include <type_traits>

namespace ndisc
{
    constexpr unsigned int KERNEL_PID = 0;

    class NetlinkSocket final : public EventHandler
    {
    private:
        int socket_fd_;
        std::vector<uint8_t> data_buffer_;
        std::span<uint8_t> remaining_data_;
        int sequence_number_ = 1;
        std::function<void(std::span<uint8_t>)> callback_;

        NetlinkSocket(int socket_fd, std::function<void(std::span<uint8_t>)> callback) : socket_fd_(socket_fd), callback_(std::move(callback))
        {
        }
        static inline void loadBatch(int socket_fd, std::vector<uint8_t> &data_buffer)
        {
            if (socket_fd < 0)
            {
                data_buffer.clear();
                std::cerr << "Unable to load batch: socket not valid\n";
                return;
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

            const long peek_data_length = recvmsg(socket_fd, &header_buffer, MSG_PEEK | MSG_TRUNC);
            if (peek_data_length <= 0)
            {
                data_buffer.clear();
                return;
            }

            data_buffer = std::vector<uint8_t>(peek_data_length, 0);

            buffer_data.iov_base = data_buffer.data();
            buffer_data.iov_len = peek_data_length;

            const long data_length = recvmsg(socket_fd, &header_buffer, 0);
            if (data_length != peek_data_length)
            {
                data_buffer.clear();
                return;
            }
            if (source_address.nl_pid != KERNEL_PID)
            {
                data_buffer.clear();
                return;
            }
            std::cout << "Loaded " << data_length << " bytes.\n";
            data_buffer.resize(data_length);
        }

        static inline std::optional<std::span<uint8_t>> tryLoadFromSpan(std::span<uint8_t> &remaining_data)
        {
            size_t buffer_size = remaining_data.size();
            const nlmsghdr *header = reinterpret_cast<const nlmsghdr *>(remaining_data.data());
            if (NLMSG_OK(header, buffer_size))
            {
                size_t next_message_size = NLMSG_ALIGN(header->nlmsg_len);
                const std::span<uint8_t> message_span = remaining_data.subspan(0, next_message_size);
                remaining_data = remaining_data.subspan(next_message_size);
                return message_span;
            }
            return std::nullopt;
        }

    public:
        static std::unique_ptr<NetlinkSocket> Create(std::function<void(std::span<uint8_t>)> callback, uint32_t multicast_groups)
        {
            int socket_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
            if (socket_fd < 0)
            {
                // TODO: add error report
                std::cerr << "Failed to make socket\n";
                return nullptr;
            }
            int enable = 1;
            setsockopt(socket_fd, SOL_NETLINK, NETLINK_EXT_ACK, static_cast<const void *>(&enable), sizeof(int));
            // unsigned int pid = getpid();
            sockaddr_nl address{};
            address.nl_family = AF_NETLINK;
            address.nl_pad = 0;
            address.nl_pid = 0;
            address.nl_groups = multicast_groups;
            int bind_result = bind(socket_fd, reinterpret_cast<const sockaddr *>(&address), sizeof(sockaddr_nl));
            if (bind_result < 0)
            {
                close(socket_fd);
                std::cerr << "Failed to bind: errno " << errno << "\n";
                return nullptr;
            }
            return std::make_unique<NetlinkSocket>(NetlinkSocket(socket_fd, std::move(callback)));
        }

        int GetSocket() const override
        {
            return socket_fd_;
        }

        uint32_t GetEvents() const override
        {
            return EPOLLIN;
        }

        void Call() override
        {
            std::cout << "NetlinkSocket Call fd: " << socket_fd_ << "\n";
            // empty current span
            for (std::optional<std::span<uint8_t>> packet_from_buffer = tryLoadFromSpan(remaining_data_);
                 packet_from_buffer.has_value();
                 packet_from_buffer = tryLoadFromSpan(remaining_data_))
            {
                std::cout << "NetlinkSocket Callback\n";
                callback_(packet_from_buffer.value());
            }
            std::cout << "Loading batch...";
            loadBatch(socket_fd_, data_buffer_);
            remaining_data_ = std::span<uint8_t>(data_buffer_.begin(), data_buffer_.end());
            for (std::optional<std::span<uint8_t>> packet_from_buffer = tryLoadFromSpan(remaining_data_);
                 packet_from_buffer.has_value();
                 packet_from_buffer = tryLoadFromSpan(remaining_data_))
            {
                std::cout << "NetlinkSocket Callback\n";
                callback_(packet_from_buffer.value());
            }
        }

        int GetSequenceNumber() const
        {
            return sequence_number_;
        }
        // std::optional<std::span<uint8_t>> Next()
        // {
        //     std::optional<std::span<uint8_t>> packet_from_remaining = tryLoadFromSpan(remaining_data_);
        //     if (packet_from_remaining.has_value())
        //     {
        //         return packet_from_remaining;
        //     }
        //     loadBatch(socket_fd_, data_buffer_);
        //     remaining_data_ = std::span<uint8_t>(data_buffer_.begin(), data_buffer_.end());
        //     return tryLoadFromSpan(remaining_data_);
        // }

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

        long SendGetLinkDumpMessage()
        {
            if (socket_fd_ < 0)
            {
                return -2;
            }
            sequence_number_++;
            alignas(nlmsghdr) std::array<uint8_t, NLMSG_LENGTH(sizeof(struct ifinfomsg))> buffer{};
            nlmsghdr *netlink_header = reinterpret_cast<nlmsghdr *>(buffer.data());
            netlink_header->nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
            netlink_header->nlmsg_pid = getpid();
            netlink_header->nlmsg_seq = sequence_number_;
            netlink_header->nlmsg_flags = NLM_F_ACK | NLM_F_DUMP | NLM_F_REQUEST;
            netlink_header->nlmsg_type = RTM_GETLINK;
            ifinfomsg *interface_info = reinterpret_cast<ifinfomsg *>(NLMSG_DATA(netlink_header));
            interface_info->ifi_family = AF_UNSPEC;
            interface_info->ifi_type = 0;
            interface_info->ifi_change = 0;
            interface_info->ifi_flags = 0;
            interface_info->ifi_index = 0;

            const long bytes_sent = send(socket_fd_, buffer.data(), netlink_header->nlmsg_len, 0);
            if (bytes_sent < netlink_header->nlmsg_len)
            {
                return -3;
            }
            return bytes_sent;
        }

        long SendGetAddrMessage()
        {
            if (socket_fd_ < 0)
            {
                return -2;
            }
            sequence_number_++;
            alignas(nlmsghdr) std::array<uint8_t, NLMSG_LENGTH(sizeof(struct ifaddrmsg))> buffer{};
            nlmsghdr *netlink_header = reinterpret_cast<nlmsghdr *>(buffer.data());
            netlink_header->nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
            netlink_header->nlmsg_pid = getpid();
            netlink_header->nlmsg_seq = sequence_number_;
            netlink_header->nlmsg_flags = NLM_F_ACK | NLM_F_REQUEST | NLM_F_DUMP;
            netlink_header->nlmsg_type = RTM_GETADDR;
            ifaddrmsg *address_message = reinterpret_cast<ifaddrmsg *>(NLMSG_DATA(netlink_header));
            address_message->ifa_family = AF_INET;
            address_message->ifa_prefixlen = 0;
            address_message->ifa_scope = IFA_UNSPEC;
            address_message->ifa_flags = 0;
            address_message->ifa_index = 0;

            const long bytes_sent = send(socket_fd_, buffer.data(), netlink_header->nlmsg_len, 0);
            if (bytes_sent < netlink_header->nlmsg_len)
            {
                return -3;
            }
            return bytes_sent;
        }
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

    struct ErrorView
    {
        nlmsghdr *header;
        nlmsgerr *error;
        std::optional<MessageContentView> original_content;
        std::vector<TLVView> attributes;
    };

    using NetlinkPacketView =
        std::variant<MessageView, LinkView, AddrView, ErrorView, DoneView>;

    // class NetlinkPacketReader
    // {
    //     std::unique_ptr<NetlinkSocket> socket_;
    //     int socket_fd_;

    //     std::function<void(NetlinkPacketView)> callback_;

    // public:
    //     NetlinkPacketReader::NetlinkPacketReader(int socket_fd, std::function<void(NetlinkPacketView)> callback) : socket_fd_(socket_fd), socket_(nullptr), callback_(std::move(callback))
    //     {
    //         std::unique_ptr<NetlinkSocket> socket = NetlinkSocket::Create();
    //     }

    //     static void SocketHandler(std::span<)
    // };

    NetlinkPacketView packetViewParser(std::span<uint8_t> packet);

    struct DeviceData
    {
        std::optional<std::array<uint8_t, 6>> mac_address = std::nullopt;
        std::optional<std::array<uint8_t, 4>> ip_address = std::nullopt;
        std::optional<std::string> interface_name = std::nullopt;
    };

    // void LoadDeviceDump(std::map<unsigned int, DeviceData> &device_registry, NetlinkPacketReader &reader)
    // {
    // }

    // class NetlinkDeviceStateMonitor
    // {
    //     std::unique_ptr<NetlinkPacketReader> device_change_monitor_;
    //     std::unique_ptr<NetlinkPacketReader> device_state_socket_;
    //     std::unique_ptr<NetlinkPacketReader> network_state_socket_;

    // public:
    //     NetlinkDeviceStateMonitor() : device_change_monitor_(NetlinkPacketReader::Create(RTMGRP_LINK | RTMGRP_IPV4_IFADDR)),
    //                                   device_state_socket_(NetlinkPacketReader::Create(0)),
    //                                   network_state_socket_(NetlinkPacketReader::Create(0))
    //     {
    //     }
    // };

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