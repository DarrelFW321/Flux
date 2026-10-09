# Builds fluxc (and optionally the tests and the WebAssembly module) on Windows.
# Needs CMake, Ninja and a C++17 compiler (MinGW g++ or clang) on PATH.
param(
    [switch]$Configure,
    [switch]$Clean,
    [switch]$Tests,    # also build + run the Catch2 suite and spirv-val checks
    [switch]$Wasm      # also build frontend/public/flux_wasm.{js,wasm} (needs emsdk)
)

$ErrorActionPreference = "Stop"
$buildDir = "$PSScriptRoot\build"

if ($Clean) {
    Remove-Item -Recurse -Force $buildDir, "$PSScriptRoot\build_wasm" -ErrorAction SilentlyContinue
    Write-Output "Build directories cleaned."
}

if ($Configure -or $Tests -or !(Test-Path "$buildDir\build.ninja")) {
    $testFlag = if ($Tests) { "ON" } else { "OFF" }
    cmake -S $PSScriptRoot -B $buildDir -G Ninja -DCMAKE_BUILD_TYPE=Release "-DFLUX_BUILD_TESTS=$testFlag"
}
cmake --build $buildDir

if ($Tests) {
    ctest --test-dir $buildDir --output-on-failure
}

if ($Wasm) {
    emcmake cmake -S "$PSScriptRoot\wasm" -B "$PSScriptRoot\build_wasm" -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build "$PSScriptRoot\build_wasm"
    Copy-Item "$PSScriptRoot\build_wasm\flux_wasm.js", "$PSScriptRoot\build_wasm\flux_wasm.wasm" "$PSScriptRoot\frontend\public\"
    Write-Output "Copied flux_wasm.{js,wasm} to frontend/public/"
}
