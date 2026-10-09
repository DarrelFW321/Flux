// Emscripten entry point: the whole compiler runs in the browser.
#include "driver.hpp"
#include <emscripten/bind.h>
#include <sstream>

// options: comma-separated flags, e.g. "O0" or "disable=unroll,disable=cse".
static std::string compile_json(const std::string& source, const std::string& options) {
    CompileOptions opts;
    std::stringstream ss(options);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (item == "O0") opts.optimize = false;
        else if (item.rfind("disable=", 0) == 0) opts.disabled_passes.push_back(item.substr(8));
    }
    return result_to_json(compile(source, opts));
}

EMSCRIPTEN_BINDINGS(flux) {
    emscripten::function("compile", &compile_json);
}
