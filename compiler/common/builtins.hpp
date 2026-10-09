#pragma once
#include <cstring>

// Built-in math functions. One table drives the type checker (signatures),
// the IR (purity, constant folding) and both backends (spellings/opcodes).
enum class BuiltinFn {
    Sin, Cos, Tan, Asin, Acos, Atan, Atan2,
    Exp, Exp2, Log, Log2, Sqrt, InverseSqrt, Pow,
    Abs, Sign, Floor, Ceil, Fract, Round, Trunc,
    Radians, Degrees,
    Min, Max, Clamp, Mix, Step, Smoothstep, Mod,
    Length, Distance, Dot, Cross, Normalize, Reflect, Refract,
    Transpose, Determinant,
    Count
};

// How each argument relates to the call's generic type T (float or vecN).
enum class ArgKind : char {
    Gen,          // exactly T
    GenOrScalar,  // T, or a float scalar broadcast to T (e.g. clamp(v, 0.0, 1.0))
    Float,        // always a float scalar (refract's eta)
    Vec3,         // exactly vec3 (cross)
    Mat,          // any square matrix
};

enum class RetKind : char { Gen, Float, Vec3, Mat };

struct BuiltinInfo {
    BuiltinFn   fn;
    const char* name;        // Flux spelling
    const char* wgsl;        // WGSL spelling (nullptr = lowered in the IR)
    int         glsl450;     // GLSL.std.450 extended opcode, -1 = core op / lowered
    int         arity;
    ArgKind     args[3];
    RetKind     ret;
    bool        allows_int;  // T may also be int (abs, sign, min, max, clamp)
};

// GLSL.std.450 opcode numbers (from the Khronos extended instruction set spec).
namespace glsl { enum : int {
    Round = 1, Trunc = 3, FAbs = 4, SAbs = 5, FSign = 6, SSign = 7, Floor = 8, Ceil = 9,
    Fract = 10, Radians = 11, Degrees = 12, Sin = 13, Cos = 14, Tan = 15, Asin = 16,
    Acos = 17, Atan = 18, Atan2 = 25, Pow = 26, Exp = 27, Log = 28, Exp2 = 29, Log2 = 30,
    Sqrt = 31, InverseSqrt = 32, Determinant = 33, FMin = 37, SMin = 39, FMax = 40,
    SMax = 42, FClamp = 43, SClamp = 45, FMix = 46, Step = 48, SmoothStep = 49,
    Length = 66, Distance = 67, Cross = 68, Normalize = 69, Reflect = 71, Refract = 72,
}; }

using AK = ArgKind;
using RK = RetKind;

inline const BuiltinInfo BUILTINS[] = {
    {BuiltinFn::Sin,         "sin",         "sin",         glsl::Sin,         1, {AK::Gen},                               RK::Gen,   false},
    {BuiltinFn::Cos,         "cos",         "cos",         glsl::Cos,         1, {AK::Gen},                               RK::Gen,   false},
    {BuiltinFn::Tan,         "tan",         "tan",         glsl::Tan,         1, {AK::Gen},                               RK::Gen,   false},
    {BuiltinFn::Asin,        "asin",        "asin",        glsl::Asin,        1, {AK::Gen},                               RK::Gen,   false},
    {BuiltinFn::Acos,        "acos",        "acos",        glsl::Acos,        1, {AK::Gen},                               RK::Gen,   false},
    {BuiltinFn::Atan,        "atan",        "atan",        glsl::Atan,        1, {AK::Gen},                               RK::Gen,   false},
    {BuiltinFn::Atan2,       "atan2",       "atan2",       glsl::Atan2,       2, {AK::Gen, AK::Gen},                      RK::Gen,   false},
    {BuiltinFn::Exp,         "exp",         "exp",         glsl::Exp,         1, {AK::Gen},                               RK::Gen,   false},
    {BuiltinFn::Exp2,        "exp2",        "exp2",        glsl::Exp2,        1, {AK::Gen},                               RK::Gen,   false},
    {BuiltinFn::Log,         "log",         "log",         glsl::Log,         1, {AK::Gen},                               RK::Gen,   false},
    {BuiltinFn::Log2,        "log2",        "log2",        glsl::Log2,        1, {AK::Gen},                               RK::Gen,   false},
    {BuiltinFn::Sqrt,        "sqrt",        "sqrt",        glsl::Sqrt,        1, {AK::Gen},                               RK::Gen,   false},
    {BuiltinFn::InverseSqrt, "inversesqrt", "inverseSqrt", glsl::InverseSqrt, 1, {AK::Gen},                               RK::Gen,   false},
    {BuiltinFn::Pow,         "pow",         "pow",         glsl::Pow,         2, {AK::Gen, AK::Gen},                      RK::Gen,   false},
    {BuiltinFn::Abs,         "abs",         "abs",         glsl::FAbs,        1, {AK::Gen},                               RK::Gen,   true},
    {BuiltinFn::Sign,        "sign",        "sign",        glsl::FSign,       1, {AK::Gen},                               RK::Gen,   true},
    {BuiltinFn::Floor,       "floor",       "floor",       glsl::Floor,       1, {AK::Gen},                               RK::Gen,   false},
    {BuiltinFn::Ceil,        "ceil",        "ceil",        glsl::Ceil,        1, {AK::Gen},                               RK::Gen,   false},
    {BuiltinFn::Fract,       "fract",       "fract",       glsl::Fract,       1, {AK::Gen},                               RK::Gen,   false},
    {BuiltinFn::Round,       "round",       "round",       glsl::Round,       1, {AK::Gen},                               RK::Gen,   false},
    {BuiltinFn::Trunc,       "trunc",       "trunc",       glsl::Trunc,       1, {AK::Gen},                               RK::Gen,   false},
    {BuiltinFn::Radians,     "radians",     "radians",     glsl::Radians,     1, {AK::Gen},                               RK::Gen,   false},
    {BuiltinFn::Degrees,     "degrees",     "degrees",     glsl::Degrees,     1, {AK::Gen},                               RK::Gen,   false},
    {BuiltinFn::Min,         "min",         "min",         glsl::FMin,        2, {AK::Gen, AK::GenOrScalar},              RK::Gen,   true},
    {BuiltinFn::Max,         "max",         "max",         glsl::FMax,        2, {AK::Gen, AK::GenOrScalar},              RK::Gen,   true},
    {BuiltinFn::Clamp,       "clamp",       "clamp",       glsl::FClamp,      3, {AK::Gen, AK::GenOrScalar, AK::GenOrScalar}, RK::Gen, true},
    {BuiltinFn::Mix,         "mix",         "mix",         glsl::FMix,        3, {AK::Gen, AK::Gen, AK::GenOrScalar},     RK::Gen,   false},
    {BuiltinFn::Step,        "step",        "step",        glsl::Step,        2, {AK::GenOrScalar, AK::Gen},              RK::Gen,   false},
    {BuiltinFn::Smoothstep,  "smoothstep",  "smoothstep",  glsl::SmoothStep,  3, {AK::GenOrScalar, AK::GenOrScalar, AK::Gen}, RK::Gen, false},
    {BuiltinFn::Mod,         "mod",         nullptr,       -1,                2, {AK::Gen, AK::GenOrScalar},              RK::Gen,   false},
    {BuiltinFn::Length,      "length",      "length",      glsl::Length,      1, {AK::Gen},                               RK::Float, false},
    {BuiltinFn::Distance,    "distance",    "distance",    glsl::Distance,    2, {AK::Gen, AK::Gen},                      RK::Float, false},
    {BuiltinFn::Dot,         "dot",         "dot",         -1,                2, {AK::Gen, AK::Gen},                      RK::Float, false},
    {BuiltinFn::Cross,       "cross",       "cross",       glsl::Cross,       2, {AK::Vec3, AK::Vec3},                    RK::Vec3,  false},
    {BuiltinFn::Normalize,   "normalize",   "normalize",   glsl::Normalize,   1, {AK::Gen},                               RK::Gen,   false},
    {BuiltinFn::Reflect,     "reflect",     "reflect",     glsl::Reflect,     2, {AK::Gen, AK::Gen},                      RK::Gen,   false},
    {BuiltinFn::Refract,     "refract",     "refract",     glsl::Refract,     3, {AK::Gen, AK::Gen, AK::Float},           RK::Gen,   false},
    {BuiltinFn::Transpose,   "transpose",   "transpose",   -1,                1, {AK::Mat},                               RK::Mat,   false},
    {BuiltinFn::Determinant, "determinant", "determinant", glsl::Determinant, 1, {AK::Mat},                               RK::Float, false},
};

inline const BuiltinInfo& builtin_info(BuiltinFn fn) { return BUILTINS[static_cast<int>(fn)]; }

inline const BuiltinInfo* find_builtin(const char* name) {
    for (const auto& b : BUILTINS)
        if (std::strcmp(b.name, name) == 0) return &b;
    return nullptr;
}
