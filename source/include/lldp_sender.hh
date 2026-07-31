#ifndef LLDP_SENDER_HH
#define LLDP_SENDER_HH

#include <cstdint>

#include "owned_file_descriptor.hh"
#include "netlink_monitor.hh"

namespace lldp
{
    class LldpSender
    {
        ndisc::OwnedFileDescriptor *socket_fd_;
        netlink::DeviceData device_data_;
        uint16_t transmit_timer_ = 0;
        uint16_t transmit_credits_ = 0;
        uint16_t fast_forward_counter_ = 0;
        bool trigger_ready_ = false;

    public:
        LldpSender(ndisc::OwnedFileDescriptor &socket, const netlink::DeviceData &device_data) : socket_fd_(&socket), device_data_(device_data)
        {
            if (device_data_.device_operational)
            {
                EnableSender();
            }
            else
            {
                DisableSender();
            }
        }

        void Update(const netlink::DeviceData &);

        void SendLldp(uint16_t ttl);

        void EndTransmission();

        void TryTransmit();

        void TriggerTransmission();

        void TimerExpired();

        void NewNeighbour();

        void LocalChangeDetected();

        void Tick(const uint64_t &);

        const netlink::DeviceData &GetDeviceData() const { return device_data_; }

        void DisableSender();

        void EnableSender();
    };
} // namespace lldp

#endif // LLDP_SENDER_HH