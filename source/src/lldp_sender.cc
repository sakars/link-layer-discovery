

#include <linux/if_arp.h>
#include <linux/if_packet.h>

#include "lldp_sender.hh"

namespace lldp
{

    constexpr uint16_t MAX_TRANSMIT_CREDITS = 5;
    constexpr uint16_t FAST_TRANSMIT_AMOUNT = 4;
    constexpr uint16_t TARGET_TTL = 30;
    constexpr uint16_t PACKET_HOLD_AMOUNT = 5;
    constexpr uint16_t MESSAGE_TRANSMIT_INTERVAL = TARGET_TTL / PACKET_HOLD_AMOUNT;
    constexpr uint16_t MESSAGE_FAST_INTERVAL = 1;

    void LldpSender::Update(const netlink::DeviceData &new_device_data)
    {
        bool any_changed = false;
        if (device_data_.interface_name != new_device_data.interface_name)
        {
            any_changed = true;
            device_data_.interface_name = new_device_data.interface_name;
        }
        if (device_data_.ipv4_address != new_device_data.ipv4_address)
        {
            any_changed = true;
            device_data_.ipv4_address = new_device_data.ipv4_address;
        }
        if (device_data_.ipv6_address != new_device_data.ipv6_address)
        {
            any_changed = true;
            device_data_.ipv6_address = new_device_data.ipv6_address;
        }
        if (device_data_.device_operational != new_device_data.device_operational)
        {
            any_changed = true;
            if (new_device_data.device_operational)
            {
                DisableSender();
            }
            else
            {
                EnableSender();
            }
            device_data_.device_operational = new_device_data.device_operational;
        }
        if (any_changed)
        {
            LocalChangeDetected();
        }
    }

    template <size_t AddressLength>
    static constexpr std::array<std::byte, 1 + 1 + AddressLength + 1 + 4 + 1> tlvValueManagementAddressTemplate(std::byte address_subtype)
    {
        std::array<std::byte, 1 + 1 + AddressLength + 1 + 4 + 1> tlv_value{};
        tlv_value[0] = std::byte{1 + AddressLength};
        tlv_value[1] = address_subtype;
        tlv_value[1 + 1 + AddressLength] = lldp::MANAGEMENT_TLV_IF_SUBTYPE_IFINDEX;
        return tlv_value;
    }

    static inline void createLldpduOptionalTlvs(const netlink::DeviceData &device_data, std::vector<lldp::LLDPDUTypeLengthValue> &tlvs)
    {
        tlvs.clear();
        if (device_data.ipv4_address.has_value())
        {
            static std::array<std::byte, 1 + 1 + sizeof(in_addr) + 1 + 4 + 1> ipv4_value =
                tlvValueManagementAddressTemplate<sizeof(in_addr)>(lldp::MANAGEMENT_TLV_ADDRESS_SUBTYPE_IPV4);
            std::ranges::copy(device_data.ipv4_address.value(), std::next(ipv4_value.begin(), 2));
            uint32_t if_index = htonl(device_data.if_index);
            std::ranges::copy(std::as_bytes(std::span<uint32_t, 1>(&if_index, 1)), std::next(ipv4_value.begin(), 1 + 1 + sizeof(in_addr) + 1));
            tlvs.emplace_back(lldp::MANAGEMENT_ADDRESS, std::span(ipv4_value));
        }
        if (device_data.ipv6_address.has_value())
        {
            static std::array<std::byte, 1 + 1 + sizeof(in6_addr) + 1 + 4 + 1> ipv6_value =
                tlvValueManagementAddressTemplate<sizeof(in6_addr)>(lldp::MANAGEMENT_TLV_ADDRESS_SUBTYPE_IPV6);
            std::ranges::copy(device_data.ipv6_address.value(), std::next(ipv6_value.begin(), 2));
            uint32_t if_index = htonl(device_data.if_index);
            std::ranges::copy(std::as_bytes(std::span<uint32_t, 1>(&if_index, 1)), std::next(ipv6_value.begin(), 1 + 1 + sizeof(in6_addr) + 1));
            tlvs.emplace_back(lldp::MANAGEMENT_ADDRESS, std::span(ipv6_value));
        }
        if (!device_data.ipv4_address.has_value() && !device_data.ipv6_address.has_value() && device_data.mac_address.has_value())
        {
            static std::array<std::byte, 1 + 1 + ETH_ALEN + 1 + 4 + 1> mac_value =
                tlvValueManagementAddressTemplate<ETH_ALEN>(lldp::MANAGEMENT_TLV_ADDRESS_SUBTYPE_MAC);
            std::ranges::copy(device_data.mac_address.value(), std::next(mac_value.begin(), 2));
            uint32_t if_index = htonl(device_data.if_index);
            std::ranges::copy(std::as_bytes(std::span<uint32_t, 1>(&if_index, 1)), std::next(mac_value.begin(), 1 + 1 + ETH_ALEN + 1));
            tlvs.emplace_back(lldp::MANAGEMENT_ADDRESS, std::span(mac_value));
        }
    }

    static const std::array<uint8_t, ETH_ALEN> MULTICAST_ADDRESS = {0x01, 0x80, 0xC2, 0x00, 0x00, 0x00};

    static inline std::span<std::byte> getMachineIdTlvValue()
    {
        static std::vector<std::byte> machine_id_vector{};
        if (machine_id_vector.empty())
        {
            const std::string &chassis = netlink::getMachineId();
            machine_id_vector.resize(chassis.size() + 1);
            machine_id_vector[0] = lldp::PORT_TLV_SUBTYPE_LOCAL;
            std::ranges::copy(
                std::as_bytes(std::span(chassis)),
                machine_id_vector.begin() + 1);
        }
        return std::span<std::byte>(machine_id_vector);
    }

    static inline void constructFrame(const uint16_t &ttl, const netlink::DeviceData &device_data, lldp::LLDPEthernetFrame &frame)
    {
        if (!device_data.mac_address.has_value())
        {
            return;
        }
        std::ranges::copy(MULTICAST_ADDRESS, std::begin(frame.header.ether_dhost));
        std::ranges::copy(
            device_data.mac_address.value(),
            std::as_writable_bytes(std::span(frame.header.ether_shost)).begin());
        frame.header.ether_type = htons(ETH_P_LLDP);
        frame.data_unit.chassis_id.type = lldp::CHASSIS_ID;
        frame.data_unit.chassis_id.value = getMachineIdTlvValue();
        frame.data_unit.port_id.type = lldp::PORT_ID;
        std::array<std::byte, 1 + ETH_ALEN> mac_address{};
        mac_address[0] = PORT_TLV_SUBTYPE_MAC;
        std::ranges::copy(device_data.mac_address.value(), mac_address.begin() + 1);
        frame.data_unit.port_id.value = std::span<const std::byte>(mac_address.begin(), mac_address.end());
        frame.data_unit.time_to_live.type = lldp::TIME_TO_LIVE;
        uint16_t ttl_network_bo = htons(ttl);
        frame.data_unit.time_to_live.value = std::as_bytes(std::span(&ttl_network_bo, 1));
        createLldpduOptionalTlvs(device_data, frame.data_unit.optional_tlv);
    }

    static inline ssize_t sendFrame(ndisc::OwnedFileDescriptor &socket_fd, const lldp::LLDPEthernetFrame &frame, int if_index)
    {
        static std::vector<std::byte> frame_buffer{};
        frame_buffer.resize(frame.GetFrameBufferSize());
        std::span<std::byte> frame_buffer_view{frame_buffer};
        std::span<std::byte>::iterator iter = frame.ToFrameBuffer(frame_buffer_view.begin());
        if (iter != frame_buffer_view.end())
        {
            std::cerr << "Warning: LldpSender frame write incomplete\n";
        }
        sockaddr_ll address{
            .sll_family = AF_PACKET,
            .sll_protocol = htons(ETH_P_LLDP),
            .sll_ifindex = if_index,
            .sll_hatype = 0,
            .sll_pkttype = 0,
            .sll_halen = ETH_ALEN,
            .sll_addr = {},
        };
        std::ranges::copy(MULTICAST_ADDRESS, std::begin(address.sll_addr));
        return sendto(*socket_fd, frame_buffer.data(), frame_buffer.size(), 0, reinterpret_cast<sockaddr *>(&address), sizeof(address));
    }

    void LldpSender::SendLldp(uint16_t ttl)
    {
        if (device_data_.if_index > INT_MAX || !device_data_.mac_address.has_value())
        {
            return;
        }
        static lldp::LLDPEthernetFrame frame{};
        constructFrame(ttl, device_data_, frame);
        ssize_t bytes = sendFrame(*socket_fd_, frame, static_cast<int>(device_data_.if_index));
        if (bytes < 0)
        {
            int error = errno;
            if (error == ENETDOWN)
            {
                device_data_.device_operational = false;
                DisableSender();
            }
            std::cerr << "errno: " << errno << "\n";
        }
    }

    void LldpSender::EndTransmission()
    {
        if (device_data_.mac_address.has_value())
        {
            SendLldp(0);
        }
    }

    void LldpSender::TryTransmit()
    {
        if (trigger_ready_ && transmit_credits_ > 0 && device_data_.mac_address.has_value())
        {
            trigger_ready_ = false;
            transmit_credits_--;
            SendLldp(TARGET_TTL);
        }
    }

    void LldpSender::TriggerTransmission()
    {
        if (fast_forward_counter_ > 0)
        {
            transmit_timer_ = MESSAGE_FAST_INTERVAL;
        }
        else
        {
            transmit_timer_ = MESSAGE_TRANSMIT_INTERVAL;
        }
        trigger_ready_ = true;
        TryTransmit();
    }
    void LldpSender::TimerExpired()
    {
        if (fast_forward_counter_ > 0)
        {
            fast_forward_counter_--;
        }
        TriggerTransmission();
    }
    void LldpSender::NewNeighbour()
    {
        if (fast_forward_counter_ == 0)
        {
            fast_forward_counter_ = FAST_TRANSMIT_AMOUNT;
        }
        TimerExpired();
    }

    void LldpSender::LocalChangeDetected()
    {
        TriggerTransmission();
    }

    void LldpSender::Tick(const uint64_t &delta_seconds)
    {
        if (device_data_.device_operational)
        {
            if (transmit_credits_ + delta_seconds <= MAX_TRANSMIT_CREDITS)
            {
                transmit_credits_ += delta_seconds;
            }
            if (transmit_timer_ > delta_seconds)
            {
                transmit_timer_ -= delta_seconds;
            }
            else
            {
                transmit_timer_ = 0;
                TimerExpired();
            }
            TryTransmit();
        }
    }

    void LldpSender::DisableSender()
    {
    }

    void LldpSender::EnableSender()
    {
        NewNeighbour();
    }
} // namespace lldp