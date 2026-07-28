#ifndef LLDP_HH
#define LLDP_HH

#include <arpa/inet.h>
#include <array>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <linux/if_ether.h>
#include <netinet/in.h>
#include <optional>
#include <span>
#include <stdint.h>
#include <string>
#include <sys/socket.h>

namespace lldp
{
    constexpr uint8_t TYPE_BIT_OFFSET = 9;
    constexpr uint16_t TYPE_MASK = (1 << TYPE_BIT_OFFSET) - 1;
    constexpr uint8_t ETHERTYPE_SIZE = sizeof(uint16_t);

    enum TLV : uint8_t
    {
        END_OF_LLDPDU = 0,
        CHASSIS_ID = 1,
        PORT_ID = 2,
        TIME_TO_LIVE = 3,
        PORT_DESCRIPTION = 4,
        SYSTEM_NAME = 5,
        SYSTEM_DESCRIPTION = 6,
        SYSTEM_CAPABILITIES = 7,
        MANAGEMENT_ADDRESS = 8,
        ORGANIZATIONALLY_SPECIFIC = 127,
    };

    constexpr std::byte PORT_ID_MAC_TYPE{0x03};

    // https://www.iana.org/assignments/address-family-numbers/address-family-numbers.xhtml
    constexpr std::byte MANAGEMENT_TLV_ADDRESS_SUBTYPE_IPV4 = std::byte{1};
    constexpr std::byte MANAGEMENT_TLV_ADDRESS_SUBTYPE_IPV6 = std::byte{2};
    constexpr std::byte MANAGEMENT_TLV_ADDRESS_SUBTYPE_MAC = std::byte{6};

    constexpr std::byte MANAGEMENT_TLV_IF_SUBTYPE_IFINDEX = std::byte{2};

    constexpr std::byte PORT_TLV_SUBTYPE_MAC = std::byte{0x03};
    constexpr std::byte PORT_TLV_SUBTYPE_NET = std::byte{0x04};
    constexpr std::byte PORT_TLV_SUBTYPE_LOCAL = std::byte{0x07};

    void logMac(std::span<const std::byte, ETH_ALEN> mac)
    {
        std::ios_base::fmtflags flags(std::cout.flags());
        std::cout << std::hex << std::setfill('0');
        std::cout << std::setw(2) << std::to_integer<int>(mac[0]);
        for (int i = 1; i < ETH_ALEN; i++)
        {

            std::cout << ":" << std::setw(2) << std::to_integer<int>(mac[i]);
        }
        std::cout.flags(flags);
    }

    void logIpv4(const std::optional<std::array<std::byte, sizeof(in_addr)>> &ipv4)
    {
        if (ipv4.has_value())
        {
            std::string address;
            address.resize(INET_ADDRSTRLEN);
            std::cout << inet_ntop(AF_INET, ipv4->data(), address.data(), address.size());
        }
        else
        {
            std::cout << "None";
        }
    }

    void logIpv6(const std::optional<std::array<std::byte, sizeof(in6_addr)>> &ipv6)
    {
        if (ipv6.has_value())
        {
            std::string address;
            address.resize(INET6_ADDRSTRLEN);
            std::cout << inet_ntop(AF_INET6, ipv6->data(), address.data(), address.size());
        }
        else
        {
            std::cout << "None";
        }
    }
} // namespace lldp

#endif // LLDP_HH