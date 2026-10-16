$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$py="$r\toolchains\openvino-2026.4.1\Scripts\python.exe"
foreach($stage in @('selected','full')){
 & $py "$vm\qualify-ptl-native-worker-v8.py" $stage *> "$vm\run-ptl-worker-v8-$stage.log"
 if($LASTEXITCODE){Get-Content "$vm\run-ptl-worker-v8-$stage.log" -Tail 3;throw "Worker qualification failed $stage"}
 Get-Content "$vm\ptl-native-worker-v8-$stage\report.json" -Raw|ConvertFrom-Json|Select-Object completed,passed,program_accepted,program_imports,geometry_accepted,masked_accepted,timeline_records|ConvertTo-Json
}
& $py "$vm\audit-ptl-worker-v8-replay.py" *> "$vm\run-ptl-worker-v8-audit.log"
if($LASTEXITCODE){Get-Content "$vm\run-ptl-worker-v8-audit.log";throw 'Independent worker audit failed'}
Get-Content "$vm\run-ptl-worker-v8-audit.log"
