$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\native-request-inventory-v2-qualification"
if(Test-Path $out){throw 'Inventory coordinator exists'};New-Item -ItemType Directory $out|Out-Null
. ([scriptblock]::Create([IO.File]::ReadAllText("$vm\atomic_lab_report_v1.ps1")))
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;npu_executed=$false;desktop_changed=$false;active_phase='waiting-for-desktop-cleanup';steps=@()}
function Save-Report{Write-NrLabReport "$out/report.json" $report};Save-Report
function Run-Step($name,$exe,$arguments,$seconds){
 $report.active_phase=$name;Save-Report;$p=[Diagnostics.Process]::new();$p.StartInfo.FileName=$exe;$p.StartInfo.Arguments=$arguments;$p.StartInfo.UseShellExecute=$false;$p.StartInfo.CreateNoWindow=$true;$p.StartInfo.RedirectStandardOutput=$true;$p.StartInfo.RedirectStandardError=$true
 try{if(!$p.Start()){throw 'Start failed'};$stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync();if(!$p.WaitForExit($seconds*1000)){$p.Kill();$p.WaitForExit();throw "$name deadline"};$code=$p.ExitCode;[IO.File]::WriteAllText("$out/$name.stdout.log",$stdout.Result);[IO.File]::WriteAllText("$out/$name.stderr.log",$stderr.Result);$report.steps+=@{name=$name;exit_code=$code};Save-Report;if($code){throw "$name exit $code"}}finally{$p.Dispose()}
}
try{
 $deadline=[DateTime]::UtcNow.AddMinutes(90)
 foreach($name in @('ptl-combined-desktop-v12','native-request-inventory-v1-qualification','command-batch-contract-v1-qualification')){while($true){$q=Get-Content "$vm/$name/report.json" -Raw|ConvertFrom-Json;if($q.completed){break};if([DateTime]::UtcNow -gt $deadline){throw 'Preceding experiment deadline'};Start-Sleep -Seconds 3}}
 $prior=if(Test-Path "$vm/native-desktop-admission-v70/report.json"){'native-desktop-admission-v70'}else{'native-desktop-admission-v68'}
 $q=Get-Content "$vm/$prior/report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.cleanup_passed -or !$q.memory_integrity_after){throw 'Desktop restoration incomplete'}
 if(@(Get-Process nr_registered*,nr_native*,nr_ptl*,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Graphics experiment active'}
 $expected=Get-Content "$vm/native-request-inventory-v2-source.json" -Raw|ConvertFrom-Json
 foreach($f in $expected.psobject.Properties){if((Get-FileHash "$vm/$($f.Name)").Hash -ne $f.Value){throw "Source mismatch: $($f.Name)"}}
 $s="$vm/native-request-inventory-v2";$b="$r/builds/native-request-inventory-v2";$sdk="$r/toolchains/openvino-2026.4.1/Lib/site-packages/openvino"
 if(Test-Path $b){throw 'Build exists'}
 Run-Step 'configure' 'cmake.exe' ('-S "'+$s+'" -B "'+$b+'" -A x64 "-DOpenVINO_DIR='+$sdk+'/cmake" "-DNR_QUALIFIED_SOURCE='+$vm+'/native-routing-v45/native/windows/registered_lab"') 120
 Run-Step 'build' 'cmake.exe' ('--build "'+$b+'" --config Release --clean-first') 300
 foreach($f in $expected.psobject.Properties){if((Get-FileHash "$vm/$($f.Name)").Hash -ne $f.Value){throw 'Source changed during build'}}
 [ordered]@{clean_build=$true;binary_sha256=(Get-FileHash "$b/Release/nr_request_inventory.exe").Hash;source_sha256=$expected;npu_executed=$false}|ConvertTo-Json -Depth 5|Set-Content "$b/build-report.json"
 Run-Step 'inventory' "$r/toolchains/openvino-2026.4.1/Scripts/python.exe" ('"'+$vm+'/qualify-native-request-inventory-v2.py"') 240
 $q=Get-Content "$vm/native-request-inventory-v2-results/report.json" -Raw|ConvertFrom-Json;if(!$q.passed -or !$q.completed -or $q.requests -ne 972 -or $q.npu_executed){throw 'Inventory proof failed'}
 $report.passed=$true
}catch{$report.error=$_.Exception.Message}
finally{$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out/report.json" -Raw;if(!$report.passed){exit 1}
