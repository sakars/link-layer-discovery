#ifndef LLDP_REPOSITORY_HH
#define LLDP_REPOSITORY_HH

#include <map>

#include "netlink_monitor.hh"
#include "device_repository.hh"
#include "lldp_sender.hh"

namespace ndisc
{
    class LldpRepository
    {
        OwnedFileDescriptor ethernet_broadcast_socket_;
        std::map<unsigned int, lldp::LldpSender> current_state_;
        std::unique_ptr<DeviceRepository> device_repository_;

        LldpRepository(OwnedFileDescriptor &&ethernet_socket,
                       std::unique_ptr<DeviceRepository> device_repository) : ethernet_broadcast_socket_(std::move(ethernet_socket)),
                                                                              device_repository_(std::move(device_repository))
        {
            device_repository_->SetSyncCallback([this](const std::map<unsigned int, netlink::DeviceData> &new_state)
                                                { UpdateState(new_state); });
        }

    public:
        LldpRepository(LldpRepository &&other) noexcept : ethernet_broadcast_socket_(std::move(other.ethernet_broadcast_socket_)),
                                                          device_repository_(std::move(other.device_repository_))
        {
            device_repository_->SetSyncCallback([this](const std::map<unsigned int, netlink::DeviceData> &new_state)
                                                { UpdateState(new_state); });
        }
        LldpRepository(LldpRepository &) = delete;
        LldpRepository &operator=(LldpRepository &&other) noexcept
        {
            ethernet_broadcast_socket_ = std::move(other.ethernet_broadcast_socket_);
            device_repository_ = std::move(other.device_repository_);
            device_repository_->SetSyncCallback([this](const std::map<unsigned int, netlink::DeviceData> &new_state)
                                                { UpdateState(new_state); });
            return *this;
        }
        LldpRepository &operator=(LldpRepository &) = delete;
        ~LldpRepository()
        {
            if (device_repository_ != nullptr)
            {
                device_repository_->ClearSyncCallback();
            }
        }

        static std::expected<LldpRepository, int> Create(ndisc::EventManager &manager, bool verbose);

        void MarkChangedLldpStateMachine(unsigned int idx);

        void DeleteLldpStateMachine(unsigned int idx);

        void CreateLldpStateMachine(unsigned int idx);

        void UpdateState(const std::map<unsigned int, netlink::DeviceData> &new_state);

        const std::map<unsigned int, lldp::LldpSender> &GetDeviceInfo() const { return current_state_; }

        void Tick(const uint64_t &);
    };
} // namespace ndisc

#endif // LLDP_REPOSITORY_HH