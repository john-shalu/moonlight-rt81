param([switch]$CheckOnly)
$ErrorActionPreference='Stop'
$packageRoot=[IO.Path]::GetFullPath($PSScriptRoot)
$manifest=Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $packageRoot 'build-manifest.json') | ConvertFrom-Json
$files=@()
foreach($line in Get-Content -Encoding UTF8 -LiteralPath (Join-Path $packageRoot 'SHA256SUMS.txt')){
 if(-not $line){continue}
 $parts=$line -split '  ',2
 if($parts.Count -ne 2 -or $parts[0] -notmatch '^[0-9a-fA-F]{64}$'){throw 'Invalid package checksum list'}
 $relative=$parts[1].Replace('/','\')
 $source=[IO.Path]::GetFullPath((Join-Path $packageRoot $relative))
 if(-not $source.StartsWith($packageRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Invalid package path'}
 if((Get-Item -LiteralPath $source).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked package files are not supported'}
 if((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $parts[0]){throw ('Package file damaged: '+$relative)}
 $files+=New-Object PSObject -Property @{relative=$relative;source=$source}
}
if($CheckOnly){Write-Output ('Package verified: '+$files.Count+' files; version '+$manifest.version);exit 0}
if($env:PROCESSOR_ARCHITECTURE -ne 'ARM'){throw 'This package is for Windows RT 8.1 ARM32. Run it on the Surface tablet.'}
$powershell=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
& $powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $packageRoot 'Check-Compatibility.ps1')
if($LASTEXITCODE -ne 0){throw 'Compatibility check did not pass'}
$local=[Environment]::GetFolderPath('LocalApplicationData')
$programs=[IO.Path]::GetFullPath((Join-Path $local 'Programs'))
$destination=[IO.Path]::GetFullPath((Join-Path $programs 'MoonlightRT81'))
if($destination -ne $programs+'\MoonlightRT81'){throw 'Invalid installation target'}
foreach($directory in @($programs,$destination)){
 if((Test-Path -LiteralPath $directory) -and ((Get-Item -LiteralPath $directory).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Installation target is a link'}
}
foreach($process in Get-Process -Name MoonlightRT81 -ErrorAction SilentlyContinue){if($process.Path -eq (Join-Path $destination 'MoonlightRT81.exe')){throw 'Please close Moonlight RT before installing this update.'}}
if($destination -ne $packageRoot){
 New-Item -ItemType Directory -Path $destination -Force | Out-Null
 foreach($file in $files){$target=Join-Path $destination $file.relative;$parent=Split-Path -Parent $target;New-Item -ItemType Directory -Path $parent -Force | Out-Null;Copy-Item -LiteralPath $file.source -Destination $target -Force}
 Copy-Item -LiteralPath (Join-Path $packageRoot 'SHA256SUMS.txt') -Destination $destination -Force
}
$shell=New-Object -ComObject WScript.Shell
$exe=Join-Path $destination 'MoonlightRT81.exe'
$links=@((Join-Path ([Environment]::GetFolderPath('Desktop')) 'Moonlight RT.lnk'),(Join-Path ([Environment]::GetFolderPath('Programs')) 'Moonlight RT.lnk'))
foreach($path in $links){$shortcut=$shell.CreateShortcut($path);$shortcut.TargetPath=$exe;$shortcut.WorkingDirectory=$destination;$shortcut.IconLocation=$exe+',0';$shortcut.Description='Moonlight for Windows RT 8.1';$shortcut.Save()}
$uninstall='HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\MoonlightRT81'
New-Item -Path $uninstall -Force | Out-Null
$properties=@{DisplayName='Moonlight RT';DisplayVersion=$manifest.version;Publisher='john-shalu';InstallLocation=$destination;DisplayIcon=$exe;UninstallString=('"'+$powershell+'" -NoProfile -ExecutionPolicy Bypass -File "'+(Join-Path $destination 'Uninstall-Moonlight.ps1')+'"')}
foreach($key in $properties.Keys){New-ItemProperty -Path $uninstall -Name $key -Value $properties[$key] -PropertyType String -Force | Out-Null}
New-ItemProperty -Path $uninstall -Name NoModify -Value 1 -PropertyType DWord -Force | Out-Null
New-ItemProperty -Path $uninstall -Name NoRepair -Value 1 -PropertyType DWord -Force | Out-Null
Write-Output 'Moonlight RT installed. Desktop and Start menu shortcuts are ready. Existing pairing data was kept.'
Start-Process -FilePath $exe
