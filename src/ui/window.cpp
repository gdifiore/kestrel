#include "kestrel/window.hpp"

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

    Window::Window(std::string_view title, int width, int height)
    {
        if (!glfwInit()) {
            throw WindowError("glfwInit failed");
}

        try
        {
            float main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor()); // Valid on GLFW 3.3+ only

            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

            const int scaled_w = (int)(width * main_scale);
            const int scaled_h = (int)(height * main_scale);
            handle_ = glfwCreateWindow(scaled_w, scaled_h, std::string(title).c_str(), nullptr, nullptr);
            if (handle_ == nullptr)
            {
                glfwTerminate();
                throw WindowError("glfwCreateWindow failed");
            }

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
            io.Fonts->AddFontFromFileTTF(KESTREL_FONT_REGULAR, 15.0F * main_scale);
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

} // namespace kestrel
