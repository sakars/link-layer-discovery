
#include <bitset>
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <linux/if_packet.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <memory>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <unistd.h>

#include "netlink_monitor.hh"

using namespace std::chrono_literals;

namespace ndisc
{

    constexpr unsigned int RANDOM_SEQUENCE_MASK = 0x0FFF;
    constexpr std::chrono::milliseconds NETLINK_DELAY = 500ms;
    constexpr unsigned int NETLINK_DUMP_READ_ATTEMPTS = 5;

    NetlinkPacketView packetViewParser(std::span<uint8_t> packet)
    {
        nlmsghdr *header = reinterpret_cast<nlmsghdr *>(packet.data());
        if (header->nlmsg_type == RTM_GETLINK || header->nlmsg_type == RTM_NEWLINK || header->nlmsg_type == RTM_DELLINK)
        {
            ifinfomsg *interface_data = reinterpret_cast<ifinfomsg *>(NLMSG_DATA(header));
            size_t remaining_bytes = IFLA_PAYLOAD(header);
            std::vector<TLVView> attributes;
            for (rtattr *rta = reinterpret_cast<rtattr *>(packet.data() + RTA_ALIGN(NLMSG_LENGTH(sizeof(ifinfomsg))));
                 RTA_OK(rta, remaining_bytes);
                 rta = RTA_NEXT(rta, remaining_bytes))
            {
                attributes.emplace_back(
                    TLVView{
                        .attribute_header = rta,
                        .value = std::span<uint8_t>(reinterpret_cast<uint8_t *>(RTA_DATA(rta)), RTA_PAYLOAD(rta)),
                    });
            }
            return LinkView{
                .header = header,
                .content = LinkContentView{
                    .interface_info = interface_data,
                    .attributes = std::move(attributes),
                },
            };
        }

        if (header->nlmsg_type == RTM_GETADDR || header->nlmsg_type == RTM_NEWADDR || header->nlmsg_type == RTM_DELADDR)
        {
            ifaddrmsg *address_info = reinterpret_cast<ifaddrmsg *>(NLMSG_DATA(header));
            size_t remaining_bytes = IFA_PAYLOAD(header);
            std::vector<TLVView> attributes;
            for (rtattr *rta = reinterpret_cast<rtattr *>(packet.data() + RTA_ALIGN(NLMSG_LENGTH(sizeof(ifaddrmsg))));
                 RTA_OK(rta, remaining_bytes);
                 rta = RTA_NEXT(rta, remaining_bytes))
            {
                attributes.emplace_back(
                    TLVView{
                        .attribute_header = rta,
                        .value = std::span<uint8_t>(reinterpret_cast<uint8_t *>(RTA_DATA(rta)), RTA_PAYLOAD(rta)),
                    });
            }
            return AddrView{
                .header = header,
                .content = AddrContentView{
                    .address_info = address_info,
                    .attributes = std::move(attributes),
                },
            };
        }

        if (header->nlmsg_type == NLMSG_DONE)
        {
            int *error = reinterpret_cast<int *>(NLMSG_DATA(header));
            return DoneView{
                .header = header,
                .error = error,
            };
        }

        if (header->nlmsg_type == NLMSG_ERROR)
        {
            nlmsgerr *error_payload = reinterpret_cast<nlmsgerr *>(NLMSG_DATA(header));
            size_t remaining_bytes = NLMSG_PAYLOAD(header, sizeof(nlmsgerr));
            std::optional<MessageContentView> original_message = std::nullopt;
            rtattr *attribute_base = reinterpret_cast<rtattr *>(packet.data() + RTA_ALIGN(NLMSG_LENGTH(sizeof(nlmsgerr))));
            if ((header->nlmsg_flags & NLM_F_CAPPED) == 0)
            {
                uint8_t *data = reinterpret_cast<uint8_t *>(NLMSG_DATA(&(error_payload->msg)));
                size_t original_message_payload_size = NLMSG_PAYLOAD(&(error_payload->msg), 0);
                std::span<uint8_t> original_message_span = std::span<uint8_t>(data, original_message_payload_size);
                original_message = MessageContentView{
                    .content = original_message_span,
                };
                remaining_bytes = NLMSG_PAYLOAD(header, NLMSG_ALIGN(sizeof(nlmsgerr)) + original_message_payload_size);
                attribute_base = reinterpret_cast<rtattr *>(packet.data() + RTA_ALIGN(NLMSG_LENGTH(sizeof(nlmsgerr)) + original_message_payload_size));
            }
            std::vector<TLVView> attributes;
            for (rtattr *rta = attribute_base; RTA_OK(rta, remaining_bytes); rta = RTA_NEXT(rta, remaining_bytes))
            {
                attributes.emplace_back(
                    TLVView{
                        .attribute_header = rta,
                        .value = std::span<uint8_t>(reinterpret_cast<uint8_t *>(RTA_DATA(rta)), RTA_PAYLOAD(rta)),
                    });
            }
            return ErrorView{
                .header = header,
                .error = error_payload,
                .original_content = original_message,
                .attributes = attributes,
            };
        }
        size_t payload_size = NLMSG_PAYLOAD(header, 0);
        uint8_t *payload_start = reinterpret_cast<uint8_t *>(NLMSG_DATA(header));
        std::span<uint8_t> payload = std::span<uint8_t>(payload_start, payload_size);

        // std::cerr << "Unrecognized packet...\n";
        return MessageView{
            .header = header,
            .content = MessageContentView{
                .content = payload,
            },
        };
    }

    std::string getMachineId()
    {
        static std::string machine_id;
        if (machine_id.empty())
        {
            std::ifstream machine_id_file_stream("/etc/machine-id");
            machine_id_file_stream >> machine_id;
            machine_id_file_stream.close();
        }
        return machine_id;
    }

    void NetlinkSocket::LoadBatch(int socket_fd, std::vector<uint8_t> &data_buffer)
    {
        if (socket_fd < 0)
        {
            data_buffer.clear();
            std::cerr << "Unable to load batch: socket not valid\n";
            return;
        }
        sockaddr_nl source_address{};
        uint8_t tmp_data = 0;
        iovec buffer_data{
            .iov_base = &tmp_data,
            .iov_len = 1,
        };
        msghdr header_buffer{
            .msg_name = &source_address,
            .msg_namelen = sizeof(source_address),
            .msg_iov = &buffer_data,
            .msg_iovlen = 1,
            .msg_control = nullptr,
            .msg_controllen = 0,
            .msg_flags = 0,
        };

        const long peek_data_length = recvmsg(socket_fd, &header_buffer, MSG_PEEK | MSG_TRUNC);
        if (peek_data_length <= 0)
        {
            data_buffer.clear();
            return;
        }

        data_buffer = std::vector<uint8_t>(peek_data_length, 0);

        buffer_data.iov_base = data_buffer.data();
        buffer_data.iov_len = peek_data_length;

        const long data_length = recvmsg(socket_fd, &header_buffer, 0);
        if (data_length != peek_data_length)
        {
            data_buffer.clear();
            return;
        }
        if (source_address.nl_pid != KERNEL_PID)
        {
            data_buffer.clear();
            return;
        }
        data_buffer.resize(data_length);
    }

    std::optional<std::span<uint8_t>> NetlinkSocket::TryLoadFromSpan(std::span<uint8_t> &remaining_data)
    {
        size_t buffer_size = remaining_data.size();
        const nlmsghdr *header = reinterpret_cast<const nlmsghdr *>(remaining_data.data());
        if (NLMSG_OK(header, buffer_size))
        {
            size_t next_message_size = NLMSG_ALIGN(header->nlmsg_len);
            const std::span<uint8_t> message_span = remaining_data.subspan(0, next_message_size);
            remaining_data = remaining_data.subspan(next_message_size);
            return message_span;
        }
        return std::nullopt;
    }

    std::expected<std::unique_ptr<NetlinkSocket>, int> NetlinkSocket::Create(std::function<void(std::span<uint8_t>)> callback, uint32_t multicast_groups)
    {
        int socket_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
        if (socket_fd < 0)
        {
            std::cerr << "Failed to make socket\n";
            return std::unexpected(errno);
        }
        int enable = 1;
        setsockopt(socket_fd, SOL_NETLINK, NETLINK_EXT_ACK, static_cast<const void *>(&enable), sizeof(int));
        // unsigned int pid = getpid();
        sockaddr_nl address{};
        address.nl_family = AF_NETLINK;
        address.nl_pad = 0;
        address.nl_pid = 0;
        address.nl_groups = multicast_groups;
        int bind_result = bind(socket_fd, reinterpret_cast<const sockaddr *>(&address), sizeof(sockaddr_nl));
        if (bind_result < 0)
        {
            int err = errno;
            close(socket_fd);
            std::cerr << "Failed to bind: errno " << errno << "\n";
            return std::unexpected(err);
        }
        return std::make_unique<NetlinkSocket>(NetlinkSocket(socket_fd, std::move(callback)));
    }

    void NetlinkSocket::Call()
    {
        // empty current span
        for (std::optional<std::span<uint8_t>> packet_from_buffer = TryLoadFromSpan(remaining_data_);
             packet_from_buffer.has_value();
             packet_from_buffer = TryLoadFromSpan(remaining_data_))
        {
            callback_(packet_from_buffer.value());
        }
        LoadBatch(*socket_fd_, data_buffer_);
        remaining_data_ = std::span<uint8_t>(data_buffer_.begin(), data_buffer_.end());
        for (std::optional<std::span<uint8_t>> packet_from_buffer = TryLoadFromSpan(remaining_data_);
             packet_from_buffer.has_value();
             packet_from_buffer = TryLoadFromSpan(remaining_data_))
        {
            callback_(packet_from_buffer.value()); // NOLINT(bugprone-unchecked-optional-access)
        }
    }

    bool NetlinkSocket::IsReadable() const
    {
        size_t buffer_size = remaining_data_.size();
        const nlmsghdr *header = reinterpret_cast<const nlmsghdr *>(remaining_data_.data());
        if (NLMSG_OK(header, buffer_size))
        {
            return true;
        }
        sockaddr_nl source_address{};
        uint8_t tmp_data = 0;
        iovec buffer_data{
            .iov_base = &tmp_data,
            .iov_len = 1,
        };
        msghdr header_buffer{
            .msg_name = &source_address,
            .msg_namelen = sizeof(source_address),
            .msg_iov = &buffer_data,
            .msg_iovlen = 1,
            .msg_control = nullptr,
            .msg_controllen = 0,
            .msg_flags = 0,
        };

        const long peek_data_length = recvmsg(*socket_fd_, &header_buffer, MSG_PEEK | MSG_TRUNC);
        return peek_data_length > 0;
    }

    long NetlinkSocket::SendGetLinkDumpMessage()
    {
        if (!socket_fd_.IsValid())
        {
            return -2;
        }
        sequence_number_++;
        alignas(nlmsghdr) std::array<uint8_t, NLMSG_LENGTH(sizeof(struct ifinfomsg))> buffer{};
        nlmsghdr *netlink_header = reinterpret_cast<nlmsghdr *>(buffer.data());
        netlink_header->nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
        netlink_header->nlmsg_pid = getpid();
        netlink_header->nlmsg_seq = sequence_number_;
        netlink_header->nlmsg_flags = NLM_F_ACK | NLM_F_DUMP | NLM_F_REQUEST;
        netlink_header->nlmsg_type = RTM_GETLINK;
        ifinfomsg *interface_info = reinterpret_cast<ifinfomsg *>(NLMSG_DATA(netlink_header));
        interface_info->ifi_family = AF_UNSPEC;
        interface_info->ifi_type = 0;
        interface_info->ifi_change = 0;
        interface_info->ifi_flags = 0;
        interface_info->ifi_index = 0;

        const long bytes_sent = send(*socket_fd_, buffer.data(), netlink_header->nlmsg_len, 0);
        if (bytes_sent < netlink_header->nlmsg_len)
        {
            return -3;
        }
        return bytes_sent;
    }

    long NetlinkSocket::SendGetAddrMessage()
    {
        if (!socket_fd_.IsValid())
        {
            return -2;
        }
        sequence_number_++;
        alignas(nlmsghdr) std::array<uint8_t, NLMSG_LENGTH(sizeof(struct ifaddrmsg))> buffer{};
        nlmsghdr *netlink_header = reinterpret_cast<nlmsghdr *>(buffer.data());
        netlink_header->nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
        netlink_header->nlmsg_pid = getpid();
        netlink_header->nlmsg_seq = sequence_number_;
        netlink_header->nlmsg_flags = NLM_F_ACK | NLM_F_REQUEST | NLM_F_DUMP;
        netlink_header->nlmsg_type = RTM_GETADDR;
        ifaddrmsg *address_message = reinterpret_cast<ifaddrmsg *>(NLMSG_DATA(netlink_header));
        address_message->ifa_family = AF_INET;
        address_message->ifa_prefixlen = 0;
        address_message->ifa_scope = IFA_UNSPEC;
        address_message->ifa_flags = 0;
        address_message->ifa_index = 0;

        const long bytes_sent = send(*socket_fd_, buffer.data(), netlink_header->nlmsg_len, 0);
        if (bytes_sent < netlink_header->nlmsg_len)
        {
            return -3;
        }
        return bytes_sent;
    }

    std::optional<LldpSender> LldpSender::Create()
    {
        OwnedFileDescriptor socket_fd{socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))};
        if (!socket_fd.IsValid())
        {
            return std::nullopt;
        }
        return LldpSender(std::move(socket_fd));
    }

    void LldpSender::SendLldp(unsigned int interface, const std::array<uint8_t, ETH_ALEN> &mac, const std::optional<std::array<uint8_t, sizeof(in_addr)>> &ip_address, uint16_t ttl) const
    {
        if (interface > INT_MAX)
        {
            return;
        }
        static const std::array<uint8_t, 6> multicast_address = {0x01, 0x80, 0xC2, 0x00, 0x00, 0x00};
        LLDPEthernetFrame frame{};
        std::copy(multicast_address.begin(), multicast_address.end(), std::begin(frame.header.ether_dhost));
        std::copy(mac.begin(), mac.end(), std::begin(frame.header.ether_shost));
        frame.header.ether_type = htons(ETH_P_LLDP);
        frame.data_unit.chassis_id.type = lldp::CHASSIS_ID;
        std::string chassis = getMachineId();
        chassis = '\x07' + chassis;
        frame.data_unit.chassis_id.value.resize(chassis.size());
        std::copy(chassis.begin(), chassis.end(), frame.data_unit.chassis_id.value.begin());
        frame.data_unit.port_id.type = lldp::PORT_ID;
        frame.data_unit.port_id.value.resize(1 + ETH_ALEN);
        frame.data_unit.port_id.value[0] = 0x03;
        std::copy(mac.begin(), mac.end(), frame.data_unit.port_id.value.begin() + 1);
        frame.data_unit.time_to_live.type = lldp::TIME_TO_LIVE;
        frame.data_unit.time_to_live.value.resize(sizeof(ttl));
        const std::array<uint8_t, sizeof(ttl)> network_ttl = std::bit_cast<std::array<uint8_t, sizeof(ttl)>>(htons(ttl));
        std::copy(network_ttl.begin(), network_ttl.end(), frame.data_unit.time_to_live.value.begin());
        if (ip_address.has_value())
        {
            LLDPDUTypeLengthValue management_tlv;
            management_tlv.type = lldp::MANAGEMENT_ADDRESS;
            management_tlv.value.resize(sizeof(in_addr));
            std::copy(ip_address->begin(), ip_address->end(), management_tlv.value.begin());
            frame.data_unit.optional_tlv.push_back(management_tlv);
        }
        const std::vector<uint8_t> frame_buffer = frame.ToFrameBuffer();
        sockaddr_ll address{};
        address.sll_family = AF_PACKET;
        std::copy(multicast_address.begin(), multicast_address.end(), std::begin(address.sll_addr));
        address.sll_halen = multicast_address.size();
        address.sll_ifindex = static_cast<int>(interface);
        address.sll_protocol = htons(ETH_P_LLDP);
        ssize_t bytes = sendto(*socket_fd_, frame_buffer.data(), frame_buffer.size(), 0, reinterpret_cast<sockaddr *>(&address), sizeof(address));
        if (bytes < 0)
        {
            std::cout << "errno: " << errno << "\n";
        }
    }

    void DeviceData::EndTransmission()
    {
        if (lldp_sender.has_value() && mac_address.has_value())
        {
            lldp_sender->SendLldp(if_index, *mac_address, ip_address, 0);
        }
    }

    void DeviceData::TryTransmit()
    {
        if (trigger_ready && transmit_credits > 0 && mac_address.has_value())
        {
            trigger_ready = false;
            transmit_credits--;
            if (lldp_sender.has_value())
            {
                std::cout << "Transmitting lldp via device " << if_index << "\n";
                lldp_sender->SendLldp(if_index, *mac_address, ip_address, TARGET_TTL);
            }
        }
    }

    void DeviceData::TriggerTransmission()
    {
        if (fast_forward_counter > 0)
        {
            transmit_timer = MESSAGE_FAST_INTERVAL;
        }
        else
        {
            transmit_timer = MESSAGE_TRANSMIT_INTERVAL;
        }
        trigger_ready = true;
        TryTransmit();
    }
    void DeviceData::TimerExpired()
    {
        if (fast_forward_counter > 0)
        {
            fast_forward_counter--;
        }
        TriggerTransmission();
    }
    void DeviceData::NewNeighbour()
    {
        if (fast_forward_counter == 0)
        {
            fast_forward_counter = FAST_TRANSMIT_AMOUNT;
        }
        TimerExpired();
    }

    void DeviceData::LocalChangeDetected()
    {
        TriggerTransmission();
    }

    void DeviceData::Tick()
    {
        if (transmit_credits < MAX_TRANSMIT_CREDITS)
        {
            transmit_credits += 1;
        }
        if (transmit_timer > 0)
        {
            transmit_timer--;
        }
        if (transmit_timer == 0)
        {
            TimerExpired();
        }
        TryTransmit();
    }

} // namespace ndisc
