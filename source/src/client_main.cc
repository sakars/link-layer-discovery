
#include <cstring>
#include <iomanip>
#include <iostream>
#include <ranges>

#include "client.hh"

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

void dumpNeighbourData(client::ClientReceiverSocket &client)
{
    auto map = client.GetData();
    std::cout << "Chassis\tPort type\tPort\tIp\tIpv6\n";
    for (auto &[key, value] : map)
    {
        std::string chassis;
        chassis.resize(value.chassis.size());
        std::ranges::copy(value.chassis, std::as_writable_bytes(std::span(chassis)).begin());
        std::cout << chassis << "\t";
        logPortType(value.port);
        std::cout << "\t";
        lldp::logPort(value.port);
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

    std::expected<client::ClientReceiverSocket, int> client = client::ClientReceiverSocket::Create();
    if (!client.has_value())
    {
        std::cerr << "Failed to create ClientReceiver, errno " << client.error();
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