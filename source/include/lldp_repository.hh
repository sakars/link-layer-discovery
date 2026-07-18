#ifndef LLDP_REPOSITORY_HH
#define LLDP_REPOSITORY_HH

#include <map>

#include "netlink_monitor.hh"
#include "device_repository.hh"

namespace ndisc
{
    class LldpRepository
    {
        OwnedFileDescriptor ethernet_broadcast_socket_;
        std::map<unsigned int, LldpSender> current_state_;

        LldpRepository(OwnedFileDescriptor &&ethernet_socket) : ethernet_broadcast_socket_(std::move(ethernet_socket)) {}

    public:
        static std::expected<LldpRepository, int> Create();

        void MarkChangedLldpStateMachine(unsigned int idx);

        void DeleteLldpStateMachine(unsigned int idx);

        void CreateLldpStateMachine(unsigned int idx);

        void UpdateState(const std::map<unsigned int, DeviceData> &new_state);

        void Tick();
    };

    void lldpStateUpdater(LldpRepository &lldp, DeviceRepository &repository);
} // namespace ndisc

#endif // LLDP_REPOSITORY_HH