# Read-only PE export check. Compatible with Windows RT 8.1 / PowerShell 4.
$ErrorActionPreference='Stop'
$probeRoot=Split-Path -Parent $MyInvocation.MyCommand.Path
function Read-U16($bytes,$offset){[BitConverter]::ToUInt16($bytes,$offset)}
function Read-U32($bytes,$offset){[BitConverter]::ToUInt32($bytes,$offset)}
function Read-Exports([string]$path){
 $bytes=[IO.File]::ReadAllBytes($path)
 if($bytes.Length -lt 256){throw 'File too small'}
 $pe=[int](Read-U32 $bytes 60)
 if([Text.Encoding]::ASCII.GetString($bytes,$pe,4) -ne "PE`0`0"){throw 'Invalid PE signature'}
 $machine=Read-U16 $bytes ($pe+4)
 $count=Read-U16 $bytes ($pe+6)
 $optional=$pe+24
 $optionalSize=Read-U16 $bytes ($pe+20)
 $sections=@()
 for($i=0;$i -lt $count;$i++){
  $offset=$optional+$optionalSize+$i*40
  $sections+=New-Object PSObject -Property @{va=(Read-U32 $bytes ($offset+12));size=[Math]::Max((Read-U32 $bytes ($offset+8)),(Read-U32 $bytes ($offset+16)));raw=(Read-U32 $bytes ($offset+20))}
 }
 function Rva-Offset([uint32]$value){
  foreach($section in $sections){if($value -ge $section.va -and $value -lt $section.va+$section.size){return [int]($section.raw+$value-$section.va)}}
  throw 'Unmapped export RVA'
 }
 $directory=$optional+96
 if((Read-U16 $bytes $optional) -eq 523){$directory=$optional+112}
 $table=Rva-Offset (Read-U32 $bytes $directory)
 $nameCount=Read-U32 $bytes ($table+24)
 $namesOffset=Rva-Offset (Read-U32 $bytes ($table+32))
 $exports=New-Object 'System.Collections.Generic.HashSet[string]'
 for($i=0;$i -lt $nameCount;$i++){
  $start=Rva-Offset (Read-U32 $bytes ($namesOffset+4*$i));$end=$start
  while($end -lt $bytes.Length -and $bytes[$end] -ne 0){$end++}
  if($end -ge $bytes.Length){throw 'Unterminated export name'}
  [void]$exports.Add([Text.Encoding]::ASCII.GetString($bytes,$start,$end-$start))
 }
 return New-Object PSObject -Property @{machine=('0x{0:x}' -f $machine);exports=$exports}
}
try{
 $requirements=Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $probeRoot 'required-imports.json') | ConvertFrom-Json
 $modules=@();$missing=0
 foreach($requirement in $requirements){
  $path=Join-Path ([Environment]::SystemDirectory) $requirement.module
  $absent=@();$machine='';$errorText=''
  try{$parsed=Read-Exports $path;$machine=$parsed.machine;foreach($symbol in $requirement.symbols){if(-not $parsed.exports.Contains([string]$symbol)){$absent+=[string]$symbol}}}
  catch{$errorText=$_.Exception.Message;$absent=@($requirement.symbols)}
  $missing+=$absent.Count
  $modules+=New-Object PSObject -Property @{module=$requirement.module;machine=$machine;missing=$absent;error=$errorText}
 }
 $isArm=($env:PROCESSOR_ARCHITECTURE -eq 'ARM')
 $result=New-Object PSObject -Property @{architecture=$env:PROCESSOR_ARCHITECTURE;os_version=[Environment]::OSVersion.Version.ToString();arm32=$isArm;missing_exports=$missing;modules=$modules}
 $output=Join-Path $probeRoot 'compatibility-result.json'
 try{$result|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $output -Encoding UTF8}catch{$output=Join-Path $env:TEMP 'MoonlightRT81-compatibility-result.json';$result|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $output -Encoding UTF8}
 Write-Host ('Compatibility report: '+$output)
 if(-not $isArm){Write-Host 'This package is ARM32 for Windows RT 8.1. Run it on the Surface tablet.';exit 2}
 if($missing){Write-Host 'Some system APIs are missing. Send compatibility-result.json for diagnosis.';exit 3}
 Write-Host 'Required system exports are present. Starting Moonlight...';exit 0
}catch{Write-Host $_.Exception.Message;exit 4}
