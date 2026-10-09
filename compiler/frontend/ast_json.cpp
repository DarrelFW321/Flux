#include "frontend/ast_json.hpp"
#include "common/format.hpp"
#include "common/json.hpp"

namespace {

struct Child {
    std::string edge;
    std::string node;
};

std::string node(const char* cat, const std::string& label, const std::string& detail, const std::string& type,
                 SourceLoc loc, const std::vector<Child>& children) {
    std::string kids = "[";
    for (size_t i = 0; i < children.size(); ++i) {
        if (i) kids += ",";
        kids += "{\"edge\":" + json::str(children[i].edge) + ",\"node\":" + children[i].node + "}";
    }
    kids += "]";
    return json::object({
        {"cat", json::str(cat)}, {"label", json::str(label)}, {"detail", json::str(detail)},
        {"type", json::str(type)}, {"line", json::num(loc.line)}, {"col", json::num(loc.col)},
        {"children", kids},
    });
}

std::string attrs_text(const std::vector<Attribute>& attrs) {
    std::string s;
    for (const auto& a : attrs) {
        s += (s.empty() ? "@" : " @") + a.name;
        if (!a.args.empty()) {
            s += "(";
            for (size_t i = 0; i < a.args.size(); ++i) s += (i ? ", " : "") + a.args[i];
            s += ")";
        }
    }
    return s;
}

std::string expr(const Expr& e) {
    std::string type = e.type.is_error() ? "" : e.type.name();
    std::vector<Child> kids;
    auto kid = [&](const std::string& edge, size_t i) { kids.push_back({edge, expr(*e.children[i])}); };
    switch (e.kind) {
        case Expr::Kind::IntLit:   return node("expr", "Int", std::to_string(e.int_value), type, e.loc, {});
        case Expr::Kind::FloatLit: return node("expr", "Float", format_float(e.float_value), type, e.loc, {});
        case Expr::Kind::BoolLit:  return node("expr", "Bool", e.bool_value ? "true" : "false", type, e.loc, {});
        case Expr::Kind::Ident:    return node("expr", "Ident", e.name, type, e.loc, {});
        case Expr::Kind::Unary:
            kid("operand", 0);
            return node("expr", "Unary", e.op, type, e.loc, kids);
        case Expr::Kind::Binary:
            kid("lhs", 0);
            kid("rhs", 1);
            return node("expr", "Binary", e.op, type, e.loc, kids);
        case Expr::Kind::Ternary:
            kid("cond", 0);
            kid("then", 1);
            kid("else", 2);
            return node("expr", "Select", "?:", type, e.loc, kids);
        case Expr::Kind::Call:
            for (size_t i = 0; i < e.children.size(); ++i) kid("arg" + std::to_string(i), i);
            return node("expr", e.builtin >= 0 ? "Builtin" : "Call", e.name + "()", type, e.loc, kids);
        case Expr::Kind::Construct:
            for (size_t i = 0; i < e.children.size(); ++i) kid("arg" + std::to_string(i), i);
            return node("expr", "Construct", e.name, type, e.loc, kids);
        case Expr::Kind::Swizzle:
            kid("base", 0);
            return node("expr", "Swizzle", "." + e.name, type, e.loc, kids);
        case Expr::Kind::Index:
            kid("base", 0);
            kid("index", 1);
            return node("expr", "Index", "[]", type, e.loc, kids);
    }
    return node("expr", "?", "", type, e.loc, {});
}

std::string block(const BlockStmt& b, const char* label = "Block");

std::string stmt(const Stmt& s) {
    std::vector<Child> kids;
    switch (s.kind) {
        case Stmt::Kind::Let:
            if (s.value) kids.push_back({"init", expr(*s.value)});
            return node("stmt", s.is_var ? "Var" : "Let", s.name,
                        s.decl_type.is_error() ? "" : s.decl_type.name(), s.loc, kids);
        case Stmt::Kind::Assign:
            kids.push_back({"target", expr(*s.target)});
            kids.push_back({"value", expr(*s.value)});
            return node("stmt", "Assign", s.op, "", s.loc, kids);
        case Stmt::Kind::If:
            kids.push_back({"cond", expr(*s.value)});
            kids.push_back({"then", block(*s.body)});
            if (s.else_stmt) kids.push_back({"else", stmt(*s.else_stmt)});
            return node("stmt", "If", "", "", s.loc, kids);
        case Stmt::Kind::For:
            kids.push_back({"start", expr(*s.value)});
            kids.push_back({"end", expr(*s.end)});
            kids.push_back({"body", block(*s.body)});
            return node("stmt", "For", s.name, "int", s.loc, kids);
        case Stmt::Kind::While:
            kids.push_back({"cond", expr(*s.value)});
            kids.push_back({"body", block(*s.body)});
            return node("stmt", "While", "", "", s.loc, kids);
        case Stmt::Kind::Break:    return node("stmt", "Break", "", "", s.loc, {});
        case Stmt::Kind::Continue: return node("stmt", "Continue", "", "", s.loc, {});
        case Stmt::Kind::Discard:  return node("stmt", "Discard", "", "", s.loc, {});
        case Stmt::Kind::Return:
            if (s.value) kids.push_back({"value", expr(*s.value)});
            return node("stmt", "Return", "", "", s.loc, kids);
        case Stmt::Kind::ExprStmt:
            kids.push_back({"expr", expr(*s.value)});
            return node("stmt", "ExprStmt", "", "", s.loc, kids);
        case Stmt::Kind::Block:
            return block(*s.body);
    }
    return node("stmt", "?", "", "", s.loc, {});
}

std::string block(const BlockStmt& b, const char* label) {
    std::vector<Child> kids;
    for (size_t i = 0; i < b.stmts.size(); ++i) kids.push_back({std::to_string(i), stmt(*b.stmts[i])});
    return node("stmt", label, "", "", b.loc, kids);
}

} // namespace

std::string ast_to_json(const Program& prog) {
    std::vector<Child> items;
    for (const auto& it : prog.order) {
        if (it.kind == Program::Item::Uniform) {
            const auto& u = *prog.uniforms[it.index];
            std::vector<Child> kids;
            if (u.default_value) kids.push_back({"default", expr(*u.default_value)});
            std::string detail = u.name + (u.attrs.empty() ? "" : "  " + attrs_text(u.attrs));
            items.push_back({"uniform", node("decl", "Uniform", detail, u.type.name(), u.loc, kids)});
        } else if (it.kind == Program::Item::Const) {
            const auto& c = *prog.consts[it.index];
            items.push_back({"const", node("decl", "Const", c.name, c.type.is_error() ? "" : c.type.name(), c.loc,
                                           {{"value", expr(*c.value)}})});
        } else {
            const auto& f = *prog.functions[it.index];
            std::vector<Child> kids;
            for (const auto& p : f.params) {
                std::string detail = p.name + (p.attrs.empty() ? "" : "  " + attrs_text(p.attrs));
                kids.push_back({"param", node("decl", "Param", detail, p.type.name(), p.loc, {})});
            }
            kids.push_back({"body", block(*f.body)});
            std::string detail = (f.attrs.empty() ? "" : attrs_text(f.attrs) + " ") + f.name;
            items.push_back({"fn", node("decl", "Fn", detail, f.return_type.name(), f.loc, kids)});
        }
    }
    return node("decl", "Module", "", "", {1, 1, 1}, items);
}

std::string tokens_to_json(const std::vector<Token>& tokens) {
    return json::array(tokens, [](const Token& t) {
        return json::object({{"type", json::str(token_type_name(t.type))}, {"lexeme", json::str(t.lexeme)},
                             {"line", json::num(t.line)}, {"col", json::num(t.col)}});
    });
}
