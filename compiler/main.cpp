#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/ast.hpp"
#include "frontend/typechecker.hpp"
#include "frontend/dumper.hpp"
#include "backend/codegen.hpp"
#include "backend/native.hpp"
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

// ── AST printer ──────────────────────────────────────────────────────────────

static void print_expr(const Expr& e, int depth);
static void print_stmt(const Stmt& s, int depth);
static void print_block(const BlockStmt& b, int depth);

static std::string indent_str(int depth) { return std::string(depth * 2, ' '); }

static void print_expr(const Expr& e, int depth) {
    std::string ind = indent_str(depth);
    std::visit([&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, IntLitExpr>) {
            std::cout << ind << "IntLit(" << v.value << ")\n";
        } else if constexpr (std::is_same_v<T, FloatLitExpr>) {
            std::cout << ind << "FloatLit(" << v.value << ")\n";
        } else if constexpr (std::is_same_v<T, BoolLitExpr>) {
            std::cout << ind << "BoolLit(" << (v.value ? "true" : "false") << ")\n";
        } else if constexpr (std::is_same_v<T, IdentExpr>) {
            std::cout << ind << "Ident(" << v.name << ")\n";
        } else if constexpr (std::is_same_v<T, BinaryExpr>) {
            std::cout << ind << "Binary(" << v.op << ")\n";
            print_expr(*v.left,  depth + 1);
            print_expr(*v.right, depth + 1);
        } else if constexpr (std::is_same_v<T, UnaryExpr>) {
            std::cout << ind << "Unary(" << v.op << ")\n";
            print_expr(*v.operand, depth + 1);
        } else if constexpr (std::is_same_v<T, CallExpr>) {
            std::cout << ind << "Call(" << v.callee << ")\n";
            for (const auto& a : v.args) print_expr(*a, depth + 1);
        } else if constexpr (std::is_same_v<T, ArrayLitExpr>) {
            std::cout << ind << "ArrayLit(" << v.elements.size() << ")\n";
            for (const auto& el : v.elements) print_expr(*el, depth + 1);
        } else if constexpr (std::is_same_v<T, IndexExpr>) {
            std::cout << ind << "Index\n";
            print_expr(*v.array, depth + 1);
            print_expr(*v.index, depth + 1);
        }
    }, e.data);
}

static void print_block(const BlockStmt& b, int depth) {
    for (const auto& s : b.stmts) print_stmt(*s, depth);
}

static void print_stmt(const Stmt& s, int depth) {
    std::string ind = indent_str(depth);
    std::visit([&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, LetStmt>) {
            std::cout << ind << "Let " << v.name << " : " << v.type_name << "\n";
            print_expr(*v.init, depth + 1);
        } else if constexpr (std::is_same_v<T, AssignStmt>) {
            std::cout << ind << "Assign " << v.name << "\n";
            print_expr(*v.value, depth + 1);
        } else if constexpr (std::is_same_v<T, ReturnStmt>) {
            std::cout << ind << "Return\n";
            print_expr(*v.value, depth + 1);
        } else if constexpr (std::is_same_v<T, PrintStmt>) {
            std::cout << ind << "Print\n";
            print_expr(*v.value, depth + 1);
        } else if constexpr (std::is_same_v<T, IfStmt>) {
            std::cout << ind << "If\n";
            std::cout << ind << "  [cond]\n";
            print_expr(*v.condition, depth + 2);
            std::cout << ind << "  [then]\n";
            print_block(*v.then_block, depth + 2);
            if (v.else_block) {
                std::cout << ind << "  [else]\n";
                print_block(**v.else_block, depth + 2);
            }
        } else if constexpr (std::is_same_v<T, WhileStmt>) {
            std::cout << ind << "While\n";
            std::cout << ind << "  [cond]\n";
            print_expr(*v.condition, depth + 2);
            std::cout << ind << "  [body]\n";
            print_block(*v.body, depth + 2);
        } else if constexpr (std::is_same_v<T, ExprStmt>) {
            std::cout << ind << "ExprStmt\n";
            print_expr(*v.expr, depth + 1);
        } else if constexpr (std::is_same_v<T, IndexAssignStmt>) {
            std::cout << ind << "IndexAssign\n";
            print_expr(*v.array, depth + 1);
            print_expr(*v.index, depth + 1);
            print_expr(*v.value, depth + 1);
        }
    }, s.data);
}

static void print_program(const Program& prog) {
    for (const auto& item : prog.items) {
        std::visit([](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, FnDecl>) {
                std::cout << "FnDecl " << v.name << "(";
                for (size_t i = 0; i < v.params.size(); ++i) {
                    if (i) std::cout << ", ";
                    std::cout << v.params[i].name << ": " << v.params[i].type_name;
                }
                std::cout << ") -> " << v.return_type << "\n";
                print_block(*v.body, 1);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<Stmt>>) {
                print_stmt(*v, 0);
            }
        }, item);
    }
}

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::string ir_to_string(llvm::Module& module) {
    std::string buf;
    llvm::raw_string_ostream ss(buf);
    module.print(ss, nullptr);
    return buf;
}

static std::string stem(const std::string& path) {
    auto slash = path.find_last_of("/\\");
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    auto dot = name.rfind('.');
    return (dot == std::string::npos) ? name : name.substr(0, dot);
}

// ── Main ─────────────────────────────────────────────────────────────────────

static void usage(const char* prog) {
    std::cerr << "Usage: " << prog
              << " [--tokens] [--ast] [--emit-llvm] [--compile [-o out]] [--dump-stages] <file>\n";
}

int main(int argc, char** argv) {
    bool        dump_tokens  = false;
    bool        dump_ast     = false;
    bool        emit_llvm    = false;
    bool        do_compile   = false;
    bool        dump_stages  = false;
    std::string source_path;
    std::string out_path;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "--tokens")      dump_tokens = true;
        else if (a == "--ast")         dump_ast    = true;
        else if (a == "--emit-llvm")   emit_llvm   = true;
        else if (a == "--compile")     do_compile  = true;
        else if (a == "--dump-stages") dump_stages = true;
        else if (a == "-o" && i + 1 < argc) out_path = argv[++i];
        else                           source_path = a;
    }

    if (source_path.empty()) { usage(argv[0]); return 1; }
    if (!dump_tokens && !dump_ast && !emit_llvm && !do_compile && !dump_stages)
        dump_ast = true;

    if (do_compile || emit_llvm) init_native_target();

    std::ifstream f(source_path);
    if (!f) { std::cerr << "Cannot open: " << source_path << "\n"; return 1; }
    std::string source(std::istreambuf_iterator<char>(f), {});

    // ── --dump-stages: full JSON pipeline output ──────────────────────────────
    if (dump_stages) {
        std::string tokens_json, ast_json, ir_json, error_msg;

        try {
            Lexer lexer(source);
            auto  tokens = lexer.tokenize();
            tokens_json  = dump_tokens_json(tokens);

            Parser  parser(std::move(tokens));
            Program prog = parser.parse();
            ast_json     = dump_ast_json(prog);

            TypeChecker tc;
            tc.check(prog);

            CodeGen cg;
            auto module = cg.generate(prog);
            ir_json     = ir_to_string(*module);
        } catch (const std::exception& e) {
            error_msg = e.what();
        }

        // Escape strings for inline JSON embedding.
        auto js = [](const std::string& s) -> std::string {
            if (s.empty()) return "null";
            std::string out = "\"";
            for (char c : s) {
                if      (c == '"')  out += "\\\"";
                else if (c == '\\') out += "\\\\";
                else if (c == '\n') out += "\\n";
                else if (c == '\r') out += "\\r";
                else if (c == '\t') out += "\\t";
                else                out += c;
            }
            return out + "\"";
        };

        std::cout << "{\n";
        std::cout << "  \"tokens\":"  << (tokens_json.empty() ? "null" : tokens_json) << ",\n";
        std::cout << "  \"ast\":"     << (ast_json.empty()    ? "null" : ast_json)    << ",\n";
        std::cout << "  \"ir\":"      << js(ir_json) << ",\n";
        std::cout << "  \"error\":"   << js(error_msg) << "\n";
        std::cout << "}\n";
        return error_msg.empty() ? 0 : 1;
    }

    // ── Normal pipeline ───────────────────────────────────────────────────────
    try {
        Lexer lexer(source);
        auto tokens = lexer.tokenize();

        if (dump_tokens) {
            std::cout << "=== Tokens ===\n";
            for (const auto& tok : tokens)
                std::cout << "[" << tok.line << ":" << tok.col << "] "
                          << token_type_name(tok.type)
                          << " \"" << tok.lexeme << "\"\n";
        }

        Parser  parser(std::move(tokens));
        Program prog = parser.parse();

        TypeChecker tc;
        tc.check(prog);

        if (dump_ast) {
            std::cout << "=== AST ===\n";
            print_program(prog);
        }

        if (emit_llvm || do_compile) {
            CodeGen cg;
            auto module = cg.generate(prog);

            std::string err;
            llvm::raw_string_ostream es(err);
            if (llvm::verifyModule(*module, &es)) {
                std::cerr << "Internal error: IR verification failed:\n" << err << "\n";
                return 1;
            }

            if (emit_llvm)  module->print(llvm::outs(), nullptr);

            if (do_compile) {
                if (out_path.empty())
                    out_path = stem(source_path) +
#ifdef _WIN32
                               ".exe";
#else
                               "";
#endif
                compile_to_binary(*module, out_path);
                std::cout << "Compiled: " << out_path << "\n";
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
