#pragma once
#include <stdexcept>
#include <string>

// Structured type used by typechecker and codegen.
// Scalar:  { kind, 0 }
// Array:   { kind, N>0 }   — fixed-size 1D array of `kind`
struct FluxType {
    enum class Kind { Int, Float, Bool, Void };

    Kind kind        = Kind::Void;
    int  array_size  = 0;   // 0 ⇒ scalar, >0 ⇒ fixed-size array

    bool is_scalar() const { return array_size == 0; }
    bool is_array()  const { return array_size >  0; }
    FluxType elem()  const { return { kind, 0 }; }

    bool operator==(const FluxType& o) const {
        return kind == o.kind && array_size == o.array_size;
    }
    bool operator!=(const FluxType& o) const { return !(*this == o); }

    static FluxType scalar(Kind k)        { return { k, 0 }; }
    static FluxType array (Kind k, int n) { return { k, n }; }

    std::string name() const {
        std::string base;
        switch (kind) {
            case Kind::Int:   base = "int";   break;
            case Kind::Float: base = "float"; break;
            case Kind::Bool:  base = "bool";  break;
            case Kind::Void:  base = "void";  break;
        }
        if (array_size > 0) base += "[" + std::to_string(array_size) + "]";
        return base;
    }

    // Parses "int", "float", "bool", "void", or "int[4]" / "float[8]" / ...
    static FluxType parse(const std::string& s) {
        auto br = s.find('[');
        std::string base = (br == std::string::npos) ? s : s.substr(0, br);

        FluxType t;
        if      (base == "int")   t.kind = Kind::Int;
        else if (base == "float") t.kind = Kind::Float;
        else if (base == "bool")  t.kind = Kind::Bool;
        else if (base == "void")  t.kind = Kind::Void;
        else throw std::logic_error("unknown type base: '" + base + "'");

        if (br != std::string::npos) {
            auto cb = s.find(']', br);
            if (cb == std::string::npos)
                throw std::logic_error("malformed array type: '" + s + "'");
            t.array_size = std::stoi(s.substr(br + 1, cb - br - 1));
            if (t.array_size <= 0)
                throw std::logic_error("array size must be > 0: '" + s + "'");
        }
        return t;
    }
};
