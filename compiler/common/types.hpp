#pragma once
#include <string>

// Flux's type system is deliberately small and shader-shaped:
//
//   scalars   bool, int, float
//   vectors   vec2 vec3 vec4          (float components)
//   matrices  mat2 mat3 mat4          (square, float, column-major)
//   void      (function results only)
//
// A Type is a (base, rows, cols) triple. Scalars are 1x1, vectors Nx1 and
// matrices NxN. `Error` poisons expressions that already produced a
// diagnostic so the checker can keep going without cascading messages.
struct Type {
    enum class Base { Void, Bool, Int, Float, Error };

    Base base = Base::Void;
    int  rows = 1;
    int  cols = 1;

    static Type void_()        { return {Base::Void, 1, 1}; }
    static Type error()        { return {Base::Error, 1, 1}; }
    static Type bool_()        { return {Base::Bool, 1, 1}; }
    static Type int_()         { return {Base::Int, 1, 1}; }
    static Type float_()       { return {Base::Float, 1, 1}; }
    static Type vec(int n)     { return {Base::Float, n, 1}; }
    static Type mat(int n)     { return {Base::Float, n, n}; }

    bool is_void()   const { return base == Base::Void; }
    bool is_error()  const { return base == Base::Error; }
    bool is_scalar() const { return rows == 1 && cols == 1 && !is_void() && !is_error(); }
    bool is_vector() const { return rows > 1 && cols == 1; }
    bool is_matrix() const { return cols > 1; }
    bool is_float()  const { return base == Base::Float; }
    bool is_int()    const { return base == Base::Int && is_scalar(); }
    bool is_bool()   const { return base == Base::Bool && is_scalar(); }
    // float, vecN — the "genType" accepted by most math builtins.
    bool is_float_scalar_or_vector() const { return is_float() && cols == 1; }
    bool is_numeric_scalar() const { return is_scalar() && (base == Base::Int || base == Base::Float); }

    int  components() const { return rows * cols; }
    Type scalar()     const { return {base, 1, 1}; }
    Type column()     const { return {base, rows, 1}; }

    bool operator==(const Type& o) const { return base == o.base && rows == o.rows && cols == o.cols; }
    bool operator!=(const Type& o) const { return !(*this == o); }

    std::string name() const {
        switch (base) {
            case Base::Void:  return "void";
            case Base::Error: return "<error>";
            case Base::Bool:  return "bool";
            case Base::Int:   return "int";
            case Base::Float: break;
        }
        if (cols > 1)  return "mat" + std::to_string(cols);
        if (rows > 1)  return "vec" + std::to_string(rows);
        return "float";
    }

    // Parses a type keyword. Returns Error for unknown names.
    static Type from_name(const std::string& s) {
        if (s == "void")  return void_();
        if (s == "bool")  return bool_();
        if (s == "int")   return int_();
        if (s == "float") return float_();
        if (s == "vec2")  return vec(2);
        if (s == "vec3")  return vec(3);
        if (s == "vec4")  return vec(4);
        if (s == "mat2")  return mat(2);
        if (s == "mat3")  return mat(3);
        if (s == "mat4")  return mat(4);
        return error();
    }
};

// Shader stages an entry point can target.
enum class Stage { None, Vertex, Fragment };

inline const char* stage_name(Stage s) {
    switch (s) {
        case Stage::Vertex:   return "vertex";
        case Stage::Fragment: return "fragment";
        default:              return "none";
    }
}
