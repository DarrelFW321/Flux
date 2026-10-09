#include <catch2/catch_test_macros.hpp>
#include "test_helpers.hpp"

TEST_CASE("WGSL output declares the uniform block and entry point", "[wgsl]") {
    auto r = compile(frag("return vec4(time);", "uniform time: float;\nuniform res: vec2;"));
    REQUIRE(r.ok);
    const auto& w = r.wgsl.code;
    CHECK(w.find("struct Uniforms {") != std::string::npos);
    CHECK(w.find("res: vec2f,") != std::string::npos);
    CHECK(w.find("@group(0) @binding(0) var<uniform> u: Uniforms;") != std::string::npos);
    CHECK(w.find("@fragment") != std::string::npos);
    CHECK(w.find("-> @location(0) vec4f") != std::string::npos);
    CHECK(w.find("u.time") != std::string::npos);
}

TEST_CASE("WGSL output renames identifiers WGSL reserves", "[wgsl]") {
    CompileOptions o;
    o.optimize = false;
    auto r = compile(frag("var loop = p.x; loop += 1.0; return vec4(loop);"), o);
    REQUIRE(r.ok);
    CHECK(r.wgsl.code.find("var loop_") != std::string::npos);
}

TEST_CASE("WGSL vertex_index is converted from u32", "[wgsl]") {
    auto r = compile("@vertex fn vs(@builtin(vertex_index) i: int) -> vec4 { return vec4(float(i)); }");
    REQUIRE(r.ok);
    CHECK(r.wgsl.code.find("@builtin(vertex_index) i_u32: u32") != std::string::npos);
    CHECK(r.wgsl.code.find("let i = i32(i_u32);") != std::string::npos);
}

TEST_CASE("WGSL line map points back at the source", "[wgsl]") {
    auto r = compile("uniform t: float;\n@fragment fn main(@builtin(position) p: vec4) -> vec4 {\n"
                     "  let named = p.x * t;\n  return vec4(named);\n}\n");
    REQUIRE(r.ok);
    REQUIRE(r.wgsl.line_map.size() == static_cast<size_t>(count(r.wgsl.code, "\n")));
    bool found = false;
    size_t line = 0, start = 0;
    for (size_t pos; (pos = r.wgsl.code.find('\n', start)) != std::string::npos; start = pos + 1, ++line)
        if (r.wgsl.code.substr(start, pos - start).find("let named") != std::string::npos)
            found = r.wgsl.line_map[line] == 3;
    CHECK(found);
}

TEST_CASE("SPIR-V output has a valid header and debug info", "[spirv]") {
    auto r = compile(frag("return p;"));
    REQUIRE(r.ok);
    const auto& w = r.spirv.words;
    REQUIRE(w.size() > 5);
    CHECK(w[0] == 0x07230203u);   // magic
    CHECK(w[1] == 0x00010000u);   // version 1.0
    CHECK(w[3] > 1);              // id bound
    const auto& t = r.spirv.disassembly;
    CHECK(t.find("OpCapability Shader") != std::string::npos);
    CHECK(t.find("OpEntryPoint Fragment %main \"main\"") != std::string::npos);
    CHECK(t.find("OpExecutionMode %main OriginUpperLeft") != std::string::npos);
    CHECK(t.find("BuiltIn FragCoord") != std::string::npos);
    CHECK(t.find("OpLine %file") != std::string::npos);
}

TEST_CASE("SPIR-V uniform blocks carry std140 decorations", "[spirv]") {
    auto r = compile(frag("return vec4(m[0].x + v.x);", "uniform v: vec3;\nuniform m: mat4;"));
    REQUIRE(r.ok);
    const auto& t = r.spirv.disassembly;
    CHECK(t.find("OpDecorate %Uniforms Block") != std::string::npos);
    CHECK(t.find("OpMemberDecorate %Uniforms 1 Offset 16") != std::string::npos);
    CHECK(t.find("OpMemberDecorate %Uniforms 1 MatrixStride 16") != std::string::npos);
    CHECK(t.find("DescriptorSet 0") != std::string::npos);
}

TEST_CASE("SPIR-V emits structured control flow", "[spirv]") {
    CompileOptions o;
    o.optimize = false;
    auto r = compile(frag("var s = 0.0; for i in 0..3 { if p.x > 1.0 { break; } s += 1.0; } return vec4(s);"), o);
    REQUIRE(r.ok);
    CHECK(count(r.spirv.disassembly, "OpLoopMerge") == 1);
    CHECK(count(r.spirv.disassembly, "OpSelectionMerge") == 1);
}

TEST_CASE("Diagnostics stop the pipeline before IR", "[driver]") {
    auto r = compile(frag("return vec3(1.0);"));
    CHECK_FALSE(r.ok);
    CHECK(r.diags.has_errors());
    CHECK(r.wgsl.code.empty());
    CHECK_FALSE(r.ast_json.empty());   // the AST is still available for the visualizer
}
