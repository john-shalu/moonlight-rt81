$ErrorActionPreference='Stop'
$work=Join-Path $PSScriptRoot 'work'
New-Item -ItemType Directory -Path $work -Force | Out-Null
function Get-GitSource($repository,$ref,$commit,$name){
 $destination=Join-Path $work $name
 if(Test-Path -LiteralPath $destination){Write-Output "Using existing source: $name";return}
 git clone --no-checkout $repository $destination
 if($LASTEXITCODE){throw "Clone failed: $name"}
 git -C $destination checkout $commit
 if($LASTEXITCODE){throw "Checkout failed: $name"}
 git -C $destination submodule update --init --recursive
 if($LASTEXITCODE){throw "Submodule checkout failed: $name"}
 $actual=git -C $destination rev-parse HEAD
 if($actual -ne $commit){throw "Source revision mismatch: $name"}
}
Get-GitSource 'https://github.com/moonlight-stream/moonlight-common-c.git' '' 'f900dd4767759c7b9d0e93bcea666b55c69ea62f' 'moonlight-common-c'
Get-GitSource 'https://github.com/Mbed-TLS/mbedtls.git' 'mbedtls-3.6.7' '068ff080b369adfac81509f9b57b2afabaf82dc5' 'mbedtls'
Get-GitSource 'https://github.com/xiph/opus.git' 'v1.5.2' 'ddbe48383984d56acd9e1ab6a090c54ca6b735a6' 'opus'
$pugi=Join-Path $work 'pugixml'
New-Item -ItemType Directory -Path $pugi -Force | Out-Null
$pins=Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'pins.json') | ConvertFrom-Json
foreach($item in $pins.pugixml.files){
 $file=Join-Path $pugi $item.name
 if(-not(Test-Path -LiteralPath $file)){Invoke-WebRequest -Uri $item.url -OutFile $file}
 if((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $item.sha256){throw "pugixml hash mismatch: $($item.name)"}
}
