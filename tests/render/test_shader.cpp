#include "render/gl_fixture.hpp"
#include "render/shader_preproc.hpp"
#include "render/shader_program.hpp"
#include "render/shader_library.hpp"
#include "core/assert_log.hpp"
#include <unordered_map>
#include <algorithm>
#include <cctype>

namespace ron::render {

static const char* get_shaders_dir() {
    static const char* paths[] = {
        RON_SOURCE_DIR "/assets/shaders",
        "assets/shaders",
        "../assets/shaders",
        "../../assets/shaders"
    };
    for (const char* p : paths) {
        std::string test_file = std::string(p) + "/common/frame.glsl";
        FILE* f = fopen(test_file.c_str(), "rb");
        if (f) {
            fclose(f);
            return p;
        }
    }
    return paths[0];
}

static size_t count_occurrences(const std::string& str, const std::string& sub) {
    if (sub.empty()) return 0;
    size_t count = 0;
    size_t pos = 0;
    while ((pos = str.find(sub, pos)) != std::string::npos) {
        ++count;
        pos += sub.length();
    }
    return count;
}

static bool contains_case_insensitive(const std::string& str, const std::string& sub) {
    auto it = std::search(
        str.begin(), str.end(),
        sub.begin(), sub.end(),
        [](char ch1, char ch2) { return std::tolower(static_cast<unsigned char>(ch1)) == std::tolower(static_cast<unsigned char>(ch2)); }
    );
    return it != str.end();
}

static bool mem_reader(void* user, const char* rel_path, std::string& out) {
    auto* map = static_cast<const std::unordered_map<std::string, std::string>*>(user);
    if (!map || !rel_path) return false;
    auto it = map->find(rel_path);
    if (it != map->end()) {
        out = it->second;
        return true;
    }
    return false;
}

RON_TEST(Shader_Preproc) {
    std::unordered_map<std::string, std::string> files = {
        { "r.glsl", "#version 460 core\n#include \"a.glsl\"\nvoid main(){}\n" },
        { "a.glsl", "float a;\n#include \"b.glsl\"\n#include \"b.glsl\"\n" },
        { "b.glsl", "float b;\n" }
    };

    const char* defines[] = { "PROBE_Z=0.25", "FLAG" };
    ShaderSource src;
    bool ok = preprocess_shader("r.glsl", mem_reader, &files, defines, 2, src);
    RON_CHECK(ok);

    // Assert text starts with "#version 460 core\n"
    RON_CHECK(src.text.rfind("#version 460 core\n", 0) == 0);

    // Contains binding defines and user defines
    RON_CHECK(src.text.find("#define BIND_UBO_FRAME 0") != std::string::npos);
    RON_CHECK(src.text.find("#define RON_REVERSE_Z 1") != std::string::npos);
    RON_CHECK(src.text.find("#define PROBE_Z 0.25") != std::string::npos);
    RON_CHECK(src.text.find("#define FLAG 1") != std::string::npos);

    // Substring "float b;" occurs exactly once
    RON_CHECK_EQ(count_occurrences(src.text, "float b;"), 1u);

    // "#line 1 1\n" and "#line 1 2\n" each occur exactly once
    RON_CHECK_EQ(count_occurrences(src.text, "#line 1 1\n"), 1u);
    RON_CHECK_EQ(count_occurrences(src.text, "#line 1 2\n"), 1u);

    // "#line 3 0\n" occurs
    RON_CHECK(src.text.find("#line 3 0\n") != std::string::npos);

    // "#line 3 1\n" occurs once after first include of b.glsl, "#line 4 1\n" occurs once
    RON_CHECK_EQ(count_occurrences(src.text, "#line 3 1\n"), 1u);
    RON_CHECK_EQ(count_occurrences(src.text, "#line 4 1\n"), 1u);

    // files == {"r.glsl", "a.glsl", "b.glsl"}
    RON_CHECK_EQ(src.files.size(), 3u);
    RON_CHECK(src.files[0] == "r.glsl");
    RON_CHECK(src.files[1] == "a.glsl");
    RON_CHECK(src.files[2] == "b.glsl");

    // Order in text: "#define FLAG 1" before "#line 2 0"
    size_t flag_pos = src.text.find("#define FLAG 1");
    size_t line2_pos = src.text.find("#line 2 0");
    RON_CHECK(flag_pos != std::string::npos);
    RON_CHECK(line2_pos != std::string::npos);
    RON_CHECK(flag_pos < line2_pos);

    // Error case 1: missing include "nope.glsl" at root line 2
    std::unordered_map<std::string, std::string> err1_files = {
        { "r.glsl", "#version 460 core\n#include \"nope.glsl\"\nvoid main(){}\n" }
    };
    ShaderSource err1_src;
    bool err1_ok = preprocess_shader("r.glsl", mem_reader, &err1_files, nullptr, 0, err1_src);
    RON_CHECK(!err1_ok);
    RON_CHECK(err1_src.error.find("nope.glsl") != std::string::npos);
    RON_CHECK(err1_src.error.find("r.glsl:2") != std::string::npos);

    // Error case 2: root without #version
    std::unordered_map<std::string, std::string> err2_files = {
        { "r.glsl", "void main(){}\n" }
    };
    ShaderSource err2_src;
    bool err2_ok = preprocess_shader("r.glsl", mem_reader, &err2_files, nullptr, 0, err2_src);
    RON_CHECK(!err2_ok);
    RON_CHECK(err2_src.error.find("#version") != std::string::npos);

    // Error case 3: a file chain deeper than 8 distinct files
    std::unordered_map<std::string, std::string> err3_files;
    err3_files["f0.glsl"] = "#version 460 core\n#include \"f1.glsl\"\n";
    for (int i = 1; i <= 9; ++i) {
        std::string cur = "f" + std::to_string(i) + ".glsl";
        std::string nxt = "f" + std::to_string(i + 1) + ".glsl";
        if (i < 9) {
            err3_files[cur] = "#include \"" + nxt + "\"\n";
        } else {
            err3_files[cur] = "float x;\n";
        }
    }
    ShaderSource err3_src;
    bool err3_ok = preprocess_shader("f0.glsl", mem_reader, &err3_files, nullptr, 0, err3_src);
    RON_CHECK(!err3_ok);
    RON_CHECK(contains_case_insensitive(err3_src.error, "depth"));
}

RON_TEST(Shader_Programs) {
    auto& gl = ::ron::render::test::test_gl();
    (void)gl;
    GL_TEST_BEGIN();

    const char* shaders_dir = get_shaders_dir();
    ShaderLibrary lib;
    std::string init_log;
    bool ok = lib.init(disk_file_reader, const_cast<char*>(shaders_dir), init_log);
    if (!ok) {
        RON_LOG_ERROR("ShaderLibrary::init failed: %s", init_log.c_str());
    }
    RON_CHECK(ok);
    RON_CHECK(!contains_case_insensitive(init_log, "error"));

    for (u32 i = 0; i < static_cast<u32>(ProgramId::COUNT); ++i) {
        u32 id = lib.program(static_cast<ProgramId>(i));
        RON_CHECK(id != 0);
        GLint status = 0;
        glGetProgramiv(id, GL_LINK_STATUS, &status);
        RON_CHECK_EQ(status, GL_TRUE);
    }

    u32 ring_id = lib.program(ProgramId::SelfTestRing);
    GLint work_group_size[3] = { 0, 0, 0 };
    glGetProgramiv(ring_id, GL_COMPUTE_WORK_GROUP_SIZE, work_group_size);
    RON_CHECK_EQ(work_group_size[0], 64);
    RON_CHECK_EQ(work_group_size[1], 1);
    RON_CHECK_EQ(work_group_size[2], 1);

    // Failure path: with an in-memory FileReader containing a fragment shader with a syntax error,
    // shader_build on a ShaderProgram that already holds a valid program returns false,
    // log is non-empty and contains "error" and "[0]=", and the ShaderProgram's id() is unchanged and still links OK.
    ShaderProgram test_prog;
    ShaderDesc desc_ok{
        "TonemapValid",
        "fullscreen.vert",
        "tonemap.frag",
        nullptr,
        {},
        0
    };
    std::string build_log;
    RON_CHECK(shader_build(desc_ok, disk_file_reader, const_cast<char*>(shaders_dir), test_prog, build_log));
    u32 orig_id = test_prog.id();
    RON_CHECK(orig_id != 0);

    std::string vs_src;
    RON_CHECK(disk_file_reader(const_cast<char*>(shaders_dir), "fullscreen.vert", vs_src));

    std::unordered_map<std::string, std::string> bad_map = {
        { "fullscreen.vert", vs_src },
        { "bad.frag", "#version 460 core\nSYNTAX ERROR HERE INVALID GLSL;\n" }
    };

    ShaderDesc desc_bad{
        "BadFrag",
        "fullscreen.vert",
        "bad.frag",
        nullptr,
        {},
        0
    };
    std::string fail_log;
    bool bad_ok = shader_build(desc_bad, mem_reader, &bad_map, test_prog, fail_log);
    RON_CHECK(!bad_ok);
    RON_CHECK(!fail_log.empty());
    RON_CHECK(contains_case_insensitive(fail_log, "error"));
    RON_CHECK(fail_log.find("[0]=") != std::string::npos);
    RON_CHECK_EQ(test_prog.id(), orig_id);

    GLint still_ok = 0;
    glGetProgramiv(test_prog.id(), GL_LINK_STATUS, &still_ok);
    RON_CHECK_EQ(still_ok, GL_TRUE);

    std::string reload_log;
    RON_CHECK(lib.reload_all(disk_file_reader, const_cast<char*>(shaders_dir), reload_log));

    GL_TEST_END();
}

} // namespace ron::render
