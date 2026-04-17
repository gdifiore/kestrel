#pragma once

#include <stdexcept>
#include <string_view>

#include <imgui.h>

struct GLFWwindow;

namespace kestrel {

  class Window {
  public:
      Window(std::string_view title, int width, int height);
      ~Window();
      Window(const Window&) = delete;
      Window& operator=(const Window&) = delete;
      Window(Window&&) = delete;
      Window& operator=(Window&&) = delete;

      bool should_close() const;
      void begin_frame();
      void end_frame();

  private:
      GLFWwindow* handle_ = nullptr;

      ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
  };

  class WindowError : public std::runtime_error {
  public:
      using std::runtime_error::runtime_error;
  };

} // namespace kestrel
