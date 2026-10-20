$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\ptl-worker-v20-qualification"
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
 $q=Get-Content "$vm/npu-tensor-memory-v2/report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.passed -or !$q.results.plugin_input.memory.write_combined){throw 'Memory-access hypothesis not established'}
 $q=Get-Content "$vm/physical-gpu-desktop-v2/report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.cleanup_passed -or !$q.memory_integrity_after){throw 'Actual GPU trial cleanup incomplete'}
 if(@(Get-Process nr_registered*,nr_native*,nr_ptl*,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Another graphics experiment is active'}
 Invoke-Step 'build' 'powershell.exe' ('-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "'+$vm+'\build-ptl-native-worker-v20.ps1"') 900
 $b=Get-Content "$r/builds/ptl-native-worker-v20/build-report.json" -Raw|ConvertFrom-Json;if(!$b.clean_build){throw 'Diagnostic build incomplete'};$hash=$b.binary_sha256.ToLower();$report.worker_sha256=$hash;Save-Report
 foreach($stage in @('selected','full')){
  Invoke-Step $stage "$r/toolchains/openvino-2026.4.1/Scripts/python.exe" ('"'+$vm+'\qualify-ptl-native-worker-v20.py" '+$stage+' --worker-sha256 '+$hash) 2100
  $q=Get-Content "$vm/ptl-native-worker-v20-$stage/report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.passed){throw "$stage proof incomplete"}
 }
 Invoke-Step 'matched-abba' "$r/toolchains/openvino-2026.4.1/Scripts/python.exe" ('"'+$vm+'\qualify-host-tensor-comparison-v1.py"') 5400
 $q=Get-Content "$vm/host-tensor-comparison-v1/report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.passed -or $q.summary.pairs -ne 1944){throw 'Matched comparison incomplete'}
 $report.comparison=$q.summary
 $report.passed=$true
}catch{$report.error=$_.Exception.Message}
finally{$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out/report.json" -Raw;if(!$report.passed){exit 1}
