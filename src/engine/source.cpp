#include "kestrel/source.hpp"

#include <string>
#include <string_view>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace kestrel
{

    namespace
    {
        [[noreturn]] void throw_errno(const char *what)
        {
            throw SourceError(std::string(what) + ": " + std::strerror(errno));
        }
    }

    Source Source::from_path(std::string_view path)
    {
        Source s;

        int fd = open(std::string(path).c_str(), O_RDONLY);
        if (fd == -1)
            throw_errno("open");

        struct stat sb;
        if (fstat(fd, &sb) == -1)
        {
            int err = errno;
            close(fd);
            errno = err;
            throw_errno("fstat");
        }

        if (sb.st_size == 0)
        {
            close(fd);
            return s; // valid empty Source
        }

        void *p = mmap(nullptr, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
        if (p == MAP_FAILED)
            throw_errno("mmap");

        madvise(p, sb.st_size, MADV_SEQUENTIAL);

        s.data_ = static_cast<const char *>(p);
        s.size_ = static_cast<std::size_t>(sb.st_size);
        return s;
    }

    Source::~Source()
    {
        release();
    }

    Source::Source(Source &&obj) noexcept
        : data_(obj.data_), size_(obj.size_)
    {
        obj.data_ = nullptr;
        obj.size_ = 0;
    }

    Source &Source::operator=(Source &&obj) noexcept
    {
        if (this != &obj)
        {
            release();
            data_ = obj.data_;
            size_ = obj.size_;

            obj.data_ = nullptr;
            obj.size_ = 0;
        }

        return *this;
    }

    void Source::release() noexcept
    {
        if (data_)
        {
            munmap(const_cast<char *>(data_), size_);
        }
        data_ = nullptr;
        size_ = 0;
    }
} // namespace kestrel
