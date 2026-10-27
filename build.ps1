param([ValidateSet('armv7','x86_64')][string]$Architecture='armv7')
$ErrorActionPreference='Stop'
$env:PYTHONDONTWRITEBYTECODE='1'
$root=$PSScriptRoot
$toolchain=Join-Path $root 'work/llvm-mingw-20261020-msvcrt-x86_64'
$cmake=Join-Path $root 'work/cmake-4.4.4-windows-x86_64/bin/cmake.exe'
$build=Join-Path $root "work/build-$Architecture"
$ninja=(Get-Command ninja.exe -ErrorAction Stop).Source
$c=(Join-Path $toolchain "bin/$Architecture-w64-mingw32-clang.exe").Replace('\','/')
$cpp=(Join-Path $toolchain "bin/$Architecture-w64-mingw32-clang++.exe").Replace('\','/')
$ar=(Join-Path $toolchain 'bin/llvm-ar.exe').Replace('\','/')
$rc=(Join-Path $toolchain "bin/$Architecture-w64-mingw32-windres.exe").Replace('\','/')
& $cmake -S $root -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" -DCMAKE_SYSTEM_NAME=Windows "-DCMAKE_SYSTEM_PROCESSOR=$Architecture" "-DCMAKE_C_COMPILER=$c" "-DCMAKE_CXX_COMPILER=$cpp" "-DCMAKE_AR=$ar" "-DCMAKE_RC_COMPILER=$rc" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
if($LASTEXITCODE){throw 'Configure failed'}
& $cmake --build $build --parallel 8
if($LASTEXITCODE){throw 'Build failed'}
& (Join-Path $toolchain 'bin/llvm-strip.exe') (Join-Path $build 'MoonlightRT81.exe')
if($LASTEXITCODE){throw 'Strip failed'}
Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $build 'MoonlightRT81.exe')
