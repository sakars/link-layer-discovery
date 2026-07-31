

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

    static std::vector<lldp::LLDPDUTypeLengthValue> createLldpduOptionalTlvs(const netlink::DeviceData &device_data)
    {
        std::vector<lldp::LLDPDUTypeLengthValue> tlvs{};
        if (device_data.ipv4_address.has_value())
        {
            lldp::LLDPDUTypeLengthValue management_tlv;
            management_tlv.type = lldp::MANAGEMENT_ADDRESS;
            management_tlv.value.resize(1 + 1 + sizeof(in_addr) + 1 + 4 + 1);
            auto iter = management_tlv.value.begin();
            *iter++ = std::byte(1 + sizeof(in_addr));
            *iter++ = lldp::MANAGEMENT_TLV_ADDRESS_SUBTYPE_IPV4;
            iter = std::ranges::copy(device_data.ipv4_address.value(), iter).out;
            *iter++ = lldp::MANAGEMENT_TLV_IF_SUBTYPE_IFINDEX;
            uint32_t if_index = htonl(device_data.if_index);
            iter = std::ranges::copy(std::as_bytes(std::span(&if_index, 1)), iter).out;
            *iter++ = std::byte{0x00};
            tlvs.push_back(management_tlv);
        }
        if (device_data.ipv6_address.has_value())
        {
            lldp::LLDPDUTypeLengthValue management_tlv;
            management_tlv.type = lldp::MANAGEMENT_ADDRESS;
            management_tlv.value.resize(1 + 1 + sizeof(in6_addr) + 1 + 4 + 1);
            auto iter = management_tlv.value.begin();
            *iter++ = std::byte(1 + sizeof(in6_addr));
            *iter++ = lldp::MANAGEMENT_TLV_ADDRESS_SUBTYPE_IPV6;
            iter = std::ranges::copy(device_data.ipv6_address.value(), iter).out;
            *iter++ = lldp::MANAGEMENT_TLV_IF_SUBTYPE_IFINDEX;
            uint32_t if_index = htonl(device_data.if_index);
            iter = std::ranges::copy(std::as_bytes(std::span(&if_index, 1)), iter).out;
            *iter++ = std::byte{0x00};
            tlvs.push_back(management_tlv);
        }
        if (!device_data.ipv4_address.has_value() && !device_data.ipv6_address.has_value() && device_data.mac_address.has_value())
        {
            lldp::LLDPDUTypeLengthValue management_tlv;
            management_tlv.type = lldp::MANAGEMENT_ADDRESS;
            management_tlv.value.resize(1 + 1 + ETH_ALEN + 1 + 4 + 1);
            auto iter = management_tlv.value.begin();
            *iter++ = std::byte(1 + ETH_ALEN);
            *iter++ = lldp::MANAGEMENT_TLV_ADDRESS_SUBTYPE_MAC;
            iter = std::ranges::copy(device_data.mac_address.value(), iter).out;
            *iter++ = lldp::MANAGEMENT_TLV_IF_SUBTYPE_IFINDEX;
            uint32_t if_index = htonl(device_data.if_index);
            iter = std::ranges::copy(std::as_bytes(std::span(&if_index, 1)), iter).out;
            *iter++ = std::byte{0x00};
            tlvs.push_back(management_tlv);
        }
        return tlvs;
    }

    static const std::array<uint8_t, ETH_ALEN> MULTICAST_ADDRESS = {0x01, 0x80, 0xC2, 0x00, 0x00, 0x00};
    static inline lldp::LLDPEthernetFrame constructFrame(const uint16_t &ttl, const netlink::DeviceData &device_data)
    {
        if (!device_data.mac_address.has_value())
        {
            return {};
        }
        lldp::LLDPEthernetFrame frame{};
        std::ranges::copy(MULTICAST_ADDRESS, std::begin(frame.header.ether_dhost));
        std::ranges::copy(
            device_data.mac_address.value(),
            std::as_writable_bytes(std::span(frame.header.ether_shost)).begin());
        frame.header.ether_type = htons(ETH_P_LLDP);
        frame.data_unit.chassis_id.type = lldp::CHASSIS_ID;
        std::string chassis = netlink::getMachineId();
        frame.data_unit.chassis_id.value.resize(chassis.size() + 1);
        frame.data_unit.chassis_id.value[0] = lldp::PORT_TLV_SUBTYPE_LOCAL;
        std::ranges::copy(
            std::as_bytes(std::span(chassis)),
            frame.data_unit.chassis_id.value.begin() + 1);
        frame.data_unit.port_id.type = lldp::PORT_ID;
        frame.data_unit.port_id.value.resize(1 + ETH_ALEN);
        frame.data_unit.port_id.value[0] = lldp::PORT_ID_MAC_TYPE;
        std::ranges::copy(device_data.mac_address.value(), frame.data_unit.port_id.value.begin() + 1);
        frame.data_unit.time_to_live.type = lldp::TIME_TO_LIVE;
        frame.data_unit.time_to_live.value.resize(sizeof(ttl));
        const std::array<std::byte, sizeof(ttl)> network_ttl = std::bit_cast<std::array<std::byte, sizeof(ttl)>>(htons(ttl));
        std::ranges::copy(network_ttl, frame.data_unit.time_to_live.value.begin());
        frame.data_unit.optional_tlv = createLldpduOptionalTlvs(device_data);
        return frame;
    }

    static inline ssize_t sendFrame(ndisc::OwnedFileDescriptor &socket_fd, const lldp::LLDPEthernetFrame &frame, int if_index)
    {

        std::vector<std::byte> frame_buffer{frame.GetFrameBufferSize(), std::byte{0x00}};
        std::span<std::byte> frame_buffer_view{frame_buffer};
        std::span<std::byte>::iterator iter = frame.ToFrameBuffer(frame_buffer_view.begin());
        if (iter != frame_buffer_view.end())
        {
            std::cerr << "Warning: LldpSender frame write incomplete\n";
        }
        sockaddr_ll address{};
        address.sll_family = AF_PACKET;
        std::ranges::copy(MULTICAST_ADDRESS, std::begin(address.sll_addr));
        address.sll_halen = MULTICAST_ADDRESS.size();
        address.sll_ifindex = if_index;
        address.sll_protocol = htons(ETH_P_LLDP);
        return sendto(*socket_fd, frame_buffer.data(), frame_buffer.size(), 0, reinterpret_cast<sockaddr *>(&address), sizeof(address));
    }

    void LldpSender::SendLldp(uint16_t ttl)
    {
        if (device_data_.if_index > INT_MAX || !device_data_.mac_address.has_value())
        {
            return;
        }
        lldp::LLDPEthernetFrame frame = constructFrame(ttl, device_data_);
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