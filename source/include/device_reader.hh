#ifndef DEVICE_READER_HH
#define DEVICE_READER_HH

#include <memory>
#include <optional>

#include "netlink_monitor.hh"

namespace netlink
{
    class DeviceReader
    {
        std::shared_ptr<NetlinkSocket> reader_socket_;
        std::optional<unsigned int> dump_request_sequence_number_;
        int dump_read_timeout_{-1};

        std::optional<std::function<void(LinkView)>> device_update_callback_;
        std::optional<std::function<void()>> end_of_device_dump_callback_;
        std::optional<std::function<void()>> dump_errored_callback_;

        void HandleNetlinkPacket(NetlinkPacketView);
        bool BindReaderSocketCallback();
        void ResetDumpAttempt();

        DeviceReader(std::shared_ptr<NetlinkSocket> reader) : reader_socket_(std::move(reader))
        {
            BindReaderSocketCallback();
        }

    public:
        DeviceReader(const DeviceReader &) = delete;
        DeviceReader(DeviceReader &&other) noexcept : reader_socket_(std::move(other.reader_socket_)),
                                                      dump_request_sequence_number_(other.dump_request_sequence_number_),
                                                      dump_read_timeout_(other.dump_read_timeout_),
                                                      device_update_callback_(std::move(other.device_update_callback_)),
                                                      end_of_device_dump_callback_(std::move(other.end_of_device_dump_callback_)),
                                                      dump_errored_callback_(std::move(other.dump_errored_callback_))
        {
            other.reader_socket_.reset();
            other.dump_request_sequence_number_.reset();
            other.dump_read_timeout_ = -1;
            other.device_update_callback_.reset();
            other.end_of_device_dump_callback_.reset();
            other.dump_errored_callback_.reset();
            BindReaderSocketCallback();
        }
        DeviceReader &operator=(const DeviceReader &) = delete;
        DeviceReader &operator=(DeviceReader &&other) noexcept
        {
            reader_socket_ = std::move(other.reader_socket_);
            dump_request_sequence_number_ = other.dump_request_sequence_number_;
            dump_read_timeout_ = other.dump_read_timeout_;
            device_update_callback_ = std::move(other.device_update_callback_);
            end_of_device_dump_callback_ = std::move(other.end_of_device_dump_callback_);
            dump_errored_callback_ = std::move(other.dump_errored_callback_);
            other.reader_socket_.reset();
            other.dump_request_sequence_number_.reset();
            other.dump_read_timeout_ = -1;
            other.device_update_callback_.reset();
            other.end_of_device_dump_callback_.reset();
            other.dump_errored_callback_.reset();
            BindReaderSocketCallback();
            return *this;
        };
        ~DeviceReader()
        {
            if (reader_socket_ != nullptr)
            {
                reader_socket_->ClearCallback();
            }
        }

        static std::expected<std::unique_ptr<netlink::DeviceReader>, int> Create(ndisc::EventManager &manager);

        void Tick(const uint64_t &);

        std::expected<void, int> TriggerDump();

        void SetDeviceUpdateCallback(std::function<void(LinkView)> callback)
        {
            device_update_callback_ = std::move(callback);
        }

        void ClearDeviceUpdateCallback() { device_update_callback_ = std::nullopt; }

        void SetEndOfDumpCallback(std::function<void()> callback)
        {
            end_of_device_dump_callback_ = std::move(callback);
        }

        void ClearEndOfDumpCallback() { end_of_device_dump_callback_ = std::nullopt; }

        void SetDumpErroredCallback(std::function<void()> callback)
        {
            dump_errored_callback_ = std::move(callback);
        }

        void ClearDumpErroredCallback() { dump_errored_callback_ = std::nullopt; }
    };

} // namespace netlink

#endif // DEVICE_READER_HH
