$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\turbo-render-provider-recovery-v1-qualification"
if(Test-Path $out){throw 'Diagnostic coordinator exists'};New-Item -ItemType Directory $out|Out-Null
. ([scriptblock]::Create([IO.File]::ReadAllText("$vm\atomic_lab_report_v1.ps1")))
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;desktop_changed=$false;result_cache_implemented=$false;tahoma_tested=$false;active_phase='verify-graphics-cleanup';steps=@()}
function Save-Report{Write-NrLabReport "$out/report.json" $report};Save-Report
function Invoke-Step($name,$exe,$arguments,$seconds){
 $report.active_phase=$name;Save-Report
 $p=[Diagnostics.Process]::new();$p.StartInfo.FileName=$exe;$p.StartInfo.Arguments=$arguments;$p.StartInfo.UseShellExecute=$false;$p.StartInfo.CreateNoWindow=$true;$p.StartInfo.RedirectStandardOutput=$true;$p.StartInfo.RedirectStandardError=$true
 try{if(!$p.Start()){throw 'Process did not start'};$stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync();if(!$p.WaitForExit($seconds*1000)){$p.Kill();$p.WaitForExit();throw "$name deadline"};$code=$p.ExitCode;[IO.File]::WriteAllText("$out/$name.stdout.log",$stdout.Result);[IO.File]::WriteAllText("$out/$name.stderr.log",$stderr.Result);$report.steps+=@{name=$name;exit_code=$code};Save-Report;if($code -ne 0){throw "$name failed with exit $code"}}finally{$p.Dispose()}
}
try{
 $report.active_phase='waiting-for-combined-worker-comparison';Save-Report
 $deadline=[DateTime]::UtcNow.AddMinutes(180)
 foreach($name in @('ptl-worker-v26-qualification','ptl-program-vector-fill-v1-qualification')){
  while($true){$q=Get-Content "$vm/$name/report.json" -Raw|ConvertFrom-Json;if($q.completed){break};if([DateTime]::UtcNow -gt $deadline){throw 'Preceding experiment deadline'};Start-Sleep -Seconds 3}
 }
 $q=Get-Content "$vm/ptl-program-vector-fill-v1-qualification/report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.passed){throw 'Inspect preceding kernel experiment before NPU recovery tests'}
 $preceding='native-desktop-admission-v72'
 $q=Get-Content "$vm/$preceding/report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.cleanup_passed -or !$q.memory_integrity_after){throw 'Preceding desktop cleanup incomplete'}
 if(@(Get-Process nr_registered*,nr_native*,nr_ptl*,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Another graphics experiment is active'}


 foreach($name in @('native-registration-stage-v20','native-registration-v47','native-desktop-admission-v68','native-desktop-admission-v69','native-desktop-admission-v72')){
  if(Test-Path "$vm/$name/report.json"){$q=Get-Content "$vm/$name/report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.cleanup_passed -or !$q.memory_integrity_after){throw 'Latest diagnostic cleanup incomplete'}}
 }
 $q=Get-Content "$vm/ptl-worker-v26-qualification/report.json" -Raw|ConvertFrom-Json
 if(!$q.completed -or !$q.passed){throw 'Combined renderer correctness and comparison incomplete'}
 Invoke-Step 'provider-faults' "$r/toolchains/openvino-2026.4.1/Scripts/python.exe" ('"'+$vm+'\qualify-turbo-render-provider-recovery-v1.py"') 660
 $q=Get-Content "$vm/turbo-render-provider-recovery-v1/report.json" -Raw|ConvertFrom-Json
 if(!$q.completed -or !$q.passed -or @($q.phases).Count -ne 2){throw 'Targeted recovery incomplete'}
 $report.worker_sha256=$q.worker_sha256
 $report.passed=$true
}catch{$report.error=$_.Exception.Message}
finally{$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out/report.json" -Raw;if(!$report.passed){exit 1}
