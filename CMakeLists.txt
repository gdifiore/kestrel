cmake_minimum_required(VERSION 3.20)
project(kestrel CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# ---- GLFW ----
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/glfw EXCLUDE_FROM_ALL)

# ---- OpenGL ----
find_package(OpenGL REQUIRED)

# ---- Dear ImGui (no CMake upstream; build inline) ----
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

# ---- ImGuiFileDialog ----
set(IFD_DIR ${CMAKE_SOURCE_DIR}/third_party/ImGuiFileDialog)
add_library(imguifiledialog STATIC ${IFD_DIR}/ImGuiFileDialog.cpp)
target_include_directories(imguifiledialog PUBLIC ${IFD_DIR})
target_link_libraries(imguifiledialog PUBLIC imgui)

# ---- JetBrains Mono (fetched zip, extracted into build dir) ----
include(FetchContent)
FetchContent_Declare(jetbrains_mono
    URL      https://github.com/JetBrains/JetBrainsMono/releases/download/v2.304/JetBrainsMono-2.304.zip
    URL_HASH SHA256=6f6376c6ed2960ea8a963cd7387ec9d76e3f629125bc33d1fdcd7eb7012f7bbf
)
FetchContent_MakeAvailable(jetbrains_mono)
set(KESTREL_FONT_DIR "${jetbrains_mono_SOURCE_DIR}/fonts/ttf" CACHE PATH "" FORCE)

# ---- Vectorscan ----
option(KESTREL_VENDOR_VECTORSCAN "Build vectorscan from third_party" ON)
if(KESTREL_VENDOR_VECTORSCAN)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
    # vectorscan uses -Werror upstream; newer clang flags unused-but-set vars.
    add_compile_options(
        -Wno-error=unused-but-set-variable
        -Wno-error=deprecated-declarations
    )
    add_subdirectory(third_party/vectorscan EXCLUDE_FROM_ALL)
    set(VECTORSCAN_TARGET hs)
else()
    find_package(Vectorscan REQUIRED)
    set(VECTORSCAN_TARGET Vectorscan::hs)
endif()

# ---- App ----
add_executable(kestrel
    src/main.cpp
    src/app.cpp
    src/util.cpp
    src/engine/scanner.cpp
    src/engine/source.cpp
    src/engine/line_index.cpp
    src/ui/window.cpp
    src/ui/ui.cpp
)
target_include_directories(kestrel PRIVATE
    include
    third_party/vectorscan/src
    ${CMAKE_BINARY_DIR}/third_party/vectorscan
)
target_link_libraries(kestrel PRIVATE imgui imguifiledialog ${VECTORSCAN_TARGET})
target_compile_definitions(kestrel PRIVATE
    KESTREL_FONT_REGULAR="${KESTREL_FONT_DIR}/JetBrainsMono-Regular.ttf"
)
target_compile_options(kestrel PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Release>:-O3 -march=native>
)

enable_testing()
add_subdirectory(tests)
