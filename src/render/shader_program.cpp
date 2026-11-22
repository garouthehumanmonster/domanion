#include "render/shader_program.hpp"
#include <glad/gl.h>
#include <vector>

namespace ron::render {

void ShaderProgram::destroy() {
    if (m_id != 0) {
        glDeleteProgram(m_id);
        m_id = 0;
    }
}

static bool compile_stage(
    GLenum stage,
    const char* stage_name,
    const char* path,
    const ShaderDesc& desc,
    FileReader reader,
    void* user,
    GLuint& out_shader,
    std::vector<std::string>& all_files,
    std::string& log)
{
    ShaderSource src;
    if (!preprocess_shader(path, reader, user, desc.defines, desc.define_count, src)) {
        log += "Preprocessor error in " + std::string(desc.name) + " (" + stage_name + "):\n";
        log += src.error + "\nFiles:\n";
        for (size_t i = 0; i < src.files.size(); ++i) {
            log += "[" + std::to_string(i) + "]=" + src.files[i] + " ";
        }
        log += "\n";
        return false;
    }

    for (const auto& f : src.files) {
        all_files.push_back(f);
    }

    GLuint shader = glCreateShader(stage);
    const char* code_str = src.text.c_str();
    GLint code_len = static_cast<GLint>(src.text.size());
    glShaderSource(shader, 1, &code_str, &code_len);
    glCompileShader(shader);

    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        GLint info_len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_len);
        std::string info_log;
        if (info_len > 1) {
            info_log.resize(info_len);
            glGetShaderInfoLog(shader, info_len, nullptr, info_log.data());
        }

        log += "Shader compile error in " + std::string(desc.name) + " (" + stage_name + "):\n";
        log += info_log + "\nFiles:\n";
        for (size_t i = 0; i < src.files.size(); ++i) {
            log += "[" + std::to_string(i) + "]=" + src.files[i] + " ";
        }
        log += "\n";

        glDeleteShader(shader);
        return false;
    }

    out_shader = shader;
    return true;
}

bool shader_build(const ShaderDesc& desc, FileReader reader, void* user, ShaderProgram& inout, std::string& log) {
    std::vector<std::string> all_files;
    GLuint vs = 0;
    GLuint fs = 0;
    GLuint cs = 0;

    if (desc.cs) {
        if (!compile_stage(GL_COMPUTE_SHADER, "compute", desc.cs, desc, reader, user, cs, all_files, log)) {
            return false;
        }
    } else {
        if (!desc.vs || !desc.fs) {
            log += "Shader descriptor error: missing vs or fs\n";
            return false;
        }
        if (!compile_stage(GL_VERTEX_SHADER, "vertex", desc.vs, desc, reader, user, vs, all_files, log)) {
            return false;
        }
        if (!compile_stage(GL_FRAGMENT_SHADER, "fragment", desc.fs, desc, reader, user, fs, all_files, log)) {
            glDeleteShader(vs);
            return false;
        }
    }

    GLuint program = glCreateProgram();
    if (cs != 0) {
        glAttachShader(program, cs);
    } else {
        glAttachShader(program, vs);
        glAttachShader(program, fs);
    }

    glLinkProgram(program);

    GLint link_status = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &link_status);

    if (cs != 0) {
        glDetachShader(program, cs);
        glDeleteShader(cs);
    } else {
        glDetachShader(program, vs);
        glDeleteShader(vs);
        glDetachShader(program, fs);
        glDeleteShader(fs);
    }

    if (link_status != GL_TRUE) {
        GLint info_len = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &info_len);
        std::string info_log;
        if (info_len > 1) {
            info_log.resize(info_len);
            glGetProgramInfoLog(program, info_len, nullptr, info_log.data());
        }

        log += "Program link error in " + std::string(desc.name) + ":\n";
        log += info_log + "\nFiles:\n";
        for (size_t i = 0; i < all_files.size(); ++i) {
            log += "[" + std::to_string(i) + "]=" + all_files[i] + " ";
        }
        log += "\n";

        glDeleteProgram(program);
        return false;
    }

    inout.destroy();
    inout.set_id(program);
    return true;
}

} // namespace ron::render
