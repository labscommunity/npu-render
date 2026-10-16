$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\ptl-point-desktop-v1"
if(Test-Path $out){throw 'Desktop coordinator exists'};New-Item -ItemType Directory $out|Out-Null
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;full_desktop_offload=$false;tahoma_tested=$false;active_phase='waiting-for-device-comparisons';phases=@()}
function Save-Report{$report|ConvertTo-Json -Depth 5|Set-Content "$out\report.json"};Save-Report
try{
 $deadline=[DateTime]::UtcNow.AddMinutes(40)
 while($true){$q=Get-Content "$vm\ptl-complex-after-worker-v1\report.json" -Raw|ConvertFrom-Json;if($q.completed){break};if([DateTime]::UtcNow -gt $deadline){throw 'Device comparisons wait deadline'};Start-Sleep -Seconds 2}
 if(!$q.passed){throw 'Inspect shader comparison failure before desktop mutation'}
 $q=Get-Content "$vm\ptl-native-worker-v11-full\report.json" -Raw|ConvertFrom-Json;$audit=Get-Content "$vm\ptl-native-worker-v11-audit\report.json" -Raw|ConvertFrom-Json
 if(!$q.passed -or !$q.completed -or !$q.program_tickets_matched -or !$q.geometry_tickets_matched -or !$audit.passed -or $audit.comparisons -ne 972){throw 'Worker proof incomplete'}
 if(@(Get-Process nr_registered*,nr_native*,nr_ptl*,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Another graphics experiment active'}
 $report.active_phase='registered-d3d';Save-Report
 & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$vm\run-native-registration-v37.ps1" -Mode render > "$out\registration.stdout.log" 2> "$out\registration.stderr.log";if($LASTEXITCODE){throw 'Registered qualification failed'}
 $q=Get-Content "$vm\native-registration-v37\report.json" -Raw|ConvertFrom-Json;if(!$q.passed -or !$q.cleanup_passed -or !$q.memory_integrity_after){throw 'Registered proof incomplete'}
 $report.phases+=@{phase='registered-d3d';passed=$true;cleanup_passed=$true};$report.active_phase='finite-live';Save-Report
 & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$vm\run-ptl-native-live-v11.ps1" > "$out\live.stdout.log" 2> "$out\live.stderr.log";if($LASTEXITCODE){throw 'Finite desktop qualification failed'}
 $q=Get-Content "$vm\ptl-native-live-v11\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.passed){throw 'Live proof incomplete'}
 $report.phases+=@{phase='finite-live';passed=$true};$report.active_phase='timing-analysis';Save-Report
 & "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" "$vm\analyze-native-latency-v7.py" --root $vm --output "$vm\native-latency-v9-analysis" > "$out\analysis.stdout.log" 2> "$out\analysis.stderr.log";if($LASTEXITCODE){throw 'Causal analysis failed'}
 & "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" "$vm\analyze-native-presentation-v4.py" --root $vm --output "$vm\native-presentation-v4-analysis" > "$out\presentation.stdout.log" 2> "$out\presentation.stderr.log";if($LASTEXITCODE){throw 'Presentation analysis failed'}
 $report.phases+=@{phase='timing-analysis';passed=$true};$report.passed=$true
}catch{$report.error=$_.Exception.Message}
finally{$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out\report.json" -Raw;if(!$report.passed){exit 1}
