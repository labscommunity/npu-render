$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\ptl-worker-v29-restart-v1-qualification"
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
 if(@(Get-Process nr_registered*,nr_native*,nr_ptl*,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Another graphics experiment is active'}
 $q=Get-Content "$vm/complex-batch-interruption-v1/report.json" -Raw|ConvertFrom-Json
 if($q.active_renderers -ne 0 -or $q.replay_rows -ne 55){throw 'Interruption evidence mismatch'}
 $boot=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().ToString('o')
 if($boot -ne $q.boot_utc){throw 'Another restart occurred; inspect machine first'}
 $report.boot_utc=$boot
 $sources=Get-Content "$vm/complex-batch-restart-v1-source.json" -Raw|ConvertFrom-Json
 foreach($f in $sources.psobject.Properties){if((Get-FileHash "$vm/$($f.Name)").Hash.ToLower() -ne $f.Value){throw 'Restart experiment source mismatch'}}
 $hash='68ee41f06082135693d0d8df748244d7ee1970030450e803cfbbcdbf5ef018a4'
 if((Get-FileHash "$r/builds/ptl-native-worker-v29/Release/nr_registered_local_indexed_worker.exe").Hash.ToLower() -ne $hash){throw 'Worker binary changed'}
 $b=Get-Content "$r/builds/ptl-native-worker-v29/build-report.json" -Raw|ConvertFrom-Json
 if(!$b.clean_build -or $b.binary_sha256.ToLower() -ne $hash){throw 'Prior build proof mismatch'}
 $report.worker_sha256=$hash;Save-Report
 Invoke-Step 'original-health' "$r/toolchains/openvino-2026.4.1/Scripts/python.exe" ('"'+$vm+'\verify-ptl-post-update-health-v1.py"') 120
 $q=Get-Content "$vm/ptl-post-update-health-v1/report.json" -Raw|ConvertFrom-Json
 if(!$q.completed -or !$q.passed -or @($q.checks).Count -ne 8){throw 'Original NPU health incomplete'}
 foreach($stage in @('selected','full')){
  Invoke-Step $stage "$r/toolchains/openvino-2026.4.1/Scripts/python.exe" ('"'+$vm+'\qualify-ptl-native-worker-v29-restart-v1.py" '+$stage+' --worker-sha256 '+$hash) 2100
  $q=Get-Content "$vm/ptl-native-worker-v29-restart-v1-$stage/report.json" -Raw|ConvertFrom-Json
  if(!$q.completed -or !$q.passed){throw "$stage proof incomplete"}
 }
 Invoke-Step 'matched-abba' "$r/toolchains/openvino-2026.4.1/Scripts/python.exe" ('"'+$vm+'\qualify-complex-batch-comparison-v3.py"') 5400
 $q=Get-Content "$vm/complex-batch-comparison-v3/report.json" -Raw|ConvertFrom-Json
 if(!$q.completed -or !$q.passed -or $q.summary.pairs -ne 1944){throw 'Matched comparison incomplete'}
 $report.comparison=$q.summary;Save-Report
 Invoke-Step 'provider-faults' "$r/toolchains/openvino-2026.4.1/Scripts/python.exe" ('"'+$vm+'\qualify-complex-batch-provider-recovery-v3.py"') 660
 $q=Get-Content "$vm/complex-batch-provider-recovery-v3/report.json" -Raw|ConvertFrom-Json
 if(!$q.completed -or !$q.passed -or @($q.phases).Count -ne 2){throw 'Recovery incomplete'}
 if((Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().ToString('o') -ne $boot){throw 'Machine restarted during comparison'}
 $report.passed=$true
}catch{$report.error=$_.Exception.Message}
finally{$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out/report.json" -Raw;if(!$report.passed){exit 1}
