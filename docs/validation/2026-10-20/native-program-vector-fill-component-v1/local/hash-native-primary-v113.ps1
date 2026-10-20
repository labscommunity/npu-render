$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render"
foreach($trial in @('ptl-program-vector-fill-v1-qualification')){
 $q=Get-Content "$r\vm\$trial\report.json" -Raw|ConvertFrom-Json;if(!$q.completed){throw 'Cannot archive active experiment'}
}
function Item([string]$name,[string]$path,[string]$remote){
 if(!(Test-Path $path)){throw "Missing evidence: $name"}
 $stream=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
 try{$h=(Get-FileHash -InputStream $stream).Hash}finally{$stream.Dispose()}
 [ordered]@{source=$name;remote_source=$remote;bytes=(Get-Item $path).Length;sha256=$h}
}
$items=@(
foreach($name in @('ptl-program-vector-fill-v1-qualification','ptl-six-span-program-vector-fill-build-v1','ptl-six-span-program-vector-fill-v1')){
 if(!(Test-Path "$r\vm\$name")){continue}
 Get-ChildItem "$r\vm\$name" -File -Recurse|Where-Object {
  ($_.Extension -in @('.json','.jsonl','.log','.txt','.inf','.ps1','.py','.c','.cpp','.hpp')) -and
  ($_.Name -notlike 'client-*' -or ($_.Name -like '*.stderr.log' -and $_.Length -gt 0))
 }|ForEach-Object {
  if($_.Length -gt 30000000){throw 'Unexpected artifact size'}
  $n=$_.FullName.Substring(("$r\vm\").Length).Replace('\','/');Item $n $_.FullName ("vm/"+$n)
 }
}
foreach($name in @('ptl-six-span-program-vector-fill-v1.c','qualify-ptl-six-span-program-vector-fill-v1.py','run-ptl-program-vector-fill-v1-qualification.ps1','ptl-program-vector-fill-device-v1-source.json')){
 Item $name "$r\vm\$name" ("vm/"+$name)
}
);$items|ConvertTo-Json -Depth 5
