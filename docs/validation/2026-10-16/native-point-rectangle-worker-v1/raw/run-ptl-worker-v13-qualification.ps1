$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\ptl-worker-v13-qualification"
if(Test-Path $out){throw 'Coordinator exists'};New-Item -ItemType Directory $out|Out-Null
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;desktop_changed=$false;tahoma_tested=$false;active_phase='waiting-for-component-comparison';phases=@()}
function Save-Report{$report|ConvertTo-Json -Depth 5|Set-Content "$out\report.json"};Save-Report
try{
 $deadline=[DateTime]::UtcNow.AddMinutes(40)
 while($true){$q=Get-Content "$vm\ptl-rectangle-capture-v2-qualification\report.json" -Raw|ConvertFrom-Json;if($q.completed){break};if([DateTime]::UtcNow -gt $deadline){throw 'Component comparison deadline'};Start-Sleep -Seconds 2}
 if(!$q.passed){throw 'Component comparison incomplete'}
 $q=Get-Content "$vm\native-desktop-admission-v58\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.cleanup_passed -or !$q.memory_integrity_after){throw 'Desktop cleanup incomplete'}
 if(@(Get-Process nr_registered*,nr_native*,nr_ptl*,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Graphics experiment active'}
 $python="$r\toolchains\openvino-2026.4.1\Scripts\python.exe"
 $component=Get-Content "$vm\ptl-point-rectangle-comparison-v2-full\report.json" -Raw|ConvertFrom-Json
 $pairs=@($component.cases|ForEach-Object {$_.pairs|Where-Object iteration -gt 0});$old=($pairs|Measure-Object baseline_full_ms -Sum).Sum;$new=($pairs|Measure-Object candidate_full_ms -Sum).Sum
 if(!$component.passed -or @($component.cases).Count -ne 37 -or $pairs.Count -ne 111 -or $new -ge $old*.8){throw 'Rectangle improvement gate not met'}
 $report.rectangle_warm_old_ms=$old;$report.rectangle_warm_new_ms=$new
 $report.active_phase='worker-build';Save-Report
 & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$vm\build-ptl-native-worker-v13.ps1" > "$out\build.stdout.log" 2> "$out\build.stderr.log";if($LASTEXITCODE){throw 'Worker build failed'}
 $hash=(Get-FileHash "$r\builds\ptl-native-worker-v13\Release\nr_registered_local_indexed_worker.exe").Hash.ToLowerInvariant();$report.worker_sha256=$hash
 foreach($stage in @('selected','full')){
  $report.active_phase=$stage;Save-Report
  & $python "$vm\qualify-ptl-native-worker-v13.py" $stage --worker-sha256 $hash > "$out\$stage.stdout.log" 2> "$out\$stage.stderr.log"
  if($LASTEXITCODE){throw "$stage worker qualification failed"};$q=Get-Content "$vm\ptl-native-worker-v13-$stage\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.passed){throw 'Worker proof incomplete'}
  $report.phases+=@{phase=$stage;passed=$true};Save-Report
 }
 $q=Get-Content "$vm\ptl-native-worker-v13-full\report.json" -Raw|ConvertFrom-Json;if(!$q.all_prior_worker_v12_outputs_preserved){throw 'Prior worker output audit failed'}
 $report.passed=$true
}catch{$report.error=$_.Exception.Message}
finally{$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out\report.json" -Raw;if(!$report.passed){exit 1}
