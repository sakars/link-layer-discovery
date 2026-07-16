#include "owned_file_descriptor.hh"

#include <unistd.h>

namespace ndisc
{

    OwnedFileDescriptor::OwnedFileDescriptor(OwnedFileDescriptor &&other) noexcept : fd_(other.fd_)
    {
        other.fd_ = -1;
    }

    OwnedFileDescriptor &OwnedFileDescriptor::operator=(OwnedFileDescriptor &&other) noexcept
    {
        if (fd_ >= 0)
        {
            close(fd_);
        }
        fd_ = other.fd_;
        other.fd_ = -1;
        return *this;
    }

    OwnedFileDescriptor::~OwnedFileDescriptor()
    {
        if (fd_ >= 0)
        {
            close(fd_);
        }
    }

} // namespace ndisc
