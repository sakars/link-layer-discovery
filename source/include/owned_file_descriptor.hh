#ifndef OWNED_FILE_DESCRIPTOR_HH
#define OWNED_FILE_DESCRIPTOR_HH

namespace ndisc
{
    class OwnedFileDescriptor
    {
        int fd_;

    public:
        OwnedFileDescriptor(int file_descriptor) : fd_(file_descriptor) {}

        OwnedFileDescriptor(OwnedFileDescriptor &) = delete;
        OwnedFileDescriptor(OwnedFileDescriptor &&) noexcept;

        OwnedFileDescriptor &operator=(OwnedFileDescriptor &) = delete;
        OwnedFileDescriptor &operator=(OwnedFileDescriptor &&) noexcept;

        ~OwnedFileDescriptor();

        int Get() const { return fd_; }

        const int &operator*() const { return fd_; }

        bool IsValid() const { return fd_ >= 0; }
    };

} // namespace ndisc

#endif // OWNED_FILE_DESCRIPTOR_HH
