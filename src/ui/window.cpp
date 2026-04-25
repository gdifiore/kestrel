#include "kestrel/window.hpp"
#include "kestrel/ui.hpp"

#include <algorithm>
#include <string>
#include <filesystem>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace kestrel
{

    static float derive_ui_scale(GLFWmonitor *mon)
    {
        const float dpi_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(mon);
        // Trust DPI scale when the OS reports anything beyond 1.0 — Windows /
        // macOS / Wayland set this. X11 frequently reports 1.0 on 1440p/4K,
        // leaving fonts at ~15px on a high-pixel-density monitor; fall back to
        // a resolution heuristic so the UI is legible out of the box.
        if (dpi_scale > 1.05F) {
            return dpi_scale;
        }
        const GLFWvidmode *mode = mon ? glfwGetVideoMode(mon) : nullptr;
        if (!mode) {
            return dpi_scale;
        }
        if (mode->width >= 3840) {
            return 2.0F;
        }
        if (mode->width >= 2560) {
            return 1.5F;
        }
        return dpi_scale;
    }

    // Pick the monitor whose work area contains the largest portion of the
    // window's rect. Falls back to primary monitor when the window is
    // off-screen or no monitors overlap.
    static GLFWmonitor *monitor_for_window(GLFWwindow *win)
    {
        int wx, wy, ww, wh;
        glfwGetWindowPos(win, &wx, &wy);
        glfwGetWindowSize(win, &ww, &wh);

        int count = 0;
        GLFWmonitor **mons = glfwGetMonitors(&count);
        GLFWmonitor *best = glfwGetPrimaryMonitor();
        int best_overlap = 0;
        for (int i = 0; i < count; i++)
        {
            int mx, my, mw, mh;
            glfwGetMonitorWorkarea(mons[i], &mx, &my, &mw, &mh);
            const int ox = std::max(0, std::min(wx + ww, mx + mw) - std::max(wx, mx));
            const int oy = std::max(0, std::min(wy + wh, my + mh) - std::max(wy, my));
            const int overlap = ox * oy;
            if (overlap > best_overlap)
            {
                best_overlap = overlap;
                best = mons[i];
            }
        }
        return best;
    }

    Window::Window(std::string_view title, int width, int height)
    {
        if (!glfwInit()) {
            throw WindowError("glfwInit failed");
}

        try
        {
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

            // Create the window first at the unscaled size so the windowing
            // system can place it; then pick the monitor it actually landed
            // on and rescale. Avoids scaling against the primary monitor when
            // the window opens on a secondary display with different DPI.
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            handle_ = glfwCreateWindow(width, height, std::string(title).c_str(), nullptr, nullptr);
            if (handle_ == nullptr)
            {
                glfwTerminate();
                throw WindowError("glfwCreateWindow failed");
            }

            const float main_scale = derive_ui_scale(monitor_for_window(handle_));
            const int scaled_w = (int)(width * main_scale);
            const int scaled_h = (int)(height * main_scale);
            glfwSetWindowSize(handle_, scaled_w, scaled_h);
            glfwShowWindow(handle_);

            const char* icon_path = "assets/kestrel-icon-16.png";
            if (std::filesystem::exists(icon_path))
            {
                int width, height, channels;
                unsigned char* pixels = stbi_load(icon_path, &width, &height, &channels, 4); // Force RGBA
                if (pixels)
                {
                    GLFWimage icon;
                    icon.width = width;
                    icon.height = height;
                    icon.pixels = pixels;
                    glfwSetWindowIcon(handle_, 1, &icon);
                    stbi_image_free(pixels);
                }
            }

            glfwSetWindowSizeLimits(handle_, scaled_w, scaled_h, GLFW_DONT_CARE, GLFW_DONT_CARE);
            glfwMakeContextCurrent(handle_);
            glfwSwapInterval(1); // Enable vsync

            ImGui::CreateContext();

#ifdef KESTREL_FONT_REGULAR
            ImGuiIO &io = ImGui::GetIO();
            io.Fonts->AddFontFromFileTTF(KESTREL_FONT_REGULAR, BASE_FONT_PX * main_scale);
#endif

            // configurable later
            ImGui::StyleColorsDark();

            ImGui_ImplGlfw_InitForOpenGL(handle_, true);

            ImGui_ImplOpenGL3_Init(nullptr);

            glfwSetWindowUserPointer(handle_, this);
        }
        catch (...)
        {
            if (handle_) {
                glfwDestroyWindow(handle_);
}
            glfwTerminate();
            throw;
        }
    }

    Window::~Window()
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        glfwDestroyWindow(handle_);
        glfwTerminate();
    }

    bool Window::should_close() const
    {
        return glfwWindowShouldClose(handle_) != 0;
    }

    void Window::begin_frame()
    {
        glfwPollEvents();
        if (glfwGetWindowAttrib(handle_, GLFW_ICONIFIED) != 0)
        {
            ImGui_ImplGlfw_Sleep(10);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void Window::end_frame()
    {
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(handle_, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(handle_);
    }

    void Window::set_title(std::string_view title)
    {
        glfwSetWindowTitle(handle_, std::string(title).c_str());
    }

    void Window::dispatch_drop(int count, const char **paths)
    {
        if (drop_cb_) {
            drop_cb_(std::span<const char *>{paths, static_cast<size_t>(count)});
}
    }

    static void drop_trampoline(GLFWwindow *w, int count, const char **paths)
    {
        auto *self = static_cast<Window *>(glfwGetWindowUserPointer(w));
        if (self) {
            self->dispatch_drop(count, paths);
}
    }

    void Window::on_file_drop(std::function<void(std::span<const char *>)> cb)
    {
        drop_cb_ = std::move(cb);
        glfwSetDropCallback(handle_, drop_cb_ ? &drop_trampoline : nullptr);
    }

    void Window::dispatch_refresh()
    {
        if (refresh_cb_) {
            refresh_cb_();
}
    }

    static void refresh_trampoline(GLFWwindow *w)
    {
        auto *self = static_cast<Window *>(glfwGetWindowUserPointer(w));
        if (self) {
            self->dispatch_refresh();
}
    }

    void Window::on_refresh(std::function<void()> cb)
    {
        refresh_cb_ = std::move(cb);
        glfwSetWindowRefreshCallback(handle_, refresh_cb_ ? &refresh_trampoline : nullptr);
    }

} // namespace kestrel
