#pragma once
#include "ir/ir.hpp"
#include <string>
#include <vector>

namespace ir {

// Every pass returns the number of changes it made (0 = nothing to do).
using PassFn = int (*)(Module&);

struct PassInfo {
    const char* name;
    const char* description;
    PassFn      run;
};

// The default pipeline, in order.
const std::vector<PassInfo>& all_passes();

int pass_inline(Module& m);        // inline calls to single-exit functions
int pass_const_fold(Module& m);    // evaluate operations on constants
int pass_simplify(Module& m);      // algebraic identities and composite peepholes
int pass_branch_fold(Module& m);   // resolve `if`s with constant conditions
int pass_forward(Module& m);       // store→load forwarding (mem2reg-lite)
int pass_dse(Module& m);           // dead store / dead variable elimination
int pass_cse(Module& m);           // scoped common-subexpression elimination
int pass_licm(Module& m);          // hoist loop-invariant computations
int pass_unroll(Module& m);        // fully unroll small constant-trip `for` loops
int pass_dce(Module& m);           // dead code and dead function elimination

struct PassStep {
    std::string      pass;
    int              iteration = 0;
    int              changes = 0;
    std::string      ir_after;     // only recorded when `changes > 0`
    std::vector<int> line_map;
};

struct PassTotal {
    std::string name;
    int         changes = 0;
};

struct PipelineReport {
    std::vector<PassStep>  steps;
    std::vector<PassTotal> totals;
    int iterations   = 0;
    int insts_before = 0;
    int insts_after  = 0;
};

// Runs the enabled passes to a fixed point (bounded). Passes named in
// `disabled` are skipped. With `record`, the IR is printed after every pass
// that changed something, for the playground's pass-by-pass view.
PipelineReport run_pipeline(Module& m, const std::vector<std::string>& disabled, bool record);

int count_module_insts(const Module& m);

} // namespace ir
