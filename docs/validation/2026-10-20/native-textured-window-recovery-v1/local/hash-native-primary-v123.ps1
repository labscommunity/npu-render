$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render"
foreach($trial in @('ptl-textured-windows-v1-qualification')){
 $q=Get-Content "$r\vm\$trial\report.json" -Raw|ConvertFrom-Json;if(!$q.completed){throw 'Cannot archive active experiment'}
}
function Item([string]$name,[string]$path,[string]$remote){
 if(!(Test-Path $path)){throw "Missing evidence: $name"}
 $stream=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
 try{$h=(Get-FileHash -InputStream $stream).Hash}finally{$stream.Dispose()}
 [ordered]@{source=$name;remote_source=$remote;bytes=(Get-Item $path).Length;sha256=$h}
}
$items=@(
foreach($name in @('ptl-textured-windows-v1-qualification','ptl-textured-windows-v1')){
 if(!(Test-Path "$r\vm\$name")){continue}
 Get-ChildItem "$r\vm\$name" -File -Recurse|Where-Object {
  ($_.Extension -in @('.json','.jsonl','.log','.txt','.inf','.ps1','.py','.c','.cpp','.hpp')) -and
  ($_.Name -notlike 'client-*' -or ($_.Name -like '*.stderr.log' -and $_.Length -gt 0))
 }|ForEach-Object {
  if($_.Length -gt 30000000){throw 'Unexpected artifact size'}
  $n=$_.FullName.Substring(("$r\vm\").Length).Replace('\','/');Item $n $_.FullName ("vm/"+$n)
 }
}
foreach($name in @('ptl_textured_windows_v1.py','ptl_textured_batch_fixtures_v1.py','qualify-ptl-textured-windows-v1.py','run-ptl-textured-windows-v1.ps1','start-textured-windows-v1.ps1','ptl-textured-windows-v1-source.json','textured-windows-v1-task.xml','textured-windows-v1-task-start.json')){
 Item $name "$r\vm\$name" ("vm/"+$name)
}
);$items|ConvertTo-Json -Depth 5
