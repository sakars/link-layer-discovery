#ifndef CLOCK_HANDLER_HH
#define CLOCK_HANDLER_HH

#include "event_handlers.hh"
#include "device_repository.hh"
#include "lldp_monitor.hh"
#include "lldp_repository.hh"
#include "owned_file_descriptor.hh"

namespace ndisc
{

    class ClockHandler final : public EventHandler
    {
        OwnedFileDescriptor socket_fd_;
        NeighbourList *neighbour_list_;
        LldpRepository *lldp_repository_;
        bool dumping_enabled_;

        uint16_t dump_timer_ = 0;

        ClockHandler(OwnedFileDescriptor &&socket_fd,
                     NeighbourList *neighbour_list,
                     LldpRepository *lldp, bool dumping_enabled) : socket_fd_(std::move(socket_fd)),
                                                                   neighbour_list_(neighbour_list),
                                                                   lldp_repository_(lldp),
                                                                   dumping_enabled_(dumping_enabled) {}

    public:
        void DumpInfo();

        void Call() override;

        uint32_t GetEvents() const override;

        int GetSocket() const override;

        static std::expected<std::unique_ptr<ClockHandler>, int> Create(NeighbourList &neighbour_list, LldpRepository &lldp_repository, bool verbose);
    };
} // namespace ndisc
#endif // CLOCK_HANDLER_HH