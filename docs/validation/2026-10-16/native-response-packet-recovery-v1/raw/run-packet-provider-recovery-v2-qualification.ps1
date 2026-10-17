$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\packet-provider-recovery-v2-qualification"
if(Test-Path $out){throw 'Coordinator exists'};New-Item -ItemType Directory $out|Out-Null
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;desktop_changed=$false;tahoma_tested=$false;active_phase='waiting-for-component-comparison';phases=@()}
function Save-Report{$report|ConvertTo-Json -Depth 5|Set-Content "$out\report.json"};Save-Report
try{
 $deadline=[DateTime]::UtcNow.AddMinutes(40)
 while($true){$q=Get-Content "$vm\ptl-worker-v19-qualification-v2\report.json" -Raw|ConvertFrom-Json;if($q.completed){break};if([DateTime]::UtcNow -gt $deadline){throw 'Component comparison deadline'};Start-Sleep -Seconds 2}
 if(!$q.passed){throw 'Component comparison incomplete'}
 $desktop=if(Test-Path "$vm\native-desktop-admission-v64\report.json"){'native-desktop-admission-v64'}else{'native-desktop-admission-v61'}
 $q=Get-Content "$vm\$desktop\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.cleanup_passed -or !$q.memory_integrity_after){throw 'Desktop cleanup incomplete'}
 if(@(Get-Process nr_registered*,nr_native*,nr_ptl*,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Graphics experiment active'}
 $python="$r\toolchains\openvino-2026.4.1\Scripts\python.exe"
 $report.active_phase='owned-provider-faults';Save-Report
 $child=Start-Process $python -ArgumentList @(('"'+$vm+'\qualify-packet-provider-recovery-v2.py"')) -PassThru -NoNewWindow -RedirectStandardOutput "$out\qualification.stdout.log" -RedirectStandardError "$out\qualification.stderr.log"
 try{if(!$child.WaitForExit(660000)){$child.Kill();$child.WaitForExit();throw 'Provider qualification deadline'};$child.Refresh();if($child.ExitCode){throw 'Provider fault qualification failed'}}finally{$child.Dispose()}
 $q=Get-Content "$vm\packet-provider-recovery-v2\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.passed){throw 'Recovery evidence incomplete'}
 $report.passed=$true
}catch{$report.error=$_.Exception.Message}
finally{$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out\report.json" -Raw;if(!$report.passed){exit 1}
