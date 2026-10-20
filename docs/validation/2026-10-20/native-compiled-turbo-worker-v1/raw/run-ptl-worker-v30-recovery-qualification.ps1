$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\ptl-worker-v30-recovery-qualification"
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
 $report.active_phase='waiting-for-comparison';Save-Report
 $deadline=[DateTime]::UtcNow.AddHours(3)
 while($true){
  $q=Get-Content "$vm/ptl-worker-v30-qualification/report.json" -Raw|ConvertFrom-Json
  if($q.completed){break}
  if([DateTime]::UtcNow -gt $deadline){throw 'Comparison deadline'}
  Start-Sleep -Seconds 3
 }
 if(!$q.passed){throw 'Compiled turbo qualification incomplete'}
 if(@(Get-Process nr_registered*,nr_native*,nr_ptl*,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Another graphics experiment is active'}
 $sources=Get-Content "$vm/compiled-turbo-recovery-v1-source.json" -Raw|ConvertFrom-Json
 foreach($f in $sources.psobject.Properties){if((Get-FileHash "$vm/$($f.Name)").Hash.ToLower() -ne $f.Value){throw 'Recovery source mismatch'}}
 Invoke-Step 'provider-faults' "$r/toolchains/openvino-2026.4.1/Scripts/python.exe" ('"'+$vm+'\qualify-compiled-turbo-provider-recovery-v1.py"') 660
 $q=Get-Content "$vm/compiled-turbo-provider-recovery-v1/report.json" -Raw|ConvertFrom-Json
 if(!$q.completed -or !$q.passed -or @($q.phases).Count -ne 2){throw 'Recovery incomplete'}
 $report.worker_sha256=$q.worker_sha256;$report.passed=$true
}catch{$report.error=$_.Exception.Message}
finally{$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out/report.json" -Raw;if(!$report.passed){exit 1}
