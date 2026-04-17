#pragma once

#include <cstddef>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace kestrel {

  class Source {
  public:
      static Source from_path(std::string_view path);

      ~Source();
      Source(Source&& obj) noexcept;
      Source& operator=(Source&& obj) noexcept;
      Source(const Source&) = delete;
      Source& operator=(const Source&) = delete;

      std::span<const char> bytes() const noexcept {
          return {data_, size_};
      }

  private:
      Source() = default;
      void release() noexcept;

      const char*       data_ = nullptr;
      std::size_t       size_ = 0;
  };

  class SourceError : public std::runtime_error {
  public:
      using std::runtime_error::runtime_error;
  };

} // namespace kestrel
