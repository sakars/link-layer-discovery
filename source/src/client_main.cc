
#include <cstring>
#include <iomanip>
#include <iostream>

#include "data_transport.hh"

void getData(ndisc::data::DataTransportClient &client)
{
    auto map = client.GetData();
    std::cout << "Chassis\tPort type\tPort\tIp\tIpv6\n";
    for (auto &[key, value] : map)
    {
        std::string chassis;
        chassis.resize(value.chassis.size());
        std::memcpy(chassis.data(), value.chassis.data(), value.chassis.size());
        std::cout << chassis << "\t";
        if (!value.port.empty())
        {
            std::cout << std::hex << std::setfill('0');
            if (value.port[0] == std::byte{0x03} && value.port.size() == 1 + ETH_ALEN)
            {
                std::cout << "MAC\t"
                          << std::setw(2) << std::to_integer<int>(value.port[1]) << ":"
                          << std::setw(2) << std::to_integer<int>(value.port[2]) << ":"
                          << std::setw(2) << std::to_integer<int>(value.port[3]) << ":"
                          << std::setw(2) << std::to_integer<int>(value.port[4]) << ":"
                          << std::setw(2) << std::to_integer<int>(value.port[5]) << ":"
                          << std::setw(2) << std::to_integer<int>(value.port[6]);
            }
            else if (value.port[0] == std::byte{0x04} && value.port.size() > 1)
            {
                if (value.port[1] == std::byte{lldp::MANAGEMENT_TLV_ADDRESS_SUBTYPE_MAC} && value.port.size() == 1 + 1 + ETH_ALEN)
                {
                    std::cout << "NET(MAC)\t"
                              << std::setw(2) << std::to_integer<int>(value.port[2]) << ":"
                              << std::setw(2) << std::to_integer<int>(value.port[3]) << ":"
                              << std::setw(2) << std::to_integer<int>(value.port[4]) << ":"
                              << std::setw(2) << std::to_integer<int>(value.port[5]) << ":"
                              << std::setw(2) << std::to_integer<int>(value.port[6]) << ":"
                              << std::setw(2) << std::to_integer<int>(value.port[7]);
                }
                else if (value.port[1] == std::byte{lldp::MANAGEMENT_TLV_ADDRESS_SUBTYPE_IPV4} && value.port.size() == 1 + 1 + sizeof(in_addr))
                {
                    std::string address;
                    address.resize(INET_ADDRSTRLEN);
                    std::cout << "NET(IPv4)\t"
                              << inet_ntop(AF_INET, std::next(value.port.begin(), 2).base(), address.data(), address.size()) << "\t";
                }
                else if (value.port[1] == std::byte{lldp::MANAGEMENT_TLV_ADDRESS_SUBTYPE_IPV6} && value.port.size() == 1 + 1 + sizeof(in6_addr))
                {
                    std::string address;
                    address.resize(INET6_ADDRSTRLEN);
                    std::cout << "NET(IPv6)\t"
                              << inet_ntop(AF_INET6, std::next(value.port.begin(), 2).base(), address.data(), address.size()) << "\t";
                }
                else
                {
                    std::cout << "NET(Unknown(" << std::to_integer<int>(value.port[1]) << "))\t";

                    for (const std::byte &port_byte : value.port)
                    {
                        std::cout << std::setw(2) << std::to_integer<int>(port_byte) << " ";
                    }
                }
            }
            else if (value.port[0] == std::byte{0x07})
            {
                std::cout << "Local\t";

                for (size_t i = 1; i < value.port.size(); i++)
                {
                    std::cout << std::to_integer<char>(value.port[i]) << " ";
                }
            }
            else
            {
                std::cout << "Unknown(" << std::to_integer<int>(value.port[0]) << ")\t";

                for (const std::byte &port_byte : value.port)
                {
                    std::cout << std::setw(2) << std::to_integer<int>(port_byte) << " ";
                }
            }
            std::cout << std::dec << std::setfill(' ') << "\t";
        }
        else
        {
            std::cout << "None\tNone\t";
        }
        if (value.ipv4_address.has_value())
        {
            std::string address;
            address.resize(INET_ADDRSTRLEN);
            std::cout << inet_ntop(AF_INET, value.ipv6_address->data(), address.data(), address.size()) << "\t";
        }
        else
        {
            std::cout << "None\t";
        }
        std::cout << std::hex << std::setfill('0');
        if (value.ipv6_address.has_value())
        {
            std::string address;
            address.resize(INET6_ADDRSTRLEN);
            std::cout << inet_ntop(AF_INET6, value.ipv6_address->data(), address.data(), address.size()) << "\t";
        }
        else
        {
            std::cout << "None";
        }
        std::cout << std::dec << std::setfill(' ');

        std::cout << "\n";
    }
    std::cout << "\n";
}

int main(int argc, char **argv)
{
    std::vector<std::string_view> args;
    args.resize(argc);
    for (int i = 0; i < argc; i++)
    {
        args[i] = argv[i];
    }
    bool run_continuous = false;
    int continuous_timeout = 5;
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
            getData(*client);
            sleep(continuous_timeout);
        }
    }
    else
    {
        getData(*client);
    }
}