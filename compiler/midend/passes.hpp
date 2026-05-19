#pragma once
#include "midend/ir.hpp"
#include <string>
#include <vector>

// ── Optimization passes over FluxIR ─────────────────────────────────────────
//
// Each pass mutates a Function in place and returns the number of
// transformations it performed. Passes are intentionally small and composable
// — running the same pass twice on the same function is always safe and a
// no-op when the function has reached a fixed point for that pass.
//
// The pipeline runs every pass in order, then loops until a full round
// produces zero changes (capped at a small iteration limit to avoid bugs in a
// pass causing an infinite loop).
namespace mir {

// Per-pass aggregate count (across all functions, all iterations).
struct PassResult {
    std::string name;
    int         changes = 0;
};

// A single pass invocation inside the pipeline. Snapshots are recorded after
// the pass runs so a "what did this pass do" diff is always available.
struct PassStep {
    std::string name;
    int         iteration = 0;   // 0-based round through the pipeline
    int         changes   = 0;
    std::string mir_after;       // full module text immediately after this pass
};

struct PassReport {
    std::vector<PassStep>   steps;     // chronological timeline of pass invocations
    std::vector<PassResult> passes;    // aggregated change counts per pass name
    int                     iterations = 0;
    int total_changes() const {
        int n = 0;
        for (const auto& p : passes) n += p.changes;
        return n;
    }
};

// A pass: take a function, mutate it, return # of changes.
using PassFn = int (*)(Function&);

struct PassPipeline {
    std::vector<std::pair<std::string, PassFn>> passes;
    PassReport run(Module& m, int max_iterations = 8);
};

// The default optimization pipeline used by the compiler.
PassPipeline default_pipeline();

// Individual passes.
int const_fold       (Function& fn);
int algebraic_simplify(Function& fn);
int loop_fusion      (Function& fn);
int dce              (Function& fn);

}  // namespace mir
