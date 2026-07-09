
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
            std::string chassis = std::string{value.chassis.begin(), value.chassis.end()};
            std::cout << "Chassis length: " << value.chassis.size() << "\t";
            std::cout << "Chassis: " << chassis << "\t";
            std::cout << "Port: ";
            std::cout << std::hex << std::setw(2);
            for (auto &x : value.port)
            {
                std::cout << (uint16_t)x << " ";
            }
            std::cout << std::dec;
            std::cout << "\tIp: ";
            if (value.ip_address.has_value())
            {
                for (auto &x : *value.ip_address)
                {
                    std::cout << x << " ";
                }
            }
            else
            {
                std::cout << "None";
            }
            std::cout << "\n";
        }
        std::cout << std::endl;
        sleep(1);
    }
}