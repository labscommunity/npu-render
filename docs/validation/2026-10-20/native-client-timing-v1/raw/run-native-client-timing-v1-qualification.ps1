$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\native-client-timing-v1-qualification"
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
 $report.active_phase='waiting-for-resident-clip-component';Save-Report
 $deadline=[DateTime]::UtcNow.AddMinutes(180)
 foreach($name in @('ptl-resident-clip-v2-qualification')){
  while($true){$q=Get-Content "$vm/$name/report.json" -Raw|ConvertFrom-Json;if($q.completed){break};if([DateTime]::UtcNow -gt $deadline){throw 'Preceding experiment deadline'};Start-Sleep -Seconds 3}
 }
 $preceding=if(Test-Path "$vm/native-desktop-admission-v67/report.json"){'native-desktop-admission-v67'}else{'physical-gpu-desktop-v2'}
 $q=Get-Content "$vm/$preceding/report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.cleanup_passed -or !$q.memory_integrity_after){throw 'Preceding desktop cleanup incomplete'}
 if(@(Get-Process nr_registered*,nr_native*,nr_ptl*,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Another graphics experiment is active'}


 Invoke-Step 'build-and-contracts' 'powershell.exe' ('-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "'+$vm+'\build-native-client-timing-v1.ps1"') 900
 $q=Get-Content "$r/builds/native-client-timing-v1/registered-build-report.json" -Raw|ConvertFrom-Json
 if(!$q.clean_build -or !$q.admission_contract_passed -or !$q.trace_lifecycle_contract_passed){throw 'Diagnostic build contracts failed'}
 $report.umd_sha256=$q.binary_sha256.'nr_native_probe_umd.dll';$report.driver_installed=$false
 $report.passed=$true
}catch{$report.error=$_.Exception.Message}
finally{$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out/report.json" -Raw;if(!$report.passed){exit 1}
