$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\host-tensor-provider-recovery-v1-qualification"
if(Test-Path $out){throw 'Coordinator exists'};New-Item -ItemType Directory $out|Out-Null
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;desktop_changed=$false;tahoma_tested=$false;active_phase='waiting-for-component-comparison';phases=@()}
. ([scriptblock]::Create([IO.File]::ReadAllText("$vm\atomic_lab_report_v1.ps1")))
function Save-Report{Write-NrLabReport "$out/report.json" $report};Save-Report
try{
 $deadline=[DateTime]::UtcNow.AddMinutes(120)
 while($true){$q=Get-Content "$vm\ptl-worker-v20-qualification\report.json" -Raw|ConvertFrom-Json;if($q.completed){break};if([DateTime]::UtcNow -gt $deadline){throw 'Component comparison deadline'};Start-Sleep -Seconds 2}
 if(!$q.passed){throw 'Component comparison incomplete'}
 $desktop='physical-gpu-desktop-v2'
 $q=Get-Content "$vm\$desktop\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.cleanup_passed -or !$q.memory_integrity_after){throw 'Desktop cleanup incomplete'}
 if(@(Get-Process nr_registered*,nr_native*,nr_ptl*,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Graphics experiment active'}
 $python="$r\toolchains\openvino-2026.4.1\Scripts\python.exe"
 $report.active_phase='owned-provider-faults';Save-Report
 $child=Start-Process $python -ArgumentList @(('"'+$vm+'\qualify-host-tensor-provider-recovery-v1.py"')) -PassThru -NoNewWindow -RedirectStandardOutput "$out\qualification.stdout.log" -RedirectStandardError "$out\qualification.stderr.log"
 try{[void]$child.Handle;if(!$child.WaitForExit(660000)){$child.Kill();$child.WaitForExit();throw 'Provider qualification deadline'};$child.Refresh();if($child.ExitCode){throw 'Provider fault qualification failed'}}finally{$child.Dispose()}
 $q=Get-Content "$vm\host-tensor-provider-recovery-v1\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.passed){throw 'Recovery evidence incomplete'}
 $report.passed=$true
}catch{$report.error=$_.Exception.Message}
finally{$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out\report.json" -Raw;if(!$report.passed){exit 1}
