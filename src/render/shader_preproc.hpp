#pragma once

#include "core/types.hpp"
#include <string>
#include <vector>

namespace ron::render {

using FileReader = bool(*)(void* user, const char* rel_path, std::string& out);

bool disk_file_reader(void* user, const char* rel_path, std::string& out);

struct ShaderSource {
    std::string text;
    std::vector<std::string> files;
    std::string error;
};

bool preprocess_shader(const char* root_path,
                       FileReader file_reader,
                       void* user,
                       const char* const* defines,
                       u32 define_count,
                       ShaderSource& out);

} // namespace ron::render
