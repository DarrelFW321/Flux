param(
    [switch]$Configure,
    [switch]$Clean
)

# MSYS2 mingw64 g++ — same MSVCRT runtime as the LLVM packages installed via pacman.
$gppDir   = "C:\msys2\mingw64\bin"
$ninjaDir = "C:\Users\ASUS\AppData\Local\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe"
$env:PATH = "$gppDir;$ninjaDir;$env:PATH"

$buildDir = "$PSScriptRoot\build"

if ($Clean) {
    Remove-Item -Recurse -Force $buildDir -ErrorAction SilentlyContinue
    Write-Output "Build directory cleaned."
}

if ($Configure -or !(Test-Path "$buildDir\build.ninja")) {
    New-Item -ItemType Directory -Force $buildDir | Out-Null
    cmake -S $PSScriptRoot -B $buildDir `
        -DCMAKE_CXX_COMPILER="$gppDir\g++.exe" `
        -DCMAKE_BUILD_TYPE=Debug `
        -DCMAKE_PREFIX_PATH="C:/msys2/mingw64" `
        -G "Ninja"
}

cmake --build $buildDir
