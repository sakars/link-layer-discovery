
#include <cstring>
#include <iostream>
#include <iomanip>

#include "data_transport.hh"

void getData(ndisc::data::DataTransportClient &client)
{
    auto map = client.GetData();
    for (auto &[key, value] : map)
    {
        std::string chassis;
        chassis.resize(value.chassis.size());
        std::memcpy(chassis.data(), value.chassis.data(), value.chassis.size());
        std::cout << "Chassis: " << chassis << "\t";
        std::cout << "Port: ";
        std::cout << std::hex << std::setw(2);
        for (auto &port_char : value.port)
        {
            std::cout << (uint16_t)port_char << " ";
        }
        std::cout << std::dec;
        std::cout << "\tIp: ";
        if (value.ipv4_address.has_value())
        {
            for (auto &ipv4_byte : *value.ipv4_address)
            {
                std::cout << std::to_integer<int>(ipv4_byte) << ".";
            }
        }
        else
        {
            std::cout << std::left << std::setw(16) << "None";
        }
        std::cout << "\tIPv6: " << std::hex << std::setfill('0');
        if (value.ipv6_address.has_value())
        {
            for (auto &ipv6_byte : *value.ipv6_address)
            {
                std::cout << std::setw(2) << std::to_integer<int>(ipv6_byte) << " ";
            }
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