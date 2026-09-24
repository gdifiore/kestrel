# Third-party notices

Kestrel is distributed under the MIT License in [`LICENSE`](LICENSE). This
file records components that are not covered by that license. Their original
license terms continue to apply to those components, and are installed with
Kestrel under `share/doc/kestrel/third-party/`.

| Component | Use in Kestrel | License | License text |
| --- | --- | --- | --- |
| Dear ImGui | User interface | MIT | [`third_party/imgui/LICENSE.txt`](third_party/imgui/LICENSE.txt) |
| ImGuiFileDialog | File chooser | MIT | [`third_party/ImGuiFileDialog/LICENSE`](third_party/ImGuiFileDialog/LICENSE) |
| stb | Image/file-dialog support | MIT or public domain | [`third_party/ImGuiFileDialog/stb/LICENSE`](third_party/ImGuiFileDialog/stb/LICENSE) |
| dirent | File-dialog support | MIT | [`third_party/ImGuiFileDialog/dirent/LICENSE`](third_party/ImGuiFileDialog/dirent/LICENSE) |
| GLFW | Windowing and input | zlib/libpng-style license | [`third_party/glfw/LICENSE.md`](third_party/glfw/LICENSE.md) |
| PCRE2 | Capture-group matching | BSD license, with PCRE2 binary redistribution exemption | [`third_party/pcre2/LICENCE`](third_party/pcre2/LICENCE) |
| Vectorscan / Hyperscan | Regular-expression scanning | BSD license, with additional included notices | [`third_party/vectorscan/LICENSE`](third_party/vectorscan/LICENSE) |
| doctest | Test framework | MIT | [`third_party/doctest/LICENSE.txt`](third_party/doctest/LICENSE.txt) |
| spdlog | Logging | MIT | Downloaded at configure time; its license is installed with Kestrel |
| fmt | Formatting library bundled by spdlog | MIT, with fmt's object-code exception | Downloaded with spdlog; its license is installed with Kestrel |
| JetBrains Mono | Bundled application font | SIL Open Font License 1.1 | Downloaded at configure time; `OFL.txt` is installed with Kestrel |

The spdlog version is pinned in `CMakeLists.txt`. Kestrel uses spdlog's
bundled fmt copy. The JetBrains Mono font is a separate font asset: it is not
covered by Kestrel's MIT License and must remain under the SIL Open Font
License 1.1.

The Kestrel icon is a modified asset credited in the README to Delwar018 via
Flaticon. That attribution is retained separately from the software license.
