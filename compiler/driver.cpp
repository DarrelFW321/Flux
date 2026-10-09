#include "driver.hpp"
#include "common/json.hpp"
#include "frontend/ast_json.hpp"
#include "frontend/parser.hpp"
#include "ir/lower.hpp"
#include <algorithm>
#include <chrono>

namespace {

class Timer {
public:
    explicit Timer(std::vector<StageTiming>& out, const char* stage)
        : out_(out), stage_(stage), start_(std::chrono::steady_clock::now()) {}
    ~Timer() {
        auto d = std::chrono::steady_clock::now() - start_;
        out_.push_back({stage_, std::chrono::duration<double, std::milli>(d).count()});
    }

private:
    std::vector<StageTiming>& out_;
    const char* stage_;
    std::chrono::steady_clock::time_point start_;
};

} // namespace

CompileResult compile(const std::string& source, const CompileOptions& opts) {
    CompileResult r;
    Program prog;
    try {
        {
            Timer t(r.timings, "lex");
            r.tokens = Lexer(source).tokenize();
        }
        {
            Timer t(r.timings, "parse");
            prog = Parser(r.tokens).parse();
        }
    } catch (const CompileError& e) {
        r.diags.error(e.loc, e.what());
        return r;
    }

    {
        Timer t(r.timings, "typecheck");
        TypeChecker tc(r.diags);
        tc.check(prog);
        r.reflection = tc.reflection();
    }
    r.ast_json = ast_to_json(prog);
    if (r.diags.has_errors()) return r;

    ir::Module mod;
    {
        Timer t(r.timings, "lower");
        mod = ir::lower_program(prog, r.reflection);
        for (auto& f : mod.functions) ir::truncate_unreachable(f.body);
    }
    r.ir_raw = ir::print_module(mod);
    if (opts.optimize) {
        Timer t(r.timings, "optimize");
        r.passes = ir::run_pipeline(mod, opts.disabled_passes, opts.record_pass_steps);
    } else {
        r.passes.insts_before = r.passes.insts_after = ir::count_module_insts(mod);
    }
    r.ir_opt = ir::print_module(mod);
    {
        Timer t(r.timings, "wgsl");
        r.wgsl = backend::emit_wgsl(mod);
    }
    {
        Timer t(r.timings, "spirv");
        r.spirv = backend::emit_spirv(mod, opts.source_name);
    }
    r.ok = true;
    return r;
}

std::string result_to_json(const CompileResult& r) {
    using namespace json;

    auto diags = r.diags.all();
    std::stable_sort(diags.begin(), diags.end(), [](const Diagnostic& a, const Diagnostic& b) {
        return a.loc.line != b.loc.line ? a.loc.line < b.loc.line : a.loc.col < b.loc.col;
    });
    std::string diag_json = array(diags, [](const Diagnostic& d) {
        const char* sev = d.severity == Diagnostic::Severity::Error   ? "error"
                        : d.severity == Diagnostic::Severity::Warning ? "warning"
                                                                      : "note";
        return object({{"severity", str(sev)}, {"message", str(d.message)}, {"line", num(d.loc.line)},
                       {"col", num(d.loc.col)}, {"len", num(d.loc.len)}});
    });

    std::string uniforms = array(r.reflection.uniforms, [](const UniformInfo& u) {
        return object({
            {"name", str(u.name)}, {"type", str(u.type.name())}, {"offset", num(u.offset)},
            {"size", num(u.size)},
            {"default", array(u.default_value, [](double v) { return num(v); })},
            {"range", u.has_range ? "[" + num(u.range_min) + "," + num(u.range_max) + "]" : "null"},
            {"color", boolean(u.color)}, {"line", num(u.line)},
        });
    });
    std::string entries = array(r.reflection.entries, [](const EntryInfo& e) {
        return object({{"name", str(e.name)}, {"stage", str(stage_name(e.stage))}});
    });

    std::string steps = array(r.passes.steps, [](const ir::PassStep& s) {
        return object({{"pass", str(s.pass)}, {"iteration", num(s.iteration)}, {"changes", num(s.changes)},
                       {"ir", s.changes ? str(s.ir_after) : "null"},
                       {"map", s.changes ? ints(s.line_map) : "null"}});
    });
    std::string totals = array(r.passes.totals, [](const ir::PassTotal& t) {
        return object({{"name", str(t.name)}, {"changes", num(t.changes)}});
    });
    std::string pass_info = array(ir::all_passes(), [](const ir::PassInfo& p) {
        return object({{"name", str(p.name)}, {"description", str(p.description)}});
    });
    std::string timings = array(r.timings, [](const StageTiming& t) {
        return object({{"stage", str(t.stage)}, {"ms", num(t.ms)}});
    });

    std::string spirv_words = "null";
    if (r.ok) {
        spirv_words = "[";
        for (size_t i = 0; i < r.spirv.words.size(); ++i)
            spirv_words += (i ? "," : "") + std::to_string(r.spirv.words[i]);
        spirv_words += "]";
    }

    auto text = [&](const std::string& s) { return r.ok ? str(s) : "null"; };

    return object({
        {"ok", boolean(r.ok)},
        {"diagnostics", diag_json},
        {"tokens", tokens_to_json(r.tokens)},
        {"ast", r.ast_json.empty() ? "null" : r.ast_json},
        {"reflection", object({{"uniforms", uniforms}, {"uniformSize", num(r.reflection.uniform_block_size)},
                               {"entries", entries}})},
        {"ir", object({{"raw", text(r.ir_raw.text)}, {"rawMap", ints(r.ir_raw.line_map)},
                       {"opt", text(r.ir_opt.text)}, {"optMap", ints(r.ir_opt.line_map)},
                       {"instsBefore", num(r.passes.insts_before)}, {"instsAfter", num(r.passes.insts_after)},
                       {"iterations", num(r.passes.iterations)}})},
        {"passes", object({{"available", pass_info}, {"totals", totals}, {"steps", steps}})},
        {"wgsl", object({{"code", text(r.wgsl.code)}, {"map", ints(r.wgsl.line_map)}})},
        {"spirv", object({{"text", text(r.spirv.disassembly)}, {"map", ints(r.spirv.line_map)},
                          {"words", spirv_words}})},
        {"timings", timings},
    });
}
