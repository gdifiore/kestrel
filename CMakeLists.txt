cmake_minimum_required(VERSION 3.20)
project(kestrel CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable ASAN for Debug builds (applies to all targets including vectorscan)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer -g")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address")
endif()

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
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(jetbrains_mono)
set(KESTREL_FONT_DIR "${jetbrains_mono_SOURCE_DIR}/fonts/ttf" CACHE PATH "" FORCE)

# ---- spdlog ----
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.15.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(spdlog)

# ---- Vectorscan ----
option(KESTREL_VENDOR_VECTORSCAN "Build vectorscan from third_party" ON)
if(KESTREL_VENDOR_VECTORSCAN)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
    # Fix platform detection issues in vectorscan develop
    set(FAT_RUNTIME OFF CACHE BOOL "" FORCE)
    # vectorscan uses -Werror upstream; newer clang flags unused-but-set vars.
    add_compile_options(
        -Wno-error=unused-but-set-variable
        -Wno-error=deprecated-declarations
    )
    # Disable vectorscan debug prints by undefining DEBUG
    add_compile_options(-UDEBUG)
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
    src/config.cpp
    src/search.cpp
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
target_link_libraries(kestrel PRIVATE imgui imguifiledialog ${VECTORSCAN_TARGET} spdlog::spdlog)
target_compile_definitions(kestrel PRIVATE
    KESTREL_FONT_REGULAR="${KESTREL_FONT_DIR}/JetBrainsMono-Regular.ttf"
    IMGUI_DISABLE_DEBUG_TOOLS
)
target_compile_options(kestrel PRIVATE
    -Wall -Wextra -Wpedantic
    -mavx2
    $<$<CONFIG:Release>:-O3 -march=native>
)

enable_testing()
add_subdirectory(tests)
