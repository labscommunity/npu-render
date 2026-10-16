$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\ptl-rectangle-captures-after-worker-v2"
if(Test-Path $out){throw 'Coordinator exists'};New-Item -ItemType Directory $out|Out-Null
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;active_phase='waiting-for-worker';desktop_changed=$false;tahoma_tested=$false}
function Save-Report{$report|ConvertTo-Json -Depth 5|Set-Content "$out\report.json"};Save-Report
try{
 $deadline=[DateTime]::UtcNow.AddMinutes(60)
 while($true){$q=Get-Content "$vm\ptl-worker-v12-qualification-v2\report.json" -Raw|ConvertFrom-Json;if($q.completed){break};if([DateTime]::UtcNow -ge $deadline){throw 'Worker deadline exceeded'};Start-Sleep -Seconds 2}
 if(!$q.passed){throw 'Inspect preceding worker failure'}
 if(@(Get-Process nr_registered*,nr_native*,nr_ptl*,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Graphics experiment active'}
 $report.active_phase='host-build';Save-Report
 & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$vm\build-ptl-point-rectangle-integration-v1.ps1" > "$out\build.stdout.log" 2> "$out\build.stderr.log";if($LASTEXITCODE){throw 'Host comparison build failed'}
 $python="$r\toolchains\openvino-2026.4.1\Scripts\python.exe";$report.active_phase='structural-inspection';Save-Report
 & $python "$vm\inspect-ptl-point-rectangle-captures-v1.py" > "$out\inspection.stdout.log" 2> "$out\inspection.stderr.log";if($LASTEXITCODE){throw 'Capture inspection failed'}
 foreach($stage in @('selected','full')){
  $report.active_phase=$stage;Save-Report
  & $python "$vm\run-ptl-point-rectangle-compare-v1.py" $stage > "$out\$stage.stdout.log" 2> "$out\$stage.stderr.log";if($LASTEXITCODE){throw "$stage rectangle comparison failed"}
  $q=Get-Content "$vm\ptl-point-rectangle-comparison-v1-$stage\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.passed){throw 'Rectangle comparison proof incomplete'}
 }
 $report.passed=$true
}catch{$report.error=$_.Exception.Message}
finally{$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out\report.json" -Raw;if(!$report.passed){exit 1}
