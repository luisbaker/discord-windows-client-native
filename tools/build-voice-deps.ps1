# Builds the native voice dependencies into deps/ (not committed):
#   - OpenSSL 3, mlspp, Opus   (vcpkg, triplet x64-windows-static-md)
#   - libdave                  (Discord's DAVE end-to-end encryption library, CMake)
# Requires Visual Studio Build Tools 2026 with the C++ workload. Takes ~15-20 minutes the first time.
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$deps = Join-Path $root "deps"
$installed = Join-Path $deps "installed"
$triplet = "x64-windows-static-md"
New-Item -ItemType Directory -Force $deps | Out-Null

# 1) libdave sources + a recent vcpkg (the pinned one predates VS 2026)
$dave = Join-Path $deps "libdave"
if (-not (Test-Path $dave)) { git clone --depth 1 https://github.com/discord/libdave.git $dave }
$vcpkg = Join-Path $dave "cpp\vcpkg"
if (-not (Test-Path (Join-Path $vcpkg ".git"))) { git -C $dave submodule update --init --depth 1 cpp/vcpkg }
git -C $vcpkg fetch --depth 1 origin master
git -C $vcpkg checkout -q FETCH_HEAD
& (Join-Path $vcpkg "bootstrap-vcpkg.bat") -disableMetrics

# 2) OpenSSL + mlspp (libdave overlay port) + Opus
$env:VCPKG_ROOT = $vcpkg
& (Join-Path $vcpkg "vcpkg.exe") install openssl mlspp opus --triplet $triplet --host-triplet $triplet `
    --overlay-ports=(Join-Path $dave "cpp\vcpkg-alts\openssl_3\overlay-ports") --classic --x-install-root=$installed

# 3) libdave (static, /MD to match the app)
$vsDevCmd = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"
$build = Join-Path $dave "cpp\build"
$prefix = Join-Path $installed $triplet
cmd /c "`"$vsDevCmd`" -arch=x64 -host_arch=x64 && cmake -S `"$dave\cpp`" -B `"$build`" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DTESTING=OFF -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL -DCMAKE_PREFIX_PATH=`"$prefix`" -DCMAKE_COMPILE_WARNING_AS_ERROR=OFF && cmake --build `"$build`" --target libdave"
if ($LASTEXITCODE -ne 0) { throw "libdave build failed" }
Write-Host "Voice dependencies ready in $deps"
