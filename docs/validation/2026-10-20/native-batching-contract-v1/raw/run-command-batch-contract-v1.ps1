$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\command-batch-contract-v1-qualification"
if(Test-Path $out){throw 'Contract coordinator exists'};New-Item -ItemType Directory $out|Out-Null
. ([scriptblock]::Create([IO.File]::ReadAllText("$vm\atomic_lab_report_v1.ps1")))
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;npu_executed=$false;desktop_changed=$false;active_phase='waiting-for-desktop-cleanup';steps=@()}
function Save-Report{Write-NrLabReport "$out/report.json" $report};Save-Report
function Run-Step($name,$exe,$arguments,$seconds){
 $report.active_phase=$name;Save-Report;$p=[Diagnostics.Process]::new();$p.StartInfo.FileName=$exe;$p.StartInfo.Arguments=$arguments;$p.StartInfo.UseShellExecute=$false;$p.StartInfo.CreateNoWindow=$true;$p.StartInfo.RedirectStandardOutput=$true;$p.StartInfo.RedirectStandardError=$true
 try{if(!$p.Start()){throw 'Start failed'};$stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync();if(!$p.WaitForExit($seconds*1000)){$p.Kill();$p.WaitForExit();throw "$name deadline"};$code=$p.ExitCode;[IO.File]::WriteAllText("$out/$name.stdout.log",$stdout.Result);[IO.File]::WriteAllText("$out/$name.stderr.log",$stderr.Result);$report.steps+=@{name=$name;exit_code=$code};Save-Report;if($code){throw "$name exit $code"}}finally{$p.Dispose()}
}
try{
 $deadline=[DateTime]::UtcNow.AddMinutes(90)
 foreach($name in @('ptl-combined-desktop-v12','native-request-inventory-v1-qualification')){while($true){$q=Get-Content "$vm/$name/report.json" -Raw|ConvertFrom-Json;if($q.completed){break};if([DateTime]::UtcNow -gt $deadline){throw 'Preceding experiment deadline'};Start-Sleep -Seconds 3}}
 $prior=if(Test-Path "$vm/native-desktop-admission-v70/report.json"){'native-desktop-admission-v70'}else{'native-desktop-admission-v68'}
 $q=Get-Content "$vm/$prior/report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.cleanup_passed -or !$q.memory_integrity_after){throw 'Desktop restoration incomplete'}
 if(@(Get-Process nr_registered*,nr_native*,nr_ptl*,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Graphics experiment active'}
 $expected=Get-Content "$vm/command-batch-contract-v1-source.json" -Raw|ConvertFrom-Json
 foreach($f in $expected.psobject.Properties){if((Get-FileHash "$vm/$($f.Name)").Hash -ne $f.Value){throw 'Contract source mismatch'}}
 $s="$vm/command-batch-contract-v1";$b="$r/builds/command-batch-contract-v1"
 if(Test-Path $b){throw 'Build exists'}
 Run-Step 'configure' 'cmake.exe' ('-S "'+$s+'" -B "'+$b+'" -A x64') 120
 Run-Step 'build' 'cmake.exe' ('--build "'+$b+'" --config Release --clean-first') 180
 Run-Step 'test' 'ctest.exe' ('--test-dir "'+$b+'" -C Release --output-on-failure --verbose') 120
 foreach($f in $expected.psobject.Properties){if((Get-FileHash "$vm/$($f.Name)").Hash -ne $f.Value){throw 'Contract source changed'}}
 $report.binary_sha256=(Get-FileHash "$b/Release/nr_command_batch_test.exe").Hash
 $report.source_sha256=$expected;$report.passed=$true
}catch{$report.error=$_.Exception.Message}
finally{$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out/report.json" -Raw;if(!$report.passed){exit 1}
