$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot 'get-toolchain.ps1')
$work=Join-Path $PSScriptRoot 'work'
$archive=Join-Path $work 'cmake-4.4.4-windows-x86_64.zip'
$expected='bace36e94b31c68ab6fa295f26dfa11219e0701cf7c94b0284a7d1cb13dac536'
if(-not(Test-Path -LiteralPath $archive)){Invoke-WebRequest -Uri 'https://github.com/Kitware/CMake/releases/download/v4.4.4/cmake-4.4.4-windows-x86_64.zip' -OutFile $archive}
if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expected){throw 'CMake hash mismatch'}
if(-not(Test-Path -LiteralPath (Join-Path $work 'cmake-4.4.4-windows-x86_64'))){Expand-Archive -LiteralPath $archive -DestinationPath $work}
Write-Output 'Build tools ready. Ninja must also be available in PATH.'
