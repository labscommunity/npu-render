$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\ptl-large-span-softmax-v1-qualification"
if(Test-Path $out){throw 'Carrier coordinator exists'};New-Item -ItemType Directory $out|Out-Null
. ([scriptblock]::Create([IO.File]::ReadAllText("$vm\atomic_lab_report_v1.ps1")))
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;desktop_changed=$false;custom_kernel_executed=$false;active_phase='waiting-for-desktop-restoration'}
function Save-Report{Write-NrLabReport "$out/report.json" $report};Save-Report;$p=$null
try{
 $deadline=[DateTime]::UtcNow.AddMinutes(25)
 while($true){$q=Get-Content "$vm\ptl-combined-desktop-v14\report.json" -Raw|ConvertFrom-Json;if($q.completed){break};if([DateTime]::UtcNow -gt $deadline){throw 'Desktop deadline'};Start-Sleep -Seconds 3}
 $q=Get-Content "$vm\native-desktop-admission-v72\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.cleanup_passed -or !$q.memory_integrity_after){throw 'Desktop recovery incomplete'}
 if(@(Get-Process nr_registered*,nr_native*,nr_ptl*,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Graphics experiment still active'}
 $report.active_phase='original-carrier-qualification';Save-Report
 $p=[Diagnostics.Process]::new();$p.StartInfo.FileName="$r\toolchains\openvino-2026.4.1\Scripts\python.exe";$p.StartInfo.Arguments='"'+$vm+'\create-ptl-large-span-softmax-v1.py"';$p.StartInfo.UseShellExecute=$false;$p.StartInfo.CreateNoWindow=$true;$p.StartInfo.RedirectStandardOutput=$true;$p.StartInfo.RedirectStandardError=$true
 if(!$p.Start()){throw 'Carrier process did not start'};$stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync();if(!$p.WaitForExit(600000)){$p.Kill();$p.WaitForExit();throw 'Carrier deadline'}
 $code=$p.ExitCode;[IO.File]::WriteAllText("$out/stdout.log",$stdout.Result);[IO.File]::WriteAllText("$out/stderr.log",$stderr.Result);$report.exit_code=$code
 $q=Get-Content "$vm\ptl-large-span-softmax-v1\report.json" -Raw|ConvertFrom-Json;$report.original_before_after_passed=$q.original_before_after_passed
 if($code -or !$q.passed){throw 'Not all wider carriers qualified; preserve compiler and execution reports'};$report.passed=$true
}catch{$report.error=$_.Exception.Message}
finally{if($p){$p.Dispose()};$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out/report.json" -Raw;if(!$report.passed){exit 1}
