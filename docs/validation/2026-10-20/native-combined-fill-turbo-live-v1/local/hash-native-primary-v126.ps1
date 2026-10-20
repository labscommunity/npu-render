$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render"
foreach($trial in @('ptl-combined-desktop-v17','native-live-v65','native-desktop-admission-v75','combined-desktop-ready-v1')){
 $q=Get-Content "$r\vm\$trial\report.json" -Raw|ConvertFrom-Json;if(!$q.completed){throw 'Cannot archive active experiment'}
}
function Item([string]$name,[string]$path,[string]$remote){
 if(!(Test-Path $path)){throw "Missing evidence: $name"}
 $stream=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
 try{$h=(Get-FileHash -InputStream $stream).Hash}finally{$stream.Dispose()}
 [ordered]@{source=$name;remote_source=$remote;bytes=(Get-Item $path).Length;sha256=$h}
}
$items=@(
foreach($name in @('ptl-combined-desktop-v17','native-registration-v54','ptl-native-live-v29','native-live-v65','native-desktop-admission-v75','native-topology-v132','native-topology-v133','native-topology-v134','native-topology-primary-v51','native-latency-v29-analysis','native-presentation-v26-analysis','native-repeated-input-v12-analysis','combined-desktop-ready-v1','combined-fill-turbo-live-postmortem-v1')){
 if(!(Test-Path "$r\vm\$name")){continue}
 Get-ChildItem "$r\vm\$name" -File -Recurse|Where-Object {
  ($_.Extension -in @('.json','.jsonl','.log','.txt','.inf','.ps1','.py','.c','.cpp','.hpp')) -and
  ($_.Name -notlike 'client-*' -or ($_.Name -like '*.stderr.log' -and $_.Length -gt 0))
 }|ForEach-Object {
  if($_.Length -gt 30000000){throw 'Unexpected artifact size'}
  $n=$_.FullName.Substring(("$r\vm\").Length).Replace('\','/');Item $n $_.FullName ("vm/"+$n)
 }
}
foreach($name in @('run-native-registration-v54.ps1','recover-native-registration-v54.ps1','qualify-native-registered-v54.py','native_shared_app_v21.py','native_desktop_bootstrap_v49.py','run-native-desktop-v75.ps1','recover-native-desktop-v75.ps1','run-native-live-v65.ps1','start-native-topology-v132.ps1','start-native-topology-v134.ps1','dismiss-native-saver-v40.ps1','run-ptl-native-live-v29.ps1','analyze-native-latency-v27.py','analyze-native-presentation-v26.py','run-ptl-combined-desktop-v17.ps1','start-native-topology-v133.ps1','native_shared_correlation_v2.py','native_provider_generation_audit_v1.py','native-registration-v54-source.json','native-desktop-v75-source.json','ptl-combined-fill-turbo-live-v1-source.json','run-combined-desktop-ready-v1.ps1','start-combined-desktop-ready-v1.ps1','analyze-repeated-input-v2.py','pixel_observation_match_v1.py','presentation_counter_match_v1.py')){
 Item $name "$r\vm\$name" ("vm/"+$name)
}
);$items|ConvertTo-Json -Depth 5
