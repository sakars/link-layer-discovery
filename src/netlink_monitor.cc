
#include "netlink_monitor.hh"
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if_arp.h>
#include <iostream>
#include <unistd.h>
#include <memory>
#include <net/if.h>
#include <bitset>
#include <functional>

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

} // namespace ndisc
