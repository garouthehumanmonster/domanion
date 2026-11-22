#include "render/gl_bindings.hpp"

namespace ron::render {

#define RON_GL_BINDING_ENTRY(name, c_val, c_type, str_val) \
    BindingDefine{ #name, str_val },

const BindingDefine BINDING_DEFINES[] = {
    RON_GL_BINDINGS(RON_GL_BINDING_ENTRY)
};
#undef RON_GL_BINDING_ENTRY

const u32 BINDING_DEFINE_COUNT = static_cast<u32>(sizeof(BINDING_DEFINES) / sizeof(BINDING_DEFINES[0]));

} // namespace ron::render
