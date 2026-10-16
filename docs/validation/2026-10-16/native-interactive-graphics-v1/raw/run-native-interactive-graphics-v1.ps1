$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\native-interactive-graphics-v1"
if(Test-Path $out){throw 'Interactive graphics coordinator exists'};New-Item -ItemType Directory $out|Out-Null
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;active_phase='waiting-for-live-cleanup';full_desktop_offload=$false;tahoma_tested=$false;phases=@()}
function Save-Report{$report|ConvertTo-Json -Depth 6|Set-Content "$out\report.json"};Save-Report
function Run-Step([string]$phase,[string]$script,[string[]]$arguments=@()){
 $report.active_phase=$phase;Save-Report
 & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$vm\$script" @arguments > "$out\$phase.stdout.log" 2> "$out\$phase.stderr.log"
 if($LASTEXITCODE){throw "Phase failed: $phase"};$report.phases+=@{phase=$phase;passed=$true};Save-Report
}
try{
 $deadline=[DateTime]::UtcNow.AddMinutes(20)
 while($true){$q=Get-Content "$vm\ptl-long-desktop-v1\report.json" -Raw|ConvertFrom-Json;if($q.completed){break};if([DateTime]::UtcNow -ge $deadline){throw 'Prior desktop deadline exceeded'};Start-Sleep -Seconds 2}
 if(!$q.passed){throw 'Live trial failed; inspect before changing the application driver'}
 $q=Get-Content "$vm\native-desktop-admission-v54\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.cleanup_passed -or !$q.memory_integrity_after){throw 'Live cleanup not qualified'}
 if(@(Get-Process nr_registered*,nr_native*,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Graphics process still active'}
 Run-Step 'worker-build' 'build-ptl-native-worker-v10.ps1'
 Run-Step 'driver-build' 'build-native-auto-umd-v2.ps1'
 Run-Step 'package' 'package-native-driver-v19.ps1'
 Run-Step 'stage' 'run-native-registration-v35.ps1' @('-Mode','stage')
 $q=Get-Content "$vm\native-registration-stage-v19\report.json" -Raw|ConvertFrom-Json;if(!$q.passed -or !$q.cleanup_passed){throw 'Stage cleanup not qualified'}
 Run-Step 'registered-interactive' 'run-native-registration-v35.ps1' @('-Mode','render')
 $q=Get-Content "$vm\native-registration-v35\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.passed -or !$q.cleanup_passed -or !$q.memory_integrity_after){throw 'Interactive driver cleanup incomplete'}
 $a=Get-Content "$vm\native-registration-v35\qualification\report.json" -Raw|ConvertFrom-Json
 if(!$a.passed -or !$a.automatic_admission -or $a.consumer_elevated -or $a.consumer_session -lt 1 -or $a.npu_operation_receipts -ne 8 -or !$a.owned_process_and_token_acls_restored_exactly -or !$a.interactive_task_removed){throw 'Interactive NPU graphics proof incomplete'}
 $report.passed=$true;$report.automatic_nonadmin_registered_graphics=$true;$report.npu_operations=8;$report.cleanup_passed=$true
}catch{$report.error=$_.Exception.Message}
finally{$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out\report.json" -Raw;if(!$report.passed){exit 1}
