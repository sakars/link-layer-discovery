#ifndef NETLINK_MONITOR_HH
#define NETLINK_MONITOR_HH

#include <arpa/inet.h>
#include <array>
#include <cstdint>
#include <functional>
#include <linux/rtnetlink.h>
#include <optional>
#include <span>
#include <sys/epoll.h>
#include <variant>
#include <vector>

#include "event_handlers.hh"
#include "lldp.hh"
#include "lldp_packet.hh"

namespace ndisc
{

    class NetlinkSocket final : public EventHandler
    {
    private:
        OwnedFileDescriptor socket_fd_;
        std::vector<std::byte> data_buffer_;
        std::span<std::byte> remaining_data_;
        int sequence_number_ = 1;
        std::function<void(std::span<std::byte>)> callback_;

        NetlinkSocket(OwnedFileDescriptor &&socket_fd, std::function<void(std::span<std::byte>)> callback) : socket_fd_(std::move(socket_fd)), callback_(std::move(callback))
        {
        }

        static void LoadBatch(int socket_fd, std::vector<std::byte> &data_buffer);

        static std::optional<std::span<std::byte>> TryLoadFromSpan(std::span<std::byte> &remaining_data);

    public:
        static std::expected<std::unique_ptr<NetlinkSocket>, int> Create(std::function<void(std::span<std::byte>)> callback, uint32_t multicast_groups);

        int GetSocket() const override
        {
            return *socket_fd_;
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
        std::span<std::byte> value;
    };

    struct MessageContentView
    {
        std::span<std::byte> content;
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

    NetlinkPacketView packetViewParser(std::span<std::byte> packet);

    constexpr uint16_t MAX_TRANSMIT_CREDITS = 5;
    constexpr uint16_t FAST_TRANSMIT_AMOUNT = 4;
    constexpr uint16_t TARGET_TTL = 30;
    constexpr uint16_t PACKET_HOLD_AMOUNT = 5;
    constexpr uint16_t MESSAGE_TRANSMIT_INTERVAL = TARGET_TTL / PACKET_HOLD_AMOUNT;
    constexpr uint16_t MESSAGE_FAST_INTERVAL = 1;

    class LldpSender
    {
        OwnedFileDescriptor socket_fd_;

        LldpSender(OwnedFileDescriptor &&socket) : socket_fd_(std::move(socket)) {}

    public:
        static std::optional<LldpSender> Create();

        void SendLldp(unsigned int interface, const std::array<std::byte, ETH_ALEN> &mac, const std::optional<std::array<std::byte, sizeof(in_addr)>> &ip_address, uint16_t ttl) const;
    };

    struct DeviceData
    {
        std::optional<std::array<std::byte, ETH_ALEN>> mac_address = std::nullopt;
        std::optional<std::array<std::byte, sizeof(in_addr)>> ip_address = std::nullopt;
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