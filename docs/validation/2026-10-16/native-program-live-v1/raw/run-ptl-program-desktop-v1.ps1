$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\ptl-program-desktop-v1"
if(Test-Path $out){throw 'Desktop coordinator already exists'};New-Item -ItemType Directory $out|Out-Null
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;full_desktop_offload=$false;tahoma_tested=$false;active_phase='waiting-for-worker-audit';phases=@()}
function Save-Report{$report|ConvertTo-Json -Depth 5|Set-Content "$out\report.json"};Save-Report
try{
 $deadline=[DateTime]::UtcNow.AddMinutes(15)
 do{
  $q=$null;try{$q=Get-Content "$vm\ptl-native-worker-v8-full\report.json" -Raw|ConvertFrom-Json}catch{}
  if($q.completed -and !$q.passed){throw 'Full worker qualification failed'}
  $audit=$null;try{$audit=Get-Content "$vm\ptl-native-worker-v8-audit\report.json" -Raw|ConvertFrom-Json}catch{}
  if($audit.completed){if(!$audit.passed -or $audit.comparisons -ne 972){throw 'Independent worker audit failed'};break}
  if([DateTime]::UtcNow -gt $deadline){throw 'Worker audit wait deadline'};Start-Sleep -Seconds 2
 }while($true)
 if(!$q.completed -or !$q.passed -or $q.program_accepted -ne 376 -or !$q.program_sequences_matched){throw 'Worker proof incomplete'}
 $report.active_phase='registered-d3d';Save-Report
 & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$vm\run-native-registration-v33.ps1" -Mode render > "$out\registration.stdout.log" 2> "$out\registration.stderr.log"
 if($LASTEXITCODE){throw 'Registered graphics qualification failed'}
 $q=Get-Content "$vm\native-registration-v33\report.json" -Raw|ConvertFrom-Json;if(!$q.passed -or !$q.cleanup_passed -or !$q.memory_integrity_after){throw 'Registered graphics proof incomplete'}
 $report.phases+=@{phase='registered-d3d';passed=$true;cleanup_passed=$true};$report.active_phase='finite-live';Save-Report
 & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$vm\run-ptl-native-live-v7.ps1" > "$out\live.stdout.log" 2> "$out\live.stderr.log"
 if($LASTEXITCODE){throw 'Finite live qualification failed'}
 $q=Get-Content "$vm\ptl-native-live-v7\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.passed){throw 'Finite live proof incomplete'}
 $report.phases+=@{phase='finite-live';passed=$true};$report.active_phase='causal-timing';Save-Report
 & "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" "$vm\analyze-native-latency-v4.py" --root $vm --output "$vm\native-latency-v6-analysis" > "$out\analysis.stdout.log" 2> "$out\analysis.stderr.log"
 if($LASTEXITCODE){throw 'Causal timing analysis failed'}
 $report.phases+=@{phase='causal-timing';passed=$true};$report.passed=$true
}catch{$report.error=$_.Exception.Message}
finally{$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out\report.json" -Raw;if(!$report.passed){exit 1}
