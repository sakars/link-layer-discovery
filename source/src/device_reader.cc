
#include "device_reader.hh"

#include <iostream>

namespace netlink
{
    static constexpr int DUMP_READ_TIMEOUT = 5;

    std::expected<std::unique_ptr<DeviceReader>, int> DeviceReader::Create(ndisc::EventManager &manager)
    {
        std::expected<std::unique_ptr<NetlinkSocket>, int> device_socket_result = netlink::NetlinkSocket::Create(0);

        if (!device_socket_result.has_value() || device_socket_result.value() == nullptr)
        {
            return std::unexpected(device_socket_result.error());
        }
        std::shared_ptr<NetlinkSocket> device_socket = std::move(*device_socket_result);
        std::expected<size_t, int> add_result = manager.Add(device_socket);
        if (!add_result.has_value())
        {
            return std::unexpected(add_result.error());
        }
        return std::make_unique<DeviceReader>(DeviceReader(std::move(device_socket), add_result.value(), &manager));
    }

    void DeviceReader::Tick(const uint64_t &delta_seconds)
    {
        if (dump_request_sequence_number_.has_value())
        {
            if (dump_read_timeout_ > static_cast<int>(delta_seconds))
            {
                dump_read_timeout_ -= static_cast<int>(delta_seconds);
            }
            else
            {
                dump_read_timeout_ = 0;
                ResetDumpAttempt();
                if (dump_errored_callback_.has_value())
                {
                    (*dump_errored_callback_)();
                }
            }
        }
    }

    bool DeviceReader::BindReaderSocketCallback()
    {
        if (reader_socket_ == nullptr)
        {
            return false;
        }
        reader_socket_->SetCallback([this](NetlinkPacketView packet)
                                    { this->HandleNetlinkPacket(std::move(packet)); });
        return true;
    }

    void DeviceReader::ResetDumpAttempt()
    {
        dump_read_timeout_ = -1;
        dump_request_sequence_number_ = std::nullopt;
    }

    std::expected<void, int> DeviceReader::TriggerDump()
    {
        if (reader_socket_ == nullptr)
        {
            std::cerr << "Failed to issue dump, socket empty\n";
            return std::unexpected(EINVAL);
        }
        dump_read_timeout_ = DUMP_READ_TIMEOUT;
        std::expected<int, int> dump_result = reader_socket_->SendGetLinkDumpMessage();
        if (!dump_result.has_value())
        {
            return std::unexpected(dump_result.error());
        }
        dump_request_sequence_number_ = *dump_result;
        return {};
    }

    void DeviceReader::HandleNetlinkPacket(NetlinkPacketView packet)
    {
        if (!dump_request_sequence_number_.has_value())
        {
            std::cout << "Ignoring packet, no message was expected.\n";
            return;
        }
        unsigned int received_sequence_number = std::visit([]<typename T>(NetlinkMessage<T> packet)
                                                           { return packet.header.nlmsg_seq; }, packet);
        if (received_sequence_number != dump_request_sequence_number_.value())
        {
            std::cerr << "Device sequence number mismatch. Possible bug..\n";
            return;
        }
        if (LinkView *link_message = std::get_if<LinkView>(&packet))
        {
            if (device_update_callback_.has_value())
            {
                (*device_update_callback_)(*link_message);
            }
            else
            {
                std::cout << "Link message got skipped as no callback is assigned.\n";
            }
        }
        else if (std::get_if<DoneView>(&packet) != nullptr)
        {
            ResetDumpAttempt();
            if (end_of_device_dump_callback_.has_value())
            {
                (*end_of_device_dump_callback_)();
            }
        }
        else if (std::get_if<ErrorView>(&packet) != nullptr)
        {
            ResetDumpAttempt();
            if (dump_errored_callback_.has_value())
            {
                (*dump_errored_callback_)();
            }
        }
    }
} // namespace netlink