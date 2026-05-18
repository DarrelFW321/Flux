#include "backend/native.hpp"
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>

void init_native_target() {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
}

void emit_object_file(llvm::Module& module, const std::string& obj_path) {
    // setTargetTriple / createTargetMachine changed from StringRef → Triple in LLVM 20.
    std::string triple_str = llvm::sys::getDefaultTargetTriple();

#if LLVM_VERSION_MAJOR >= 20
    llvm::Triple triple(triple_str);
    module.setTargetTriple(triple);
#else
    module.setTargetTriple(triple_str);
#endif

    std::string err;
    const auto* target = llvm::TargetRegistry::lookupTarget(triple_str, err);
    if (!target)
        throw std::runtime_error("LLVM target not found: " + err);

#if LLVM_VERSION_MAJOR >= 20
    auto TM = std::unique_ptr<llvm::TargetMachine>(
        target->createTargetMachine(llvm::Triple(triple_str), "generic", "",
                                    llvm::TargetOptions{}, llvm::Reloc::PIC_));
#else
    auto TM = std::unique_ptr<llvm::TargetMachine>(
        target->createTargetMachine(triple_str, "generic", "",
                                    llvm::TargetOptions{}, llvm::Reloc::PIC_));
#endif
    if (!TM)
        throw std::runtime_error("Could not create TargetMachine");

    module.setDataLayout(TM->createDataLayout());

    std::error_code EC;
    llvm::raw_fd_ostream dest(obj_path, EC, llvm::sys::fs::OF_None);
    if (EC)
        throw std::runtime_error("Cannot open object file: " + EC.message());

    llvm::legacy::PassManager PM;
    if (TM->addPassesToEmitFile(PM, dest, nullptr,
                                llvm::CodeGenFileType::ObjectFile))
        throw std::runtime_error("TargetMachine cannot emit object files");

    PM.run(module);
    dest.flush();
}

static std::string find_linker() {
    // On Windows we use MSYS2 g++ — it carries the MinGW runtime and can link
    // the COFF object files our TargetMachine emits (x86_64-w64-mingw32 triple).
    // On Linux the container always has clang or gcc in PATH.
    const std::string candidates[] = {
#ifdef _WIN32
        "C:\\msys2\\mingw64\\bin\\g++.exe",
        "g++.exe",
        "clang++.exe",
#else
        "/usr/bin/clang",
        "/usr/bin/clang-21",
        "/usr/bin/clang-20",
        "/usr/bin/clang-19",
        "/usr/bin/clang-18",
        "clang",
        "g++",
#endif
    };
    for (const auto& c : candidates)
        if (std::filesystem::exists(c)) return c;
    return "g++";
}

void link_binary(const std::string& obj_path, const std::string& out_path) {
    std::string clang = find_linker();
    // On Windows, cmd.exe /c requires the whole command in outer quotes when
    // the first token itself is quoted (path with spaces).
#ifdef _WIN32
    std::string cmd = "\"\"" + clang + "\" -o \"" + out_path + "\" \"" + obj_path + "\"\"";
#else
    std::string cmd = "\"" + clang + "\" -o \"" + out_path + "\" \"" + obj_path + "\"";
#endif
    int rc = std::system(cmd.c_str());
    if (rc != 0)
        throw std::runtime_error("Linking failed (clang exit code " +
                                 std::to_string(rc) + ")");
}

void compile_to_binary(llvm::Module& module, const std::string& out_path) {
    // Use a temp .obj file next to the output.
    std::string obj_path = out_path + ".obj";
    emit_object_file(module, obj_path);
    link_binary(obj_path, out_path);
    std::filesystem::remove(obj_path);
}
