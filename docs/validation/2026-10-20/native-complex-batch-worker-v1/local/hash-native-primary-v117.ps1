$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render"
foreach($trial in @('ptl-worker-v29-restart-v1-qualification')){
 $q=Get-Content "$r\vm\$trial\report.json" -Raw|ConvertFrom-Json;if(!$q.completed){throw 'Cannot archive active experiment'}
}
function Item([string]$name,[string]$path,[string]$remote){
 if(!(Test-Path $path)){throw "Missing evidence: $name"}
 $stream=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
 try{$h=(Get-FileHash -InputStream $stream).Hash}finally{$stream.Dispose()}
 [ordered]@{source=$name;remote_source=$remote;bytes=(Get-Item $path).Length;sha256=$h}
}
$items=@(
foreach($name in @('ptl-worker-v29-restart-v1-qualification','ptl-native-worker-v29-restart-v1-selected','ptl-native-worker-v29-restart-v1-full','complex-batch-comparison-v3','complex-batch-provider-recovery-v3','ptl-post-update-health-v1')){
 if(!(Test-Path "$r\vm\$name")){continue}
 Get-ChildItem "$r\vm\$name" -File -Recurse|Where-Object {
  ($_.Extension -in @('.json','.jsonl','.log','.txt','.inf','.ps1','.py','.c','.cpp','.hpp')) -and
  ($_.Name -notlike 'client-*' -or ($_.Name -like '*.stderr.log' -and $_.Length -gt 0))
 }|ForEach-Object {
  if($_.Length -gt 30000000){throw 'Unexpected artifact size'}
  $n=$_.FullName.Substring(("$r\vm\").Length).Replace('\','/');Item $n $_.FullName ("vm/"+$n)
 }
}
foreach($name in @('qualify-ptl-native-worker-v29-restart-v1.py','qualify-complex-batch-comparison-v3.py','qualify-complex-batch-provider-recovery-v3.py','verify-ptl-post-update-health-v1.py','run-ptl-worker-v29-restart-v1-qualification.ps1','complex-batch-restart-v1-source.json','start-complex-batch-restart-v1.ps1','complex-batch-restart-v1-task.xml','complex-batch-restart-v1-task-start.json','capture-post-update-context-v1.ps1','post-update-render-context-v1.json')){
 Item $name "$r\vm\$name" ("vm/"+$name)
}
foreach($version in @(29)){foreach($file in @('build-report.json','registered-build-report.json')){
 $name="ptl-native-worker-v$version"
 if(Test-Path "$r\builds\$name\$file"){Item "build-$name/$file" "$r\builds\$name\$file" "builds/$name/$file"}
}}
);$items|ConvertTo-Json -Depth 5
