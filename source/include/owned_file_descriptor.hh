#ifndef OWNED_FILE_DESCRIPTOR_HH
#define OWNED_FILE_DESCRIPTOR_HH

#include <string>
#include <unistd.h>
#include <utility>

namespace ndisc
{
    class OwnedFileDescriptor
    {
        int fd_;

    public:
        OwnedFileDescriptor(int file_descriptor) : fd_(file_descriptor) {}

        OwnedFileDescriptor(const OwnedFileDescriptor &) = delete;
        OwnedFileDescriptor(OwnedFileDescriptor &&) noexcept;

        OwnedFileDescriptor &operator=(const OwnedFileDescriptor &) = delete;
        OwnedFileDescriptor &operator=(OwnedFileDescriptor &&) noexcept;

        ~OwnedFileDescriptor();

        int Get() const { return fd_; }

        const int &operator*() const { return fd_; }

        bool IsValid() const { return fd_ >= 0; }
    };

    class DeletingOwnedFileDescriptor
    {
        OwnedFileDescriptor fd_;
        std::string path_;

    public:
        DeletingOwnedFileDescriptor(OwnedFileDescriptor &&file_descriptor, const std::string &path) : fd_(std::move(file_descriptor)), path_(path)
        {
        }

        DeletingOwnedFileDescriptor(const DeletingOwnedFileDescriptor &) = delete;
        DeletingOwnedFileDescriptor(DeletingOwnedFileDescriptor &&other) noexcept : fd_(std::move(other.fd_)),
                                                                                    path_(std::move(other.path_))
        {
            other.path_ = "";
        }

        DeletingOwnedFileDescriptor &operator=(const DeletingOwnedFileDescriptor &) = delete;
        DeletingOwnedFileDescriptor &operator=(DeletingOwnedFileDescriptor &&other) noexcept
        {
            fd_ = std::move(other.fd_);
            path_ = std::move(other.path_);
            other.path_ = "";
            return *this;
        }

        ~DeletingOwnedFileDescriptor()
        {
            if (fd_.IsValid())
            {
                unlink(path_.c_str());
            }
        }

        int Get() const { return fd_.Get(); }

        const int &operator*() const { return *fd_; }

        bool IsValid() const { return fd_.IsValid(); }
    };

} // namespace ndisc

#endif // OWNED_FILE_DESCRIPTOR_HH
