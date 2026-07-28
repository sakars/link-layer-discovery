
#include <cstring>
#include <iomanip>
#include <iostream>
#include <ranges>

#include "data_transport.hh"

static void logPortType(const std::vector<std::byte> &port)
{
    if (port.empty())
    {
        std::cout << "Missing...";
        return;
    }
    if (port[0] == lldp::PORT_TLV_SUBTYPE_MAC)
    {
        std::cout << "MAC";
    }
    else if (port[0] == lldp::PORT_TLV_SUBTYPE_LOCAL)
    {
        std::cout << "LOCAL";
    }
    else if (port[0] == lldp::PORT_TLV_SUBTYPE_NET)
    {
        std::cout << "NET(";
        if (port.size() < 2)
        {
            std::cout << "INVALID";
        }
        else
        {
            if (port[1] == lldp::MANAGEMENT_TLV_ADDRESS_SUBTYPE_MAC)
            {
                std::cout << "MAC";
            }
            else if (port[1] == lldp::MANAGEMENT_TLV_ADDRESS_SUBTYPE_IPV4)
            {
                std::cout << "IPv4";
            }
            else if (port[1] == lldp::MANAGEMENT_TLV_ADDRESS_SUBTYPE_IPV6)
            {
                std::cout << "IPv6";
            }
            else
            {
                std::cout << "UNKNOWN(" << std::to_integer<int>(port[1]) << ")";
            }
        }
        std::cout << ")";
    }
    else
    {
        std::cout << "UNKNOWN";
    }
}

static void logPort(const std::vector<std::byte> &port)
{
    if (port.empty())
    {
        std::cout << "Missing";
        return;
    }
    if (port[0] == lldp::PORT_TLV_SUBTYPE_MAC && port.size() == 1 + ETH_ALEN)
    {
        lldp::logMac(std::span<const std::byte, ETH_ALEN>{port.begin() + 1, ETH_ALEN});
    }
    else if (port[0] == lldp::PORT_TLV_SUBTYPE_LOCAL)
    {
        std::string_view local_view = std::string_view{reinterpret_cast<const char *>((port.begin() + 1).base()), port.size() - 1};
        std::cout << local_view;
    }
    else if (port[0] == lldp::PORT_TLV_SUBTYPE_NET && port.size() > 1)
    {
        if (port[1] == lldp::MANAGEMENT_TLV_ADDRESS_SUBTYPE_MAC && port.size() == 1 + 1 + ETH_ALEN)
        {
            lldp::logMac(std::span<const std::byte, ETH_ALEN>{std::next(port.begin(), 2), ETH_ALEN});
        }
        else if (port[1] == lldp::MANAGEMENT_TLV_ADDRESS_SUBTYPE_IPV4 && port.size() == 1 + 1 + sizeof(in_addr))
        {
            std::string address;
            address.resize(INET_ADDRSTRLEN);
            std::cout << inet_ntop(AF_INET, std::next(port.begin(), 2).base(), address.data(), address.size());
        }
        else if (port[1] == lldp::MANAGEMENT_TLV_ADDRESS_SUBTYPE_IPV6 && port.size() == 1 + 1 + sizeof(in6_addr))
        {
            std::string address;
            address.resize(INET6_ADDRSTRLEN);
            std::cout << inet_ntop(AF_INET6, std::next(port.begin(), 2).base(), address.data(), address.size());
        }
        else
        {
            for (const std::byte &port_byte : port)
            {
                std::cout << std::setw(2) << std::to_integer<int>(port_byte) << " ";
            }
        }
    }
}

void dumpNeighbourData(ndisc::data::DataTransportClient &client)
{
    auto map = client.GetData();
    std::cout << "Chassis\tPort type\tPort\tIp\tIpv6\n";
    for (auto &[key, value] : map)
    {
        std::string chassis;
        chassis.resize(value.chassis.size());
        std::memcpy(chassis.data(), value.chassis.data(), value.chassis.size());
        std::cout << chassis << "\t";
        logPortType(value.port);
        std::cout << "\t";
        logPort(value.port);
        std::cout << "\t";
        lldp::logIpv4(value.ipv4_address);
        std::cout << "\t";
        lldp::logIpv6(value.ipv6_address);
        std::cout << "\n";
    }
    std::cout << "\n";
}

constexpr int DEFAULT_CONTINUOUS_MODE_TIMER = 5;

int main(int argc, const char *argv[])
{
    std::vector<std::string_view> args{argv, argv + argc};
    bool run_continuous = false;
    int continuous_timeout = DEFAULT_CONTINUOUS_MODE_TIMER;
    for (std::vector<std::string_view>::iterator arg = args.begin(); arg != args.end(); arg++)
    {
        if (*arg == "-c")
        {
            run_continuous = true;
        }
        if (*arg == "-t")
        {
            auto next = std::next(arg);
            if (next != args.end())
            {
                std::from_chars(next->begin(), next->end(), continuous_timeout);
                arg++;
            }
        }
    }

    std::expected<ndisc::data::DataTransportClient, int> client = ndisc::data::DataTransportClient::Create();
    if (!client.has_value())
    {
        std::cerr << "Failed to create DataTransportClient, errno " << client.error();
    }
    if (run_continuous)
    {
        while (true)
        {
            dumpNeighbourData(*client);
            sleep(continuous_timeout);
        }
    }
    else
    {
        dumpNeighbourData(*client);
    }
}