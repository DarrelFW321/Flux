// fluxc — command-line driver for the Flux shader compiler.
#include "driver.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

static void usage() {
    std::cerr <<
        "usage: fluxc <file.flux> [options]\n"
        "\n"
        "  --emit <what>     wgsl (default) | spirv | spirv-asm | ir | ir-raw | ast | tokens | json\n"
        "  -o <file>         write output to a file (spirv is binary)\n"
        "  -O0               disable IR optimization\n"
        "  --disable <pass>  skip one optimization pass (repeatable)\n"
        "  --passes          list optimization passes\n"
        "  --stats           print per-pass change counts to stderr\n";
}

int main(int argc, char** argv) {
    std::string input, output, emit = "wgsl";
    CompileOptions opts;
    opts.record_pass_steps = false;
    bool stats = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { usage(); std::exit(2); }
            return argv[++i];
        };
        if (a == "--emit") emit = next();
        else if (a == "-o") output = next();
        else if (a == "-O0") opts.optimize = false;
        else if (a == "--disable") opts.disabled_passes.push_back(next());
        else if (a == "--stats") stats = true;
        else if (a == "--passes") {
            for (const auto& p : ir::all_passes()) std::cout << p.name << "\t" << p.description << "\n";
            return 0;
        } else if (a == "-h" || a == "--help") { usage(); return 0; }
        else if (!a.empty() && a[0] == '-') { std::cerr << "unknown option " << a << "\n"; usage(); return 2; }
        else input = a;
    }
    if (input.empty()) { usage(); return 2; }

    std::ifstream in(input, std::ios::binary);
    if (!in) { std::cerr << "fluxc: cannot open " << input << "\n"; return 1; }
    std::stringstream ss;
    ss << in.rdbuf();

    auto slash = input.find_last_of("/\\");
    opts.source_name = slash == std::string::npos ? input : input.substr(slash + 1);
    CompileResult r = compile(ss.str(), opts);

    if (emit == "json") {
        std::string j = result_to_json(r);
        if (output.empty()) std::cout << j << "\n";
        else std::ofstream(output) << j;
        return r.ok ? 0 : 1;
    }

    std::cerr << r.diags.format(input);
    if (!r.ok && emit != "tokens" && emit != "ast") return 1;

    if (stats) {
        for (const auto& t : r.passes.totals) std::cerr << "  " << t.name << ": " << t.changes << "\n";
        std::cerr << "  insts: " << r.passes.insts_before << " -> " << r.passes.insts_after
                  << " in " << r.passes.iterations << " iteration(s)\n";
    }

    if (emit == "spirv") {
        if (output.empty()) { std::cerr << "fluxc: --emit spirv needs -o <file>\n"; return 2; }
        std::ofstream out(output, std::ios::binary);
        out.write(reinterpret_cast<const char*>(r.spirv.words.data()),
                  static_cast<std::streamsize>(r.spirv.words.size() * sizeof(uint32_t)));
        return 0;
    }

    std::string text;
    if (emit == "wgsl") text = r.wgsl.code;
    else if (emit == "spirv-asm") text = r.spirv.disassembly;
    else if (emit == "ir") text = r.ir_opt.text;
    else if (emit == "ir-raw") text = r.ir_raw.text;
    else if (emit == "ast") text = r.ast_json + "\n";
    else if (emit == "tokens") {
        for (const auto& t : r.tokens)
            text += std::to_string(t.line) + ":" + std::to_string(t.col) + "\t" + token_type_name(t.type) +
                    "\t" + t.lexeme + "\n";
    } else { std::cerr << "fluxc: unknown --emit kind '" << emit << "'\n"; return 2; }

    if (output.empty()) std::cout << text;
    else std::ofstream(output) << text;
    return 0;
}
