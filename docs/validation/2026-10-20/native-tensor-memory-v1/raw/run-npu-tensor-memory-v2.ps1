$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\npu-tensor-memory-v2-qualification"
if(Test-Path $out){throw 'Memory diagnostic exists'};New-Item -ItemType Directory $out|Out-Null
. ([scriptblock]::Create([IO.File]::ReadAllText("$vm\atomic_lab_report_v1.ps1")))
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;active_phase='waiting-for-gpu-live';npu_inferences=0;desktop_changed=$false;tahoma_tested=$false}
function Save-Report{Write-NrLabReport "$out/report.json" $report};Save-Report
try{
 $deadline=[DateTime]::UtcNow.AddMinutes(80)
 while($true){$q=Get-Content "$vm/physical-gpu-live-v1-qualification/report.json" -Raw|ConvertFrom-Json;if($q.completed){break};if([DateTime]::UtcNow -gt $deadline){throw 'GPU live completion deadline'};Start-Sleep -Seconds 3}
 $transaction="$vm/physical-gpu-desktop-v2"
 if(Test-Path "$transaction/report.json"){
  $q=Get-Content "$transaction/report.json" -Raw|ConvertFrom-Json
  if(!$q.completed -or !$q.cleanup_passed){
   if(!(Test-Path "$transaction/ownership.json")){throw 'GPU cleanup ownership absent'}
   & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$transaction/recover.ps1" -Root $transaction *> "$out/recovery.log"
   if($LASTEXITCODE){throw 'GPU recovery failed'};$report.external_recovery_requested=$true
   $recovery=Get-Content "$transaction/recovery-report.json" -Raw|ConvertFrom-Json;if(!$recovery.passed){throw 'GPU recovery proof incomplete'}
  }
 }
 if(@(Get-Process nr_registered*,nr_native*,nr_ptl*,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Another graphics experiment active'}
 $report.active_phase='memory-access-diagnostic';Save-Report
 $p=[Diagnostics.Process]::new();$p.StartInfo.FileName="$r/toolchains/openvino-2026.4.1/Scripts/python.exe";$p.StartInfo.Arguments='"'+$vm+'\probe-npu-tensor-memory-v2.py"';$p.StartInfo.UseShellExecute=$false;$p.StartInfo.CreateNoWindow=$true;$p.StartInfo.RedirectStandardOutput=$true;$p.StartInfo.RedirectStandardError=$true
 try{if(!$p.Start()){throw 'Memory probe did not start'};$stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync();if(!$p.WaitForExit(120000)){$p.Kill();$p.WaitForExit();throw 'Memory probe deadline'};$report.exit_code=$p.ExitCode;[IO.File]::WriteAllText("$out/stdout.log",$stdout.Result);[IO.File]::WriteAllText("$out/stderr.log",$stderr.Result);if($report.exit_code -ne 0){throw 'Memory probe failed'}}finally{$p.Dispose()}
 $q=Get-Content "$vm/npu-tensor-memory-v2/report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.passed -or $q.npu_inferences -ne 0){throw 'Memory diagnostic proof incomplete'};$report.passed=$true
}catch{$report.error=$_.Exception.Message}
finally{$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out/report.json" -Raw;if(!$report.passed){exit 1}
