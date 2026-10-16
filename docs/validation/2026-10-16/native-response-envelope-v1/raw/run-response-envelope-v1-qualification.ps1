$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\native-response-envelope-v1-qualification"
if(Test-Path $out){throw 'Coordinator exists'};New-Item -ItemType Directory $out|Out-Null
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;npu_executed=$false;desktop_changed=$false;tahoma_tested=$false;active_phase='waiting-for-graphics-experiments'}
function Save-Report{$report|ConvertTo-Json -Depth 6|Set-Content "$out\report.json"};Save-Report
try{
 $deadline=[DateTime]::UtcNow.AddMinutes(55)
 while($true){$q=Get-Content "$vm\ptl-combined-desktop-v5\report.json" -Raw|ConvertFrom-Json;if($q.completed){break};if([DateTime]::UtcNow -gt $deadline){throw 'Graphics completion deadline'};Start-Sleep -Seconds 3}
 $desktop=if(Test-Path "$vm\native-desktop-admission-v63\report.json"){'native-desktop-admission-v63'}else{'native-desktop-admission-v61'}
 $q=Get-Content "$vm\$desktop\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.cleanup_passed -or !$q.memory_integrity_after){throw 'Desktop cleanup incomplete'}
 if(@(Get-Process nr_registered*,nr_native*,nr_ptl*,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Graphics experiment active'}
 $report.active_phase='cpu-component-build';Save-Report
 $build=Start-Process powershell.exe -ArgumentList @('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',('"'+$vm+'\build-native-response-envelope-v1.ps1"')) -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$out\build.stdout.log" -RedirectStandardError "$out\build.stderr.log"
 try{if($build.ExitCode){throw 'CPU component build failed'}}finally{$build.Dispose()}
 $exe="$r\builds\native-response-envelope-v1\Release\nr_response_envelope_compare.exe";$report.binary_sha256=(Get-FileHash $exe).Hash.ToLowerInvariant();$report.active_phase='cpu-equivalence';Save-Report
 $oldPath=$env:PATH;$env:PATH="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs;"+$oldPath
 try{$child=Start-Process $exe -PassThru -NoNewWindow -RedirectStandardOutput "$out\comparison.stdout.jsonl" -RedirectStandardError "$out\comparison.stderr.log";try{if(!$child.WaitForExit(120000)){$child.Kill();$child.WaitForExit();throw 'CPU comparison deadline'};$child.Refresh();if($child.ExitCode){throw 'CPU equivalence failed'}}finally{$child.Dispose()}}finally{$env:PATH=$oldPath}
 $rows=@(Get-Content "$out\comparison.stdout.jsonl"|Where-Object {$_ -like '{*'}|ForEach-Object {$_|ConvertFrom-Json});$result=@($rows|Where-Object completed)
 if($result.Count -ne 1 -or !$result[0].passed -or $result[0].equivalence_checks -ne 3598 -or $result[0].npu_executed -or $result[0].desktop_tested){throw 'CPU proof incomplete'}
 $report.equivalence_checks=$result[0].equivalence_checks;$report.timing=@($rows|Where-Object kind -eq 'validation_only_timing');$report.passed=$true
}catch{$report.error=$_.Exception.Message}
finally{$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out\report.json" -Raw;if(!$report.passed){exit 1}
