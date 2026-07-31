#ifndef NETLINK_MONITOR_HH
#define NETLINK_MONITOR_HH

#include <arpa/inet.h>
#include <array>
#include <cstdint>
#include <expected>
#include <functional>
#include <linux/rtnetlink.h>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <sys/epoll.h>
#include <variant>
#include <vector>

#include "event_handlers.hh"
#include "lldp.hh"
#include "lldp_packet.hh"

namespace netlink
{

    std::string getMachineId();

    struct TLVView
    {
        rtattr attribute_header;
        std::span<std::byte> value;
    };

    template <typename T>
    struct NetlinkMessage
    {
        nlmsghdr header{};
        T content;
    };

    struct MessageContentView
    {
        std::span<std::byte> content;
    };

    using MessageView = NetlinkMessage<MessageContentView>;

    struct LinkContentView
    {
        ifinfomsg interface_info;
        std::vector<TLVView> attributes;
    };

    using LinkView = NetlinkMessage<LinkContentView>;

    struct AddrContentView
    {
        ifaddrmsg address_info;
        std::vector<TLVView> attributes;
    };

    using AddrView = NetlinkMessage<AddrContentView>;

    using DoneView = NetlinkMessage<int>;

    struct ErrorContentView
    {
        nlmsgerr message_error{};
        std::optional<MessageContentView> original_content;
        std::vector<TLVView> attributes;
    };
    using ErrorView = NetlinkMessage<ErrorContentView>;

    using NetlinkPacketView =
        std::variant<MessageView, LinkView, AddrView, ErrorView, DoneView>;

    NetlinkPacketView packetViewParser(std::span<std::byte> packet);

    class NetlinkSocket final : public ndisc::EventHandler
    {
    public:
        using Callback = std::function<void(NetlinkPacketView)>;

    private:
        ndisc::OwnedFileDescriptor socket_fd_;
        std::vector<std::byte> data_buffer_;
        std::span<std::byte> remaining_data_;
        int sequence_number_ = 1;
        std::optional<Callback> callback_;

        NetlinkSocket(ndisc::OwnedFileDescriptor &&socket_fd) : socket_fd_(std::move(socket_fd))
        {
        }

        static void LoadBatch(int socket_fd, std::vector<std::byte> &data_buffer);

        static std::optional<std::span<std::byte>> TryLoadFromSpan(std::span<std::byte> &remaining_data);

    public:
        static std::expected<std::unique_ptr<NetlinkSocket>, int> Create(uint32_t multicast_groups);

        void SetCallback(Callback callback)
        {
            callback_ = std::move(callback);
        }

        void ClearCallback()
        {
            callback_ = std::nullopt;
        }

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

        std::expected<int, int> SendGetLinkDumpMessage();

        std::expected<int, int> SendGetAddrMessage();
    };

    struct DeviceData
    {
        std::optional<std::array<std::byte, ETH_ALEN>> mac_address = std::nullopt;
        std::optional<std::array<std::byte, sizeof(in_addr)>> ipv4_address = std::nullopt;
        std::optional<std::array<std::byte, sizeof(in6_addr)>> ipv6_address = std::nullopt;
        std::optional<std::string> interface_name = std::nullopt;
        bool device_operational = false;
        unsigned int if_index{};
    };

} // namespace netlink

#endif