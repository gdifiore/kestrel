#pragma once

#include <functional>
#include <span>
#include <stdexcept>
#include <string_view>

#include <imgui.h>

struct GLFWwindow;

namespace kestrel
{

    class Window
    {
    public:
        Window(std::string_view title, int width, int height);
        ~Window();
        Window(const Window &) = delete;
        Window &operator=(const Window &) = delete;
        Window(Window &&) = delete;
        Window &operator=(Window &&) = delete;

        bool should_close() const;
        void begin_frame();
        void end_frame();
        void set_title(std::string_view title);

        void dispatch_drop(int count, const char **paths);
        void on_file_drop(std::function<void(std::span<const char *>)>);

    private:
        GLFWwindow *handle_ = nullptr;

        ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

        std::function<void(std::span<const char *>)> drop_cb_;
    };

    class WindowError : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

} // namespace kestrel
