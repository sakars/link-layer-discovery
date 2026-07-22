
#include <cstring>
#include <iostream>
#include <iomanip>

#include "data_transport.hh"

int main()
{
    std::expected<ndisc::data::DataTransportClient, int> client = ndisc::data::DataTransportClient::Create();
    if (!client.has_value())
    {
        std::cerr << "Failed to create DataTransportClient, errno " << client.error();
    }
    while (true)
    {
        auto map = client->GetData();
        for (auto &[key, value] : map)
        {
            std::string chassis;
            chassis.resize(value.chassis.size());
            std::memcpy(chassis.data(), value.chassis.data(), value.chassis.size());
            std::cout << "Chassis: " << chassis << "\t";
            std::cout << "Port: ";
            std::cout << std::hex << std::setw(2);
            for (auto &x : value.port)
            {
                std::cout << (uint16_t)x << " ";
            }
            std::cout << std::dec;
            std::cout << "\tIp: ";
            if (value.ipv4_address.has_value())
            {
                for (auto &x : *value.ipv4_address)
                {
                    std::cout << std::to_integer<int>(x) << ".";
                }
            }
            else
            {
                std::cout << std::left << std::setw(16) << "None";
            }
            std::cout << "\tIPv6: " << std::hex << std::setfill('0');
            if (value.ipv6_address.has_value())
            {
                for (auto &x : *value.ipv6_address)
                {
                    std::cout << std::setw(2) << std::to_integer<int>(x) << " ";
                }
            }
            else
            {
                std::cout << "None";
            }
            std::cout << std::dec << std::setfill(' ');

            std::cout << "\n";
        }
        std::cout << std::endl;
        sleep(1);
    }
}