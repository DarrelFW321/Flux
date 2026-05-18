#pragma once
#include <llvm/IR/Module.h>
#include <string>

// Call once at program startup before any native compilation.
void init_native_target();

// Emit a COFF/ELF object file for the module at obj_path.
void emit_object_file(llvm::Module& module, const std::string& obj_path);

// Link obj_path into a native executable at out_path using clang.
void link_binary(const std::string& obj_path, const std::string& out_path);

// Convenience: emit object file then link.
void compile_to_binary(llvm::Module& module, const std::string& out_path);
