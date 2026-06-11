#ifndef NETLINK_HH
#define NETLINK_HH

#include <array>
#include <cstdint>
#include <linux/rtnetlink.h>
#include <memory>
#include <string>
#include <variant>
#include <optional>
#include <vector>

namespace ndisc
{

    struct InterfaceDumpEntry
    {
        size_t interface_index;
        std::optional<std::string> interface_name;
        std::optional<std::array<uint8_t, 6>> mac_address;
        std::optional<std::array<uint8_t, 4>> ipv4_address;
    };
    class NetlinkSocket
    {
    private:
        int socket_fd_{};
        uint32_t sequence_number_ = 0;
        std::unique_ptr<sockaddr_nl> address_ = nullptr;

    public:
        NetlinkSocket();
        ~NetlinkSocket();
        NetlinkSocket(const NetlinkSocket &) = delete;
        NetlinkSocket(NetlinkSocket &&) noexcept;
        NetlinkSocket &operator=(const NetlinkSocket &) = delete;
        NetlinkSocket &operator=(NetlinkSocket &&) noexcept;

        enum class NetlinkError : uint8_t
        {
            SOCKET_CLOSED
        };

        std::variant<std::array<uint8_t, 4>, NetlinkSocket::NetlinkError, std::monostate> GetIpAddressOfDevice(const std::string &device);

        bool IsSocketOk() const;
        std::vector<InterfaceDumpEntry> GetDevices();
    };
} // namespace ndisc

#endif