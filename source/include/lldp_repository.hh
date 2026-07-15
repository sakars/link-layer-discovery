#ifndef LLDP_REPOSITORY_HH
#define LLDP_REPOSITORY_HH

#include <map>

#include "netlink_monitor.hh"
#include "device_repository.hh"

namespace ndisc
{
    struct LldpRepository
    {
        std::map<unsigned int, ndisc::DeviceData> current_state;

        void CheckSocketForTxReady(unsigned int idx);

        void MarkChangedLldpStateMachine(unsigned int idx);

        void DeleteLldpStateMachine(unsigned int idx);

        void CreateLldpStateMachine(unsigned int idx);

        void UpdateState(std::map<unsigned int, ndisc::DeviceData> &new_state);

        void Tick();
    };

    void lldpStateUpdater(LldpRepository &lldp, DeviceRepository &repository);
} // namespace ndisc

#endif // LLDP_REPOSITORY_HH