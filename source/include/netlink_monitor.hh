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

        static void LoadBatch(int socket_fd, std::vector<uint8_t> &data_buffer);

        static std::optional<std::span<uint8_t>> TryLoadFromSpan(std::span<uint8_t> &remaining_data);

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

        void EndTransmission();

        void TryTransmit();

        void TriggerTransmission();

        void TimerExpired();

        void NewNeighbour();

        void LocalChangeDetected();

        void Tick();
    };

} // namespace ndisc

#endif