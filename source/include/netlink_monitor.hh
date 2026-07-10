#ifndef NETLINK_MONITOR_HH
#define NETLINK_MONITOR_HH

#include "event_handlers.hh"
#include "lldp.hh"
#include "lldp_packet.hh"

#include <arpa/inet.h>
#include <cstdint>
#include <expected>
#include <fstream>
#include <functional>
#include <iostream>
#include <linux/if_packet.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <map>
#include <memory>
#include <net/ethernet.h>
#include <net/if_arp.h>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

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

        static void LoadBatch(int socket_fd, std::vector<uint8_t> &data_buffer)
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
            data_buffer.resize(data_length);
        }

        static std::optional<std::span<uint8_t>> TryLoadFromSpan(std::span<uint8_t> &remaining_data)
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
        static std::expected<std::unique_ptr<NetlinkSocket>, int> Create(std::function<void(std::span<uint8_t>)> callback, uint32_t multicast_groups);

        int GetSocket() const override
        {
            return socket_fd_;
        }

        uint32_t GetEvents() const override
        {
            return EPOLLIN;
        }

        void Call() override;

        int GetSequenceNumber() const
        {
            return sequence_number_;
        }

        bool IsReadable() const;

        long SendGetLinkDumpMessage();

        long SendGetAddrMessage();
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
        nlmsghdr *header = nullptr;
        MessageContentView content;
    };

    struct LinkContentView
    {
        ifinfomsg *interface_info;
        std::vector<TLVView> attributes;
    };

    struct LinkView
    {
        nlmsghdr *header = nullptr;
        LinkContentView content;
    };

    struct AddrContentView
    {
        ifaddrmsg *address_info;
        std::vector<TLVView> attributes;
    };

    struct AddrView
    {
        nlmsghdr *header = nullptr;
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

    NetlinkPacketView packetViewParser(std::span<uint8_t> packet);

    constexpr uint16_t MAX_TRANSMIT_CREDITS = 5;
    constexpr uint16_t FAST_TRANSMIT_AMOUNT = 4;
    constexpr uint16_t TARGET_TTL = 30;
    constexpr uint16_t PACKET_HOLD_AMOUNT = 5;
    constexpr uint16_t MESSAGE_TRANSMIT_INTERVAL = TARGET_TTL / PACKET_HOLD_AMOUNT;
    constexpr uint16_t MESSAGE_FAST_INTERVAL = 1;

    std::string getMachineId();

    class LldpSender
    {
        int socket_fd_;

        LldpSender(int socket) : socket_fd_(socket) {}

    public:
        ~LldpSender()
        {
            if (socket_fd_ >= 0)
            {
                close(socket_fd_);
            }
        }
        LldpSender(const LldpSender &) = delete;
        LldpSender(LldpSender &&other) noexcept : socket_fd_(other.socket_fd_)
        {
            other.socket_fd_ = -1;
        }
        LldpSender &operator=(const LldpSender &) = delete;
        LldpSender &operator=(LldpSender &&other) noexcept
        {
            if (socket_fd_ >= 0)
            {
                close(socket_fd_);
            }
            socket_fd_ = other.socket_fd_;
            other.socket_fd_ = -1;
            return *this;
        }

        static std::optional<LldpSender> Create();

        void SendLldp(unsigned int interface, const std::array<uint8_t, ETH_ALEN> &mac, const std::optional<std::array<uint8_t, sizeof(in_addr)>> &ip_address, uint16_t ttl) const;
    };

    struct DeviceData
    {
        std::optional<std::array<uint8_t, ETH_ALEN>> mac_address = std::nullopt;
        std::optional<std::array<uint8_t, sizeof(in_addr)>> ip_address = std::nullopt;
        std::optional<std::string> interface_name = std::nullopt;
        std::optional<LldpSender> lldp_sender = std::nullopt;
        unsigned int if_index;
        uint16_t transmit_timer = 0;
        uint16_t transmit_credits = 0;
        uint16_t fast_forward_counter = 0;
        bool trigger_ready = false;

        void EndTransmission()
        {
            if (lldp_sender.has_value() && mac_address.has_value())
            {
                lldp_sender->SendLldp(if_index, *mac_address, ip_address, 0);
            }
        }

        void TryTransmit()
        {
            if (trigger_ready && transmit_credits > 0 && mac_address.has_value())
            {
                trigger_ready = false;
                transmit_credits--;
                if (lldp_sender.has_value())
                {
                    std::cout << "Transmitting lldp via device " << if_index << "\n";
                    lldp_sender->SendLldp(if_index, *mac_address, ip_address, TARGET_TTL);
                }
            }
        }

        void TriggerTransmission()
        {
            if (fast_forward_counter > 0)
            {
                transmit_timer = MESSAGE_FAST_INTERVAL;
            }
            else
            {
                transmit_timer = MESSAGE_TRANSMIT_INTERVAL;
            }
            trigger_ready = true;
            TryTransmit();
        }
        void TimerExpired()
        {
            if (fast_forward_counter > 0)
            {
                fast_forward_counter--;
            }
            TriggerTransmission();
        }
        void NewNeighbour()
        {
            if (fast_forward_counter == 0)
            {
                fast_forward_counter = FAST_TRANSMIT_AMOUNT;
            }
            TimerExpired();
        }

        void LocalChangeDetected()
        {
            TriggerTransmission();
        }

        void Tick()
        {
            if (transmit_credits < MAX_TRANSMIT_CREDITS)
            {
                transmit_credits += 1;
            }
            if (transmit_timer > 0)
            {
                transmit_timer--;
            }
            if (transmit_timer == 0)
            {
                TimerExpired();
            }
            TryTransmit();
        }
    };

} // namespace ndisc

#endif