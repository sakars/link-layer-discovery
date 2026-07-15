#ifndef CLOCK_HANDLER_HH
#define CLOCK_HANDLER_HH

#include "event_handlers.hh"
#include "device_repository.hh"
#include "lldp_monitor.hh"
#include "lldp_repository.hh"

namespace ndisc
{

    class ClockHandler final : public EventHandler
    {
        int socket_fd_;
        NeighbourList *neighbour_list_;
        DeviceRepository *device_repository_;
        LldpRepository *lldp_repository_;

        uint16_t dump_timer_ = 0;

        ClockHandler(int socket_fd, NeighbourList *neighbour_list, DeviceRepository *device_repository, LldpRepository *lldp) : socket_fd_(socket_fd),
                                                                                                                                neighbour_list_(neighbour_list),
                                                                                                                                device_repository_(device_repository),
                                                                                                                                lldp_repository_(lldp) {}

    public:
        void DumpInfo();

        void Call() override;

        uint32_t GetEvents() const override;

        int GetSocket() const override;

        static std::unique_ptr<ClockHandler> Create(NeighbourList &neighbour_list, DeviceRepository &device_repository, LldpRepository &lldp_repository);
    };
} // namespace ndisc
#endif // CLOCK_HANDLER_HH