$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$prior="$vm\ptl-worker-v19-qualification-v2";$out="$vm\ptl-worker-v19-qualification-v3"
if(Test-Path $out){throw 'Recovery coordinator exists'}
if((Get-Item "$prior\report.json").Length -ne 0){throw 'Interrupted report is not empty'}
if(@(Get-Process nr_registered*,nr_native*,nr_ptl*,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Graphics experiment active'}
if(Test-Path "$vm\ptl-native-worker-v19-full"){throw 'Full replay already exists'}
$q=Get-Content "$vm\ptl-native-worker-v19-selected\report.json" -Raw|ConvertFrom-Json
$hash='d4ddc1238bf91e94190b873f7f985def21c64c85f57fde18b6456f60c175f96c'
if(!$q.completed -or !$q.passed -or $q.worker_sha256 -ne $hash -or (Get-FileHash "$r\builds\ptl-native-worker-v19\Release\nr_registered_local_indexed_worker.exe").Hash -ne $hash){throw 'Selected replay identity mismatch'}
New-Item -ItemType Directory $out|Out-Null
Copy-Item "$prior\report.json" "$out\interrupted-empty-report.json"
. ([scriptblock]::Create([IO.File]::ReadAllText("$vm\atomic_lab_report_v1.ps1")))
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;desktop_changed=$false;tahoma_tested=$false;active_phase='full';worker_sha256=$hash;resumed_from='ptl-worker-v19-qualification-v2';prior_error='Set-Content: Stream was not readable after successful selected replay';empty_prior_report_retained=$true;atomic_report_helper_sha256=(Get-FileHash "$vm\atomic_lab_report_v1.ps1").Hash;phases=@(@{phase='selected';passed=$true;reused_identical_binary=$true})}
function Save-Report {Write-NrLabReport "$out\report.json" $report;Write-NrLabReport "$prior\report.json" $report}
Save-Report
try{
 $python="$r\toolchains\openvino-2026.4.1\Scripts\python.exe"
 $child=Start-Process $python -ArgumentList @(('"'+$vm+'\qualify-ptl-native-worker-v19-v2.py"'),'full','--worker-sha256',$hash) -PassThru -NoNewWindow -RedirectStandardOutput "$out\full.stdout.log" -RedirectStandardError "$out\full.stderr.log"
 try{if(!$child.WaitForExit(2000000)){$child.Kill();$child.WaitForExit();throw 'Full replay deadline'};$child.Refresh();if($child.ExitCode){throw 'Full replay failed'}}finally{$child.Dispose()}
 $q=Get-Content "$vm\ptl-native-worker-v19-full\report.json" -Raw|ConvertFrom-Json
 if(!$q.completed -or !$q.passed -or !$q.all_prior_worker_v15_outputs_preserved -or !$q.all_prior_worker_v18_outputs_preserved -or $q.worker_sha256 -ne $hash){throw 'Full replay proof incomplete'}
 $report.phases+=@{phase='full';passed=$true};$report.passed=$true
}catch{$report.error=$_.Exception.Message}
finally{$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out\report.json" -Raw;if(!$report.passed){exit 1}
