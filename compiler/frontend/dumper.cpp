#include "frontend/dumper.hpp"
#include <sstream>

// ── JSON helpers ──────────────────────────────────────────────────────────────

static std::string jstr(const std::string& s) {
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
}

static std::string jstr(const char* s) { return jstr(std::string(s)); }

// ── Tokens ────────────────────────────────────────────────────────────────────

std::string dump_tokens_json(const std::vector<Token>& tokens) {
    std::string out = "[";
    bool first = true;
    for (const auto& t : tokens) {
        if (t.type == TokenType::EOF_TOK) continue;
        if (!first) out += ",";
        first = false;
        out += "{\"type\":" + jstr(token_type_name(t.type));
        out += ",\"lexeme\":"  + jstr(t.lexeme);
        out += ",\"line\":"    + std::to_string(t.line);
        out += ",\"col\":"     + std::to_string(t.col) + "}";
    }
    return out + "]";
}

// ── AST ───────────────────────────────────────────────────────────────────────

static std::string expr_json(const Expr& e);
static std::string stmt_json(const Stmt& s);
static std::string block_json(const BlockStmt& b);

static std::string expr_json(const Expr& e) {
    return std::visit([](const auto& v) -> std::string {
        using T = std::decay_t<decltype(v)>;

        if constexpr (std::is_same_v<T, IntLitExpr>)
            return "{\"kind\":\"IntLit\",\"value\":" + std::to_string(v.value) + "}";

        if constexpr (std::is_same_v<T, FloatLitExpr>) {
            std::ostringstream ss;
            ss << v.value;
            return "{\"kind\":\"FloatLit\",\"value\":" + ss.str() + "}";
        }

        if constexpr (std::is_same_v<T, BoolLitExpr>)
            return std::string("{\"kind\":\"BoolLit\",\"value\":") +
                   (v.value ? "true" : "false") + "}";

        if constexpr (std::is_same_v<T, IdentExpr>)
            return "{\"kind\":\"Ident\",\"name\":" + jstr(v.name) + "}";

        if constexpr (std::is_same_v<T, UnaryExpr>)
            return "{\"kind\":\"Unary\",\"op\":" + jstr(v.op) +
                   ",\"operand\":" + expr_json(*v.operand) + "}";

        if constexpr (std::is_same_v<T, BinaryExpr>)
            return "{\"kind\":\"Binary\",\"op\":" + jstr(v.op) +
                   ",\"left\":"  + expr_json(*v.left) +
                   ",\"right\":" + expr_json(*v.right) + "}";

        if constexpr (std::is_same_v<T, CallExpr>) {
            std::string args = "[";
            for (size_t i = 0; i < v.args.size(); ++i) {
                if (i) args += ",";
                args += expr_json(*v.args[i]);
            }
            return "{\"kind\":\"Call\",\"callee\":" + jstr(v.callee) +
                   ",\"args\":" + args + "]}";
        }

        return "null";
    }, e.data);
}

static std::string block_json(const BlockStmt& b) {
    std::string out = "[";
    for (size_t i = 0; i < b.stmts.size(); ++i) {
        if (i) out += ",";
        out += stmt_json(*b.stmts[i]);
    }
    return out + "]";
}

static std::string stmt_json(const Stmt& s) {
    return std::visit([](const auto& v) -> std::string {
        using T = std::decay_t<decltype(v)>;

        if constexpr (std::is_same_v<T, LetStmt>)
            return "{\"kind\":\"Let\",\"name\":" + jstr(v.name) +
                   ",\"type\":" + jstr(v.type_name) +
                   ",\"init\":"  + expr_json(*v.init) + "}";

        if constexpr (std::is_same_v<T, AssignStmt>)
            return "{\"kind\":\"Assign\",\"name\":" + jstr(v.name) +
                   ",\"value\":" + expr_json(*v.value) + "}";

        if constexpr (std::is_same_v<T, ReturnStmt>)
            return "{\"kind\":\"Return\",\"value\":" + expr_json(*v.value) + "}";

        if constexpr (std::is_same_v<T, PrintStmt>)
            return "{\"kind\":\"Print\",\"value\":" + expr_json(*v.value) + "}";

        if constexpr (std::is_same_v<T, IfStmt>) {
            std::string out = "{\"kind\":\"If\",\"cond\":" + expr_json(*v.condition) +
                              ",\"then\":" + block_json(*v.then_block);
            if (v.else_block)
                out += ",\"else\":" + block_json(**v.else_block);
            return out + "}";
        }

        if constexpr (std::is_same_v<T, WhileStmt>)
            return "{\"kind\":\"While\",\"cond\":" + expr_json(*v.condition) +
                   ",\"body\":" + block_json(*v.body) + "}";

        if constexpr (std::is_same_v<T, ExprStmt>)
            return "{\"kind\":\"ExprStmt\",\"expr\":" + expr_json(*v.expr) + "}";

        return "null";
    }, s.data);
}

std::string dump_ast_json(const Program& prog) {
    std::string items = "[";
    for (size_t i = 0; i < prog.items.size(); ++i) {
        if (i) items += ",";
        std::visit([&items](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, FnDecl>) {
                std::string params = "[";
                for (size_t j = 0; j < v.params.size(); ++j) {
                    if (j) params += ",";
                    params += "{\"name\":" + jstr(v.params[j].name) +
                              ",\"type\":"  + jstr(v.params[j].type_name) + "}";
                }
                params += "]";
                items += "{\"kind\":\"FnDecl\",\"name\":" + jstr(v.name) +
                         ",\"params\":" + params +
                         ",\"return_type\":" + jstr(v.return_type) +
                         ",\"body\":" + block_json(*v.body) + "}";
            } else if constexpr (std::is_same_v<T, std::unique_ptr<Stmt>>) {
                items += stmt_json(*v);
            }
        }, prog.items[i]);
    }
    items += "]";
    return "{\"kind\":\"Program\",\"items\":" + items + "}";
}
