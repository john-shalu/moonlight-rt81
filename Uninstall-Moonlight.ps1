$ErrorActionPreference='Stop'
$programs=[IO.Path]::GetFullPath((Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Programs'))
$target=[IO.Path]::GetFullPath((Join-Path $programs 'MoonlightRT81'))
if([IO.Path]::GetFullPath($PSScriptRoot) -ne $target -or $target -ne $programs+'\MoonlightRT81'){throw 'Run uninstall only from the installed MoonlightRT81 folder'}
foreach($directory in @($programs,$target)){
 if((Get-Item -LiteralPath $directory).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Refusing to remove a linked directory'}
}
if(@(Get-ChildItem -LiteralPath $target -Recurse -Force | Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint}).Count){throw 'Refusing to remove a directory containing links'}
foreach($process in Get-Process -Name MoonlightRT81 -ErrorAction SilentlyContinue){if($process.Path -eq (Join-Path $target 'MoonlightRT81.exe')){throw 'Please close Moonlight RT, then run uninstall again.'}}
$answer=Read-Host 'Remove Moonlight RT? Your pairing data and settings will be kept. Type YES to continue'
if($answer -cne 'YES'){exit 0}
$shell=New-Object -ComObject WScript.Shell
foreach($folder in @([Environment]::GetFolderPath('Desktop'),[Environment]::GetFolderPath('Programs'))){$path=Join-Path $folder 'Moonlight RT.lnk';if(Test-Path -LiteralPath $path){$link=$shell.CreateShortcut($path);if($link.TargetPath -eq (Join-Path $target 'MoonlightRT81.exe')){Remove-Item -LiteralPath $path -Force}}}
Remove-Item -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\MoonlightRT81' -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $target -Recurse -Force
Write-Output 'Moonlight RT removed. Your profile is still in LocalAppData\MoonlightRT81.'
