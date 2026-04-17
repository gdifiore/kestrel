#include "kestrel/app.hpp"
#include "kestrel/window.hpp"

namespace kestrel
{

    int run_app(int /*argc*/, char ** /*argv*/)
    {
        Window w("kestrel", 1280, 800);

        while (!w.should_close())
        {
            w.begin_frame();
            if (ImGui::BeginMainMenuBar())
            {
                if (ImGui::BeginMenu("File"))
                {
                    if (ImGui::MenuItem("Open...", "Ctrl+O"))
                    { /* open */
                    }
                    if (ImGui::MenuItem("Close"))
                    { /* close */
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Quit", "Ctrl+Q"))
                    { /* quit */
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("View"))
                { /* ... */
                    ImGui::EndMenu();
                }
                ImGui::EndMainMenuBar();
            }
            w.end_frame();
        }

        return 0;
    }

} // namespace kestrel
