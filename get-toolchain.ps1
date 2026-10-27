$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$work=Join-Path $root 'work'
New-Item -ItemType Directory -Path $work -Force | Out-Null
$name='llvm-mingw-20261020-msvcrt-x86_64.zip'
$archive=Join-Path $work $name
$url='https://github.com/mstorsjo/llvm-mingw/releases/download/20261020/'+$name
$expected='52a3e3f584a74c3cde8ceb0c6960549683178e03b36818cb02b7ed109a68ff67'
if(-not(Test-Path -LiteralPath $archive)){Invoke-WebRequest -Uri $url -OutFile $archive}
if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expected){throw 'Toolchain archive hash mismatch'}
$folder=Join-Path $work 'llvm-mingw-20261020-msvcrt-x86_64'
if(-not(Test-Path -LiteralPath $folder)){Expand-Archive -LiteralPath $archive -DestinationPath $work}
Write-Output "Verified toolchain ready: $folder"
