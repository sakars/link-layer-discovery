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
        uint64_t reader_socket_event_handler_handle_;
        ndisc::EventManager *event_manager_;
        std::optional<unsigned int> dump_request_sequence_number_;
        int dump_read_timeout_{-1};

        std::optional<std::function<void(LinkView)>> device_update_callback_;
        std::optional<std::function<void()>> end_of_device_dump_callback_;
        std::optional<std::function<void()>> dump_errored_callback_;

        void HandleNetlinkPacket(NetlinkPacketView);
        bool BindReaderSocketCallback();
        void ResetDumpAttempt();

        DeviceReader(std::shared_ptr<NetlinkSocket> reader, uint64_t reader_handle_, ndisc::EventManager *event_manager) : reader_socket_(std::move(reader)),
                                                                                                                           reader_socket_event_handler_handle_(reader_handle_),
                                                                                                                           event_manager_(event_manager)
        {
            BindReaderSocketCallback();
        }

    public:
        DeviceReader(const DeviceReader &) = delete;
        DeviceReader(DeviceReader &&other) noexcept : reader_socket_(std::move(other.reader_socket_)),
                                                      reader_socket_event_handler_handle_(other.reader_socket_event_handler_handle_),
                                                      event_manager_(other.event_manager_),
                                                      dump_request_sequence_number_(other.dump_request_sequence_number_),
                                                      dump_read_timeout_(other.dump_read_timeout_),
                                                      device_update_callback_(std::move(other.device_update_callback_)),
                                                      end_of_device_dump_callback_(std::move(other.end_of_device_dump_callback_)),
                                                      dump_errored_callback_(std::move(other.dump_errored_callback_))
        {
            other.reader_socket_.reset();
            other.reader_socket_event_handler_handle_ = 0;
            other.event_manager_ = nullptr;
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
            reader_socket_event_handler_handle_ = other.reader_socket_event_handler_handle_;
            event_manager_ = other.event_manager_;
            dump_request_sequence_number_ = other.dump_request_sequence_number_;
            dump_read_timeout_ = other.dump_read_timeout_;
            device_update_callback_ = std::move(other.device_update_callback_);
            end_of_device_dump_callback_ = std::move(other.end_of_device_dump_callback_);
            dump_errored_callback_ = std::move(other.dump_errored_callback_);
            other.reader_socket_.reset();
            other.reader_socket_event_handler_handle_ = 0;
            other.event_manager_ = nullptr;
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
            if (event_manager_ != nullptr && reader_socket_event_handler_handle_ != 0)
            {
                std::expected<void, int> remove_result = event_manager_->Remove(reader_socket_event_handler_handle_);
                if (remove_result.has_value())
                {
                    std::cerr << "DeviceReader: Failed to remove reader socket from event handler, errno: " << remove_result.error() << "\n";
                }
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
