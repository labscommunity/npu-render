$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\command-batch-transaction-v1-qualification"
if(Test-Path $out){throw 'Transaction coordinator exists'};New-Item -ItemType Directory $out|Out-Null
. ([scriptblock]::Create([IO.File]::ReadAllText("$vm\atomic_lab_report_v1.ps1")))
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;npu_executed=$false;desktop_changed=$false;active_phase='waiting-for-desktop-cleanup';steps=@()}
function Save-Report{Write-NrLabReport "$out/report.json" $report};Save-Report
function Run-Step($name,$exe,$arguments,$seconds){
 $report.active_phase=$name;Save-Report;$p=[Diagnostics.Process]::new();$p.StartInfo.FileName=$exe;$p.StartInfo.Arguments=$arguments;$p.StartInfo.UseShellExecute=$false;$p.StartInfo.CreateNoWindow=$true;$p.StartInfo.RedirectStandardOutput=$true;$p.StartInfo.RedirectStandardError=$true
 try{if(!$p.Start()){throw 'Start failed'};$stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync();if(!$p.WaitForExit($seconds*1000)){$p.Kill();$p.WaitForExit();throw "$name deadline"};$code=$p.ExitCode;[IO.File]::WriteAllText("$out/$name.stdout.log",$stdout.Result);[IO.File]::WriteAllText("$out/$name.stderr.log",$stderr.Result);$report.steps+=@{name=$name;exit_code=$code};Save-Report;if($code){throw "$name exit $code"}}finally{$p.Dispose()}
}
try{
 $q=Get-Content "$vm/ptl-command-batch-comparison-v4/report.json" -Raw|ConvertFrom-Json;if(!$q.passed -or !$q.completed){throw 'Component comparison incomplete'}
 $q=Get-Content "$vm/native-desktop-admission-v70/report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.cleanup_passed -or !$q.memory_integrity_after){throw 'Desktop restoration incomplete'}
 if(@(Get-Process nr_registered*,nr_native*,nr_ptl*,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Graphics experiment active'}
 $expected=Get-Content "$vm/command-batch-transaction-v1-source.json" -Raw|ConvertFrom-Json
 foreach($f in $expected.psobject.Properties){if((Get-FileHash "$vm/$($f.Name)").Hash -ne $f.Value){throw 'Transaction source mismatch'}}
 $s="$vm/command-batch-transaction-v1";$b="$r/builds/command-batch-transaction-v1";$sdk="$r/toolchains/openvino-2026.4.1/Lib/site-packages/openvino"
 if(Test-Path $b){throw 'Build exists'}
 Run-Step 'configure' 'cmake.exe' ('-S "'+$s+'" -B "'+$b+'" -A x64 "-DOpenVINO_DIR='+$sdk+'/cmake"') 120
 Run-Step 'build' 'cmake.exe' ('--build "'+$b+'" --config Release --clean-first') 300
 foreach($f in $expected.psobject.Properties){if((Get-FileHash "$vm/$($f.Name)").Hash -ne $f.Value){throw 'Transaction source changed'}}
 $report.binary_sha256=(Get-FileHash "$b/Release/nr_command_batch_transaction.exe").Hash;$report.source_sha256=$expected
 $env:PATH="$sdk/libs;$env:PATH";$report.npu_executed=$null;Save-Report
 Run-Step 'npu-transaction' "$b/Release/nr_command_batch_transaction.exe" ('"'+$vm+'/ptl-six-span-command-batch-v3/patched.blob"') 180
 $rows=@(Get-Content "$out/npu-transaction.stdout.log"|Where-Object {$_.StartsWith('{')}|ForEach-Object {$_|ConvertFrom-Json})
 if($rows.Count -ne 4){throw 'Missing transaction cases'}
 for($i=0;$i -lt 4;$i++){$x=$rows[$i];if($x.scenario -ne $i -or !$x.passed -or $x.execution_device -ne 'NPU' -or $x.cpu_fallback -or $x.desktop_tested -or $x.committed_pixels_sha256.Length -ne 64){throw 'Invalid transaction receipt'}}
 if($rows[0].provider_submissions -ne 3 -or $rows[0].committed_version -ne 33 -or $rows[0].provider_generations -ne 1 -or $rows[1].provider_submissions -ne 6 -or $rows[1].committed_version -ne 65 -or $rows[1].provider_generations -ne 1){throw 'Normal ordered commit incomplete'}
 if(!$rows[2].injected_after_partial_output -or !$rows[2].retried -or $rows[2].provider_generations -ne 2 -or $rows[2].provider_submissions -ne 4 -or $rows[2].committed_version -ne 33){throw 'Checkpoint recovery incomplete'}
 if(!$rows[3].injected_after_partial_output -or !$rows[3].abandoned -or $rows[3].provider_generations -ne 1 -or $rows[3].provider_submissions -ne 1 -or $rows[3].committed_version -ne 1){throw 'Abandonment changed the checkpoint'}
 $report.npu_executed=$true;$report.cases=$rows;$report.real_device_loss_tested=$false;$report.desktop_tested=$false;$report.tahoma_tested=$false;$report.passed=$true
}catch{$report.error=$_.Exception.Message}
finally{$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out/report.json" -Raw;if(!$report.passed){exit 1}
