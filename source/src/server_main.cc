
#include "data_transport.hh"
#include "device_repository.hh"
#include "lldp_monitor.hh"
#include "netlink_monitor.hh"

#include <chrono>
#include <execinfo.h>
#include <iomanip>
#include <iostream>
#include <queue>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/timerfd.h>
#include <thread>
#include <unistd.h>

using namespace std::chrono_literals;

struct LldpRepository
{
    std::map<unsigned int, ndisc::DeviceData> current_state;

    void CheckSocketForTxReady(unsigned int idx)
    {
        if (current_state.contains(idx))
        {
            ndisc::DeviceData &device_data = current_state[idx];
            if (!device_data.lldp_sender.has_value() && device_data.interface_name.has_value())
            {
                // device_data.lldp_sender = ndisc::LldpSender::Create();
                std::optional<ndisc::LldpSender> sender = ndisc::LldpSender::Create();
                if (sender.has_value())
                {
                    device_data.lldp_sender = std::move(*sender);
                }
            }
        }
    }

    void MarkChangedLldpStateMachine(unsigned int idx)
    {
        CheckSocketForTxReady(idx);
        if (current_state.contains(idx))
        {
            current_state[idx].LocalChangeDetected();
        }
    }

    void DeleteLldpStateMachine(unsigned int idx)
    {
        CheckSocketForTxReady(idx);
        if (current_state.contains(idx))
        {
            current_state[idx].EndTransmission();
        }
        current_state.erase(idx);
    }

    void CreateLldpStateMachine(unsigned int idx)
    {
        CheckSocketForTxReady(idx);
        if (current_state.contains(idx))
        {
            ndisc::DeviceData &device_data = current_state[idx];
            device_data.NewNeighbour();
        }
    }

    void UpdateState(std::map<unsigned int, ndisc::DeviceData> &new_state)
    {
        for (auto &[index, new_device_state] : new_state)
        {
            if (current_state.contains(index))
            {
                bool any_changed = false;
                ndisc::DeviceData &device_state = current_state.at(index);
                if (device_state.interface_name != new_device_state.interface_name)
                {
                    any_changed = true;
                    device_state.interface_name = new_device_state.interface_name;
                }
                if (device_state.ip_address != new_device_state.ip_address)
                {
                    any_changed = true;
                    device_state.ip_address = new_device_state.ip_address;
                }
                if (any_changed)
                {
                    MarkChangedLldpStateMachine(index);
                }
            }
            else
            {
                current_state[index] = std::move(new_device_state);
                CreateLldpStateMachine(index);
            }
        }
        std::vector<unsigned int> deletables{};
        for (auto &[index, current_device_state] : current_state)
        {
            if (!new_state.contains(index))
            {
                deletables.push_back(index);
            }
        }
        for (const unsigned int idx : deletables)
        {
            DeleteLldpStateMachine(idx);
        }
    }

    void Tick()
    {
        for (auto &[idx, device] : current_state)
        {
            device.Tick();
        }
    }
};

void lldpStateUpdater(LldpRepository &lldp, DeviceRepository &repository)
{
    if (
        repository.device_reader.device_reader_state == ReaderState::IDLE &&
        repository.ip_reader.ip_reader_state == ReaderState::IDLE)
    {
        lldp.UpdateState(repository.devices);
    }
}

class ClockHandler final : public ndisc::EventHandler
{
    int socket_fd_;
    ndisc::NeighbourList *neighbour_list_;
    DeviceRepository *device_repository_;
    LldpRepository *lldp_repository_;

    uint16_t dump_timer_ = 0;

    ClockHandler(int socket_fd, ndisc::NeighbourList *neighbour_list, DeviceRepository *device_repository, LldpRepository *lldp) : socket_fd_(socket_fd),
                                                                                                                                   neighbour_list_(neighbour_list),
                                                                                                                                   device_repository_(device_repository),
                                                                                                                                   lldp_repository_(lldp) {}

public:
    void DumpInfo()
    {
        std::cout << "\033[2J";
        std::cout << "Device data:\n";
        std::cout << std::left
                  << std::setw(4) << "IDX"
                  << std::setw(24) << "Name"
                  << std::setw(18) << "MAC"
                  << std::setw(16) << "IP"
                  << '\n';
        for (const auto &[idx, device] : device_repository_->devices)
        {
            std::cout << std::left << std::setw(4) << device.if_index
                      << std::setw(24) << device.interface_name.value_or("---");
            if (device.mac_address.has_value())
            {
                std::cout << std::right << std::hex << std::setfill('0')
                          << std::setw(2) << (uint16_t)(*device.mac_address)[0] << ":"
                          << std::setw(2) << (uint16_t)(*device.mac_address)[1] << ":"
                          << std::setw(2) << (uint16_t)(*device.mac_address)[2] << ":"
                          << std::setw(2) << (uint16_t)(*device.mac_address)[3] << ":"
                          << std::setw(2) << (uint16_t)(*device.mac_address)[4] << ":"
                          << std::setw(2) << (uint16_t)(*device.mac_address)[5] << " "
                          << std::dec << std::setfill(' ');
            }
            else
            {
                std::cout << std::setw(18) << "";
            }
            if (device.ip_address.has_value())
            {
                std::cout << std::left
                          << (uint16_t)(*device.ip_address)[0] << '.'
                          << (uint16_t)(*device.ip_address)[1] << '.'
                          << (uint16_t)(*device.ip_address)[2] << '.'
                          << (uint16_t)(*device.ip_address)[3] << ' ';
            }
            else
            {
                std::cout << std::left << std::setw(16) << "Unknown";
            }
            std::cout << "\n";
        }
        std::cout << "\n\n";
        std::cout << "Found " << neighbour_list_->chassis_map.size() << " chassis\n";
        std::cout << "\n\nNeighbours:\n";
        std::cout << std::left
                  << std::setw(36) << "Chassis"
                  << std::setw(18) << "Port"
                  << std::setw(16) << "IP"
                  << std::setw(12) << "TTL"
                  << '\n';

        for (const auto &[chassis_id, port_map] : neighbour_list_->chassis_map)
        {

            std::string chassis;
            chassis.resize(chassis_id.size() - 2);
            std::copy(chassis_id.begin() + 1, chassis_id.end() - 1, chassis.begin());
            std::cout << "Chassis: " << chassis << "\n";
            for (const auto &[port_id, neighbour] : port_map)
            {
                std::cout << std::setw(36) << chassis;
                std::cout << std::right << std::hex << std::setfill('0')
                          << std::setw(2) << (uint16_t)(neighbour.port_id)[0] << ":"
                          << std::setw(2) << (uint16_t)(neighbour.port_id)[1] << ":"
                          << std::setw(2) << (uint16_t)(neighbour.port_id)[2] << ":"
                          << std::setw(2) << (uint16_t)(neighbour.port_id)[3] << ":"
                          << std::setw(2) << (uint16_t)(neighbour.port_id)[4] << ":"
                          << std::setw(2) << (uint16_t)(neighbour.port_id)[5] << " "
                          << std::dec << std::setfill(' ');
                if (neighbour.ip_address.has_value())
                {
                    std::cout << std::left
                              << (uint16_t)(*neighbour.ip_address)[0] << '.'
                              << (uint16_t)(*neighbour.ip_address)[1] << '.'
                              << (uint16_t)(*neighbour.ip_address)[2] << '.'
                              << (uint16_t)(*neighbour.ip_address)[3] << ' ';
                }
                else
                {
                    std::cout << std::left << std::setw(16) << "Unknown";
                }

                std::cout << std::setw(12) << neighbour.time_to_live;
                std::cout << '\n';
            }
        }
    }

    void Call() override
    {
        uint64_t times_triggered = 0;
        ssize_t bytes_received = read(socket_fd_, &times_triggered, sizeof(times_triggered));
        if (bytes_received < 0 || times_triggered == 0)
        {
            return;
        }
        std::vector<std::tuple<std::vector<uint8_t>, std::vector<uint8_t>>> timed_out_entries{};
        for (auto &[chassis, port_map] : neighbour_list_->chassis_map)
        {
            for (auto &[port, entry] : port_map)
            {
                if (entry.time_to_live <= times_triggered)
                {
                    entry.time_to_live = 0;
                    timed_out_entries.emplace_back(chassis, port);
                }
                else
                {
                    entry.time_to_live -= times_triggered;
                }
            }
        }
        for (const auto &[chassis, port] : timed_out_entries)
        {
            neighbour_list_->chassis_map[chassis].erase(port);
            if (neighbour_list_->chassis_map[chassis].empty())
            {
                neighbour_list_->chassis_map.erase(chassis);
            }
        }
        device_repository_->Tick();
        lldpStateUpdater(*lldp_repository_, *device_repository_);
        lldp_repository_->Tick();
        if (dump_timer_ == 0)
        {
            dump_timer_ = 5;
            DumpInfo();
        }
        dump_timer_--;
    }

    uint32_t GetEvents() const override
    {
        return EPOLLIN;
    }

    int GetSocket() const override
    {
        return socket_fd_;
    }

    static std::unique_ptr<ClockHandler> Create(ndisc::NeighbourList &neighbour_list, DeviceRepository &device_repository, LldpRepository &lldp_repository)
    {
        int socket_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
        if (socket_fd < 0)
        {
            return nullptr;
        }
        itimerspec timer_spec{};
        timer_spec.it_value.tv_nsec = 0;
        timer_spec.it_value.tv_sec = 1;
        timer_spec.it_interval.tv_nsec = 0;
        timer_spec.it_interval.tv_sec = 1;
        timerfd_settime(socket_fd, 0, &timer_spec, nullptr);
        return std::make_unique<ClockHandler>(ClockHandler(socket_fd, &neighbour_list, &device_repository, &lldp_repository));
    }
};

int main()
{
    std::expected<ndisc::EventManager, int> manager_create_result = ndisc::EventManager::Create();

    if (!manager_create_result.has_value())
    {
        std::cerr << "Failed to initialize Event manager. Errno: " << manager_create_result.error() << "\n";
        return -1;
    }
    ndisc::EventManager manager = std::move(manager_create_result.value());
    std::cout << "Event manager initialized" << std::endl;
    std::expected<std::unique_ptr<DeviceRepository>, int> repository_result = DeviceRepository::Create(manager);
    std::cout << "Device repository initialized" << std::endl;
    if (!repository_result.has_value())
    {
        std::cerr << "Failed to create device repository with errno " << repository_result.error();
    }
    std::unique_ptr<DeviceRepository> repository = std::move(repository_result.value());
    std::cout << "Device repository initialized" << std::endl;

    ndisc::NeighbourList neighbour_list;

    std::shared_ptr<ndisc::EthernetLldpMonitor> monitor = ndisc::EthernetLldpMonitor::Create(std::bind_front(ndisc::lldpFrameParser, std::ref(neighbour_list)));
    if (monitor == nullptr)
    {
        std::cerr << "Monitor is null\n";
        return -1;
    }
    std::expected<uint64_t, int> monitor_add_handle = manager.Add(monitor);
    if (!monitor_add_handle.has_value())
    {
        std::cerr << "Failed to add Ethernet monitor, errno: " << monitor_add_handle.error() << "\n";
        return -1;
    }

    std::cout << "Ethernet LLDP monitor initialized" << std::endl;

    LldpRepository lldp;

    std::shared_ptr<ClockHandler> clock = ClockHandler::Create(neighbour_list, *repository, lldp);
    if (clock == nullptr)
    {
        std::cerr << "Clock is nullptr\n";
        return -1;
    }
    std::expected<uint64_t, int> clock_add_handle = manager.Add(clock);
    if (!clock_add_handle.has_value())
    {
        std::cerr << "Failed to add Clock monitor, errno: " << clock_add_handle.error() << "\n";
        return -1;
    }

    std::cout << "Clock handler initialized" << std::endl;

    std::expected<std::unique_ptr<ndisc::data::DataTransportRepository>, int> dtr_result = ndisc::data::DataTransportRepository::Create(manager);
    if (!dtr_result.has_value())
    {
        std::cerr << "Failed to create DataTransportRepository, errno: " << dtr_result.error() << "\n";
        return -1;
    }
    if (dtr_result.value() == nullptr)
    {
        std::cerr << "DTR for some reason nullptr\n";
        return -1;
    }
    std::shared_ptr<ndisc::data::DataTransportRepository> dtr = std::move(dtr_result.value());

    std::cout << "Data Transport Repository initialized" << std::endl;

    std::expected<std::unique_ptr<ndisc::data::DataTransportListenSocket>, int> dtls_result = ndisc::data::DataTransportListenSocket::Create(*repository, neighbour_list, *dtr);
    if (!dtls_result.has_value())
    {
        std::cerr << "Failed to create DataTransportListenSocket, errno: " << dtls_result.error() << "\n";
        return -1;
    }
    if (dtls_result.value() == nullptr)
    {
        std::cerr << "DTLS somehow nullptr\n";
        return -1;
    }
    std::shared_ptr<ndisc::data::DataTransportListenSocket> dtls = std::move(*dtls_result);

    std::cout << "DTLS initialized" << std::endl;

    std::expected<uint64_t, int> dtr_handle = manager.Add(dtr);
    std::expected<uint64_t, int> dtls_handle = manager.Add(dtls);
    if (!dtr_handle.has_value())
    {
        std::cerr << "Failed to add DTR, errno: " << dtr_handle.error() << "\n";
        return -1;
    }
    if (!dtls_handle.has_value())
    {
        std::cerr << "Failed to add DTLS, errno: " << dtls_handle.error() << "\n";
        return -1;
    }

    std::cout << "Handlers initialized" << std::endl;

    while (true)
    {
        manager.Wait();
    }
}
