$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\ptl-worker-v11-qualification-v2"
if(Test-Path $out){throw 'Coordinator exists'};New-Item -ItemType Directory $out|Out-Null
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;desktop_changed=$false;tahoma_tested=$false;phases=@()}
function Save-Report{$report|ConvertTo-Json -Depth 5|Set-Content "$out\report.json"};Save-Report
try{
 $q=Get-Content "$vm\native-desktop-admission-v56\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.cleanup_passed -or !$q.memory_integrity_after){throw 'Desktop cleanup incomplete'}
 $q=Get-Content "$vm\ptl-complex-reference-check-v2\report.json" -Raw|ConvertFrom-Json;if(!$q.completed){throw 'Shader diagnostic active'}
 if(@(Get-Process nr_registered*,nr_native*,nr_ptl*,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Graphics experiment active'}
 $python="$r\toolchains\openvino-2026.4.1\Scripts\python.exe";$hash='c445ff2bb154e879609933508187af886100062ee60ee29fd1467f776316f12f'
 foreach($stage in @('selected','full')){
  $report.active_phase=$stage;Save-Report
  & $python "$vm\qualify-ptl-native-worker-v11-v2.py" $stage --worker-sha256 $hash > "$out\$stage.stdout.log" 2> "$out\$stage.stderr.log"
  if($LASTEXITCODE){throw "$stage worker qualification failed"};$proofPath=if($stage -eq 'selected'){"$vm\ptl-native-worker-v11-validation-v2-selected\report.json"}else{"$vm\ptl-native-worker-v11-full\report.json"};$q=Get-Content $proofPath -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.passed){throw 'Worker proof incomplete'}
  $report.phases+=@{phase=$stage;passed=$true};Save-Report
 }
 $report.active_phase='independent-audit';Save-Report
 & $python "$vm\audit-ptl-worker-v11-replay.py" > "$out\audit.stdout.log" 2> "$out\audit.stderr.log";if($LASTEXITCODE){throw 'Independent audit failed'}
 $q=Get-Content "$vm\ptl-native-worker-v11-audit\report.json" -Raw|ConvertFrom-Json;if(!$q.passed -or $q.comparisons -ne 972){throw 'Audit incomplete'}
 $report.phases+=@{phase='independent-audit';passed=$true};$report.passed=$true
}catch{$report.error=$_.Exception.Message}
finally{$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out\report.json" -Raw;if(!$report.passed){exit 1}
