#pragma once

#include "core/types.hpp"
#include "render/shader_preproc.hpp"
#include <string>

namespace ron::render {

struct ShaderDesc {
    const char* name;
    const char* vs;
    const char* fs;
    const char* cs;
    const char* defines[8];
    u32 define_count;
};

class ShaderProgram {
public:
    ShaderProgram() = default;
    ~ShaderProgram() { destroy(); }

    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;

    ShaderProgram(ShaderProgram&& o) noexcept : m_id(o.m_id) { o.m_id = 0; }
    ShaderProgram& operator=(ShaderProgram&& o) noexcept {
        if (this != &o) {
            destroy();
            m_id = o.m_id;
            o.m_id = 0;
        }
        return *this;
    }

    u32 id() const { return m_id; }
    bool valid() const { return m_id != 0; }
    void destroy();
    void set_id(u32 id) { m_id = id; }

private:
    u32 m_id = 0;
};

bool shader_build(const ShaderDesc& desc, FileReader reader, void* user, ShaderProgram& inout, std::string& log);

} // namespace ron::render
