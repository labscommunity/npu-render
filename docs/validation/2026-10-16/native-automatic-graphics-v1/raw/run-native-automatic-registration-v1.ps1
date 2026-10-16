$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\native-automatic-registration-v1"
if(Test-Path $out){throw 'Automatic registration coordinator already exists'};New-Item -ItemType Directory $out|Out-Null
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;active_phase='waiting-for-live-timing-recovery';full_desktop_offload=$false;tahoma_tested=$false}
function Save-Report{$report|ConvertTo-Json -Depth 5|Set-Content "$out\report.json"};Save-Report
try{
 $deadline=[DateTime]::UtcNow.AddMinutes(16)
 while($true){$q=Get-Content "$vm\ptl-native-live-v6\report.json" -Raw|ConvertFrom-Json;if($q.completed){break};if([DateTime]::UtcNow -ge $deadline){throw 'Prior live trial deadline exceeded'};Start-Sleep -Seconds 3}
 if(!$q.passed){throw 'Prior live trial failed; inspect before installing another candidate'}
 $q=Get-Content "$vm\native-desktop-admission-v52\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.cleanup_passed){throw 'Live desktop recovery not complete'}
 $q=Get-Content "$vm\native-auto-broker-v1-qualification\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.passed){throw 'Broker not qualified'}
 $report.active_phase='automatic-registered-d3d';Save-Report
 & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$vm\run-native-registration-v31.ps1" -Mode render > "$out\registration.stdout.log" 2> "$out\registration.stderr.log"
 if($LASTEXITCODE){throw 'Automatic registered D3D qualification failed'}
 $q=Get-Content "$vm\native-registration-v31\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.passed -or !$q.cleanup_passed){throw 'Registration/recovery evidence incomplete'}
 $a=Get-Content "$vm\native-registration-v31\qualification\report.json" -Raw|ConvertFrom-Json;if(!$a.passed -or !$a.automatic_admission -or $a.consumer_started_suspended -or !$a.automatic_broker_cleanup_passed -or $a.npu_operation_receipts -ne 8){throw 'Automatic NPU execution evidence incomplete'}
 $report.passed=$true;$report.automatic_registered_graphics=$true;$report.npu_operations=8;$report.cleanup_passed=$true
}catch{$report.error=$_.Exception.Message}
finally{$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out\report.json";if(!$report.passed){exit 1}
