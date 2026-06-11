#include <catch2/catch_test_macros.hpp>
#include <unordered_set>
#include "test_helpers.hpp"
#include "frontend/typechecker.hpp"
#include "midend/lower_from_ast.hpp"
#include "midend/passes.hpp"

static mir::Module lower_and_optimize(const std::string& src) {
    auto prog = parse_source(src);
    TypeChecker tc;
    tc.check(prog);
    mir::Module m = mir::lower_program(prog);
    mir::default_pipeline().run(m);
    return m;
}

// Every operand referenced anywhere must have a definition (an instruction
// result or a function parameter). Passes that delete instructions must not
// leave dangling references behind.
static bool module_is_well_formed(const mir::Module& m) {
    for (const auto& fn : m.functions) {
        std::unordered_set<mir::ValueId> defined;
        for (const auto& p : fn.params) defined.insert(p.id);
        for (const auto& b : fn.blocks)
            for (const auto& i : b.insts)
                if (i.result != mir::NO_VALUE) defined.insert(i.result);

        for (const auto& b : fn.blocks) {
            for (const auto& i : b.insts)
                for (mir::ValueId op : i.operands)
                    if (!defined.count(op)) return false;
            if (b.term.kind == mir::Terminator::BR_COND && !defined.count(b.term.cond))
                return false;
            if (b.term.kind == mir::Terminator::RET && !defined.count(b.term.ret_val))
                return false;
        }
    }
    return true;
}

static bool has_fused_loop(const mir::Module& m) {
    for (const auto& fn : m.functions)
        for (const auto& b : fn.blocks)
            for (const auto& i : b.insts)
                if (i.op == mir::Op::FUSED_LOOP) return true;
    return false;
}

TEST_CASE("Loop fusion fires for a single-use map+reduce chain", "[passes]") {
    auto m = lower_and_optimize(
        "let x: float[4] = [1.0, 2.0, 3.0, 4.0];"
        "let y: float[4] = [1.0, 1.0, 1.0, 1.0];"
        "print(sum(x * 2.0 + y));"
    );
    CHECK(has_fused_loop(m));
    CHECK(module_is_well_formed(m));
}

// Regression: the array feeding sum() is also indexed later. Fusing the chain
// used to delete the intermediate array's definition and leave the index.load
// referencing a value that no longer existed ("codegen: untyped ValueId").
TEST_CASE("Loop fusion keeps arrays that have other uses", "[passes]") {
    auto m = lower_and_optimize(
        "let a: float[4] = [1.0, 2.0, 3.0, 4.0];"
        "let b: float[4] = [4.0, 3.0, 2.0, 1.0];"
        "let combined: float[4] = a * 2.0 + b;"
        "print(sum(combined));"
        "print(combined[2]);"
    );
    CHECK(module_is_well_formed(m));
}

TEST_CASE("Optimized MIR stays well-formed across all passes", "[passes]") {
    auto m = lower_and_optimize(
        "fn scale(v: float[4], k: float) -> float[4] {"
        "  return v * k;"
        "}"
        "let x: float[4] = [1.0, 2.0, 3.0, 4.0];"
        "let y: float[4] = scale(x, 2.0 + 3.0 * 0.0);"
        "let i: int = 0;"
        "let acc: float = 0.0;"
        "while i < 4 {"
        "  acc = acc + y[i];"
        "  i = i + 1;"
        "}"
        "print(acc);"
        "print(dot(x, y));"
    );
    CHECK(module_is_well_formed(m));
}
