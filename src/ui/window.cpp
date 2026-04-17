#include "kestrel/window.hpp"

#include <string>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

namespace kestrel
{

    Window::Window(std::string_view title, int width, int height)
    {
        if (!glfwInit())
            throw WindowError("glfwInit failed");

        try
        {
            float main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor()); // Valid on GLFW 3.3+ only

            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

            handle_ = glfwCreateWindow((int)(width * main_scale), (int)(height * main_scale), std::string(title).c_str(), nullptr, nullptr);
            if (handle_ == nullptr)
            {
                glfwTerminate();
                throw WindowError("glfwCreateWindow failed");
            }
            glfwMakeContextCurrent(handle_);
            glfwSwapInterval(1); // Enable vsync

            ImGui::CreateContext();

            // configurable later
            ImGui::StyleColorsDark();
            // ImGui::StyleColorsLight();

            ImGui_ImplGlfw_InitForOpenGL(handle_, true);

            ImGui_ImplOpenGL3_Init(nullptr);

            glfwSetWindowUserPointer(handle_, this);
        }
        catch (...)
        {
            if (handle_)
                glfwDestroyWindow(handle_);
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

    void Window::dispatch_drop(int count, const char **paths)
    {
        if (drop_cb_)
            drop_cb_(std::span<const char *>{paths, static_cast<size_t>(count)});
    }

    static void drop_trampoline(GLFWwindow *w, int count, const char **paths)
    {
        auto *self = static_cast<Window *>(glfwGetWindowUserPointer(w));
        if (self)
            self->dispatch_drop(count, paths);
    }

    void Window::on_file_drop(std::function<void(std::span<const char *>)> cb)
    {
        drop_cb_ = std::move(cb);
        glfwSetDropCallback(handle_, drop_cb_ ? &drop_trampoline : nullptr);
    }

} // namespace kestrel
