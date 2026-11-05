#include "kestrel/source.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#if defined(__linux__)
#include <linux/fs.h>
#include <sys/ioctl.h>
#endif
#include <unistd.h>

namespace kestrel
{

    namespace
    {
        [[noreturn]] void throw_errno(const char *what)
        {
            throw SourceError(std::string(what) + ": " + std::strerror(errno));
        }

        class FileDescriptor
        {
        public:
            explicit FileDescriptor(int fd) : fd_(fd) {}
            ~FileDescriptor() { if (fd_ != -1) close(fd_); }
            FileDescriptor(const FileDescriptor &) = delete;
            FileDescriptor &operator=(const FileDescriptor &) = delete;
            int get() const noexcept { return fd_; }

        private:
            int fd_;
        };

        std::size_t checked_size(off_t size)
        {
            if (size < 0 || static_cast<uintmax_t>(size) >
                                std::numeric_limits<std::size_t>::max())
                throw SourceError("file size exceeds address space");
            return static_cast<std::size_t>(size);
        }

        void report_progress(const ProgressCallback &progress, std::size_t completed,
                             std::size_t total)
        {
            if (progress && !progress(completed, total))
                throw LoadCancelled();
        }

        std::size_t copy_snapshot(int input_fd, int snapshot_fd, std::size_t size,
                                  const ProgressCallback &progress)
        {
            constexpr std::size_t CHUNK = 1024 * 1024;
            std::vector<char> buffer(std::min(size, CHUNK));
            std::size_t captured = 0;
            while (captured < size)
            {
                const ssize_t count = pread(input_fd, buffer.data(),
                                             std::min(buffer.size(), size - captured),
                                             static_cast<off_t>(captured));
                if (count == -1 && errno == EINTR)
                    continue;
                if (count == -1)
                    throw_errno("pread source");
                // A concurrent truncate can shorten the snapshot. Appends are
                // deliberately bounded by the initial size to ensure progress.
                if (count == 0)
                    break;
                std::size_t written = 0;
                while (written < static_cast<std::size_t>(count))
                {
                    const ssize_t n = write(snapshot_fd, buffer.data() + written,
                                              static_cast<std::size_t>(count) - written);
                    if (n == -1 && errno == EINTR)
                        continue;
                    if (n == -1)
                        throw_errno("write snapshot");
                    if (n == 0)
                        throw SourceError("write snapshot: no progress");
                    written += static_cast<std::size_t>(n);
                }
                captured += static_cast<std::size_t>(count);
                report_progress(progress, captured, size);
            }
            return captured;
        }
    }

    Source Source::from_path(std::string_view path, ProgressCallback progress)
    {
        Source s;
        s.path_ = std::string(path);

        const FileDescriptor input(open(s.path_.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK));
        const int fd = input.get();
        if (fd == -1)
            throw_errno("open");

        struct stat sb;
        if (fstat(fd, &sb) == -1)
            throw_errno("fstat");
        if (!S_ISREG(sb.st_mode))
            throw SourceError("source is not a regular file");
        const std::size_t initial_size = checked_size(sb.st_size);
        report_progress(progress, 0, initial_size);
        if (initial_size == 0)
            return s;

        // Mapping the live inode is unsafe even with MAP_PRIVATE: truncation
        // can SIGBUS readers, and in-place rewrites can invalidate line offsets.
        // Keep an unlinked, privately owned file-backed copy instead. Its pages
        // remain reclaimable, so large files need not fit in resident memory.
        // A tmpfs system temp directory consumes RAM/swap rather than disk.
        const auto close_file = [](FILE *file) { std::fclose(file); };
        std::unique_ptr<FILE, decltype(close_file)> snapshot(std::tmpfile(), close_file);
        if (!snapshot)
            throw_errno("tmpfile");
        const int snapshot_fd = fileno(snapshot.get());

        std::size_t captured = 0;
#if defined(__linux__)
        // A reflink is both fast and atomic with respect to source writes where
        // the filesystem supports it. Unsupported/cross-device clones fall back
        // to bounded copying without mapping the mutable source.
        if (ioctl(snapshot_fd, FICLONE, fd) == 0)
        {
            if (fstat(snapshot_fd, &sb) == -1)
                throw_errno("fstat snapshot");
            captured = checked_size(sb.st_size);
        }
        else
#endif
        {
            captured = copy_snapshot(fd, snapshot_fd, initial_size, progress);
        }
        report_progress(progress, captured, captured);
        if (captured == 0)
            return s;

        void *p = mmap(nullptr, captured, PROT_READ, MAP_PRIVATE, snapshot_fd, 0);
        if (p == MAP_FAILED)
            throw_errno("mmap snapshot");
        // Closing the unlinked snapshot preserves its immutable mapping until
        // the last Source owning it is released.
        s.data_ = static_cast<const char *>(p);
        s.size_ = captured;
        return s;
    }

    Source::~Source()
    {
        release();
    }

    Source::Source(Source &&obj) noexcept
        : data_(obj.data_), size_(obj.size_), path_(std::move(obj.path_))
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
            path_ = std::move(obj.path_);

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
