#include "render/shader_preproc.hpp"
#include "render/gl_bindings.hpp"
#include "render/overlay_types.hpp"
#include <fstream>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <cstdio>

namespace ron::render {

bool disk_file_reader(void* user, const char* rel_path, std::string& out) {
    if (!user || !rel_path) return false;
    std::filesystem::path root(static_cast<const char*>(user));
    std::filesystem::path p = root / rel_path;
    std::ifstream file(p, std::ios::binary);
    if (!file.is_open()) return false;
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    // Normalize CRLF to LF
    std::string normalized;
    normalized.reserve(content.size());
    for (size_t i = 0; i < content.size(); ++i) {
        if (content[i] == '\r') {
            if (i + 1 < content.size() && content[i + 1] == '\n') {
                continue;
            }
            normalized.push_back('\n');
        } else {
            normalized.push_back(content[i]);
        }
    }
    out = std::move(normalized);
    return true;
}

static std::string trim(const std::string& s) {
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, (last - first + 1));
}

static std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= s.size()) {
        size_t end = s.find('\n', start);
        if (end == std::string::npos) {
            if (start < s.size()) {
                lines.push_back(s.substr(start));
            }
            break;
        }
        lines.push_back(s.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

static bool parse_include(const std::string& trimmed, std::string& out_path) {
    if (trimmed.empty() || trimmed[0] != '#') return false;
    size_t pos = 1;
    while (pos < trimmed.size() && (trimmed[pos] == ' ' || trimmed[pos] == '\t')) pos++;
    if (trimmed.compare(pos, 7, "include") != 0) return false;
    pos += 7;
    while (pos < trimmed.size() && (trimmed[pos] == ' ' || trimmed[pos] == '\t')) pos++;
    if (pos >= trimmed.size() || trimmed[pos] != '"') return false;
    pos++;
    size_t close_quote = trimmed.find('"', pos);
    if (close_quote == std::string::npos) return false;
    out_path = trimmed.substr(pos, close_quote - pos);
    return true;
}

static bool process_file_lines(
    const std::string& file_path,
    u32 file_idx,
    int depth,
    FileReader file_reader,
    void* user,
    ShaderSource& out,
    std::string& result_text)
{
    if (depth >= 8) {
        out.error = file_path + ": include depth limit exceeded (depth >= 8)";
        return false;
    }

    std::string content;
    if (!file_reader(user, file_path.c_str(), content)) {
        out.error = "cannot open include \"" + file_path + "\"";
        return false;
    }

    std::vector<std::string> lines = split_lines(content);

    for (size_t l = 0; l < lines.size(); ++l) {
        u32 line_num = static_cast<u32>(l + 1);
        std::string trimmed = trim(lines[l]);

        if (trimmed.rfind("#version", 0) == 0) {
            out.error = file_path + ":" + std::to_string(line_num) + ": #version in included file";
            return false;
        }

        std::string inc_path;
        if (parse_include(trimmed, inc_path)) {
            // Check if already included
            auto it = std::find(out.files.begin(), out.files.end(), inc_path);
            if (it != out.files.end()) {
                // Repeated include emits only the "#line <L+1> <I>\n" part
                result_text += "#line " + std::to_string(line_num + 1) + " " + std::to_string(file_idx) + "\n";
            } else {
                u32 inc_idx = static_cast<u32>(out.files.size());
                out.files.push_back(inc_path);

                std::string included_text;
                if (!process_file_lines(inc_path, inc_idx, depth + 1, file_reader, user, out, included_text)) {
                    if (out.error.rfind("cannot open include", 0) == 0) {
                        out.error = file_path + ":" + std::to_string(line_num) + ": " + out.error;
                    }
                    return false;
                }

                result_text += "#line 1 " + std::to_string(inc_idx) + "\n";
                result_text += included_text;
                if (result_text.empty() || result_text.back() != '\n') {
                    result_text.push_back('\n');
                }
                result_text += "#line " + std::to_string(line_num + 1) + " " + std::to_string(file_idx) + "\n";
            }
        } else {
            result_text += lines[l];
            result_text.push_back('\n');
        }
    }

    return true;
}

bool preprocess_shader(
    const char* root_path,
    FileReader file_reader,
    void* user,
    const char* const* defines,
    u32 define_count,
    ShaderSource& out)
{
    out.text.clear();
    out.files.clear();
    out.error.clear();

    if (!root_path || !file_reader) {
        out.error = "invalid root_path or file_reader";
        return false;
    }

    std::string root_content;
    if (!file_reader(user, root_path, root_content)) {
        out.error = std::string(root_path) + ": cannot open root file";
        return false;
    }

    std::vector<std::string> lines = split_lines(root_content);
    if (lines.empty() || trim(lines[0]) != "#version 460 core") {
        out.error = std::string(root_path) + ": missing '#version 460 core' on line 1";
        return false;
    }

    out.files.push_back(root_path);

    std::string output;
    output.reserve(root_content.size() + 2048);

    // Line 1: #version 460 core
    output += "#version 460 core\n";

    // Binding defines
    for (u32 i = 0; i < BINDING_DEFINE_COUNT; ++i) {
        output += "#define ";
        output += BINDING_DEFINES[i].name;
        output += " ";
        output += BINDING_DEFINES[i].value;
        output += "\n";
    }

    // User defines
    if (defines && define_count > 0) {
        for (u32 i = 0; i < define_count; ++i) {
            if (!defines[i]) continue;
            std::string d(defines[i]);
            size_t eq = d.find('=');
            if (eq != std::string::npos) {
                output += "#define " + d.substr(0, eq) + " " + d.substr(eq + 1) + "\n";
            } else {
                output += "#define " + d + " 1\n";
            }
        }
    }

    // Owner palette array built from overlay_palette()
    output += "const vec3 K_OWNER_PALETTE[16] = vec3[16](\n";
    for (int i = 0; i < 16; ++i) {
        Vec4 c = overlay_palette(i);
        char buf[128];
        std::snprintf(buf, sizeof(buf), "    vec3(%.6ff, %.6ff, %.6ff)%s\n", c.x, c.y, c.z, (i < 15 ? "," : ""));
        output += buf;
    }
    output += ");\n";

    output += "#line 2 0\n";

    // Process lines of root file from line 2 (index 1)
    for (size_t l = 1; l < lines.size(); ++l) {
        u32 line_num = static_cast<u32>(l + 1);
        std::string trimmed = trim(lines[l]);

        std::string inc_path;
        if (parse_include(trimmed, inc_path)) {
            auto it = std::find(out.files.begin(), out.files.end(), inc_path);
            if (it != out.files.end()) {
                output += "#line " + std::to_string(line_num + 1) + " 0\n";
            } else {
                u32 inc_idx = static_cast<u32>(out.files.size());
                out.files.push_back(inc_path);

                std::string included_text;
                if (!process_file_lines(inc_path, inc_idx, 1, file_reader, user, out, included_text)) {
                    if (out.error.rfind("cannot open include", 0) == 0) {
                        out.error = std::string(root_path) + ":" + std::to_string(line_num) + ": " + out.error;
                    }
                    return false;
                }

                output += "#line 1 " + std::to_string(inc_idx) + "\n";
                output += included_text;
                if (output.empty() || output.back() != '\n') {
                    output.push_back('\n');
                }
                output += "#line " + std::to_string(line_num + 1) + " 0\n";
            }
        } else {
            output += lines[l];
            output.push_back('\n');
        }
    }

    out.text = std::move(output);
    return true;
}

} // namespace ron::render
