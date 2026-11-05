#pragma once

#include "kestrel/load_progress.hpp"

#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace kestrel
{

    class Source
    {
    public:
        // Captures a stable, privately owned file snapshot. The temporary
        // backing file may require space up to the input size when reflinks
        // are unavailable, using RAM/swap if the system temp directory is tmpfs.
        // Concurrent rewrites during copying are not an atomic snapshot.
        static Source from_path(std::string_view path, ProgressCallback progress = {});

        ~Source();
        Source(Source &&obj) noexcept;
        Source &operator=(Source &&obj) noexcept;
        Source(const Source &) = delete;
        Source &operator=(const Source &) = delete;

        std::span<const char> bytes() const noexcept
        {
            return {data_, size_};
        }

        const std::string &path() const noexcept { return path_; }

    private:
        Source() = default;
        void release() noexcept;

        const char *data_ = nullptr;
        std::size_t size_ = 0;
        std::string path_;
    };

    class SourceError : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

} // namespace kestrel
