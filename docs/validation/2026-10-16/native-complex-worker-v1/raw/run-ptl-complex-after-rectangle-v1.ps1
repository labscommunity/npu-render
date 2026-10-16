$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\ptl-complex-after-rectangle-v1"
if(Test-Path $out){throw 'Coordinator exists'};New-Item -ItemType Directory $out|Out-Null
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;active_phase='waiting-for-device-qualification';desktop_changed=$false;tahoma_tested=$false}
function Save-Report{$report|ConvertTo-Json -Depth 5|Set-Content "$out\report.json"};Save-Report
try{
 $deadline=[DateTime]::UtcNow.AddMinutes(20)
 while($true){$q=Get-Content "$vm\ptl-rectangle-after-desktop-v1\report.json" -Raw|ConvertFrom-Json;if($q.completed){break};if([DateTime]::UtcNow -ge $deadline){throw 'Preceding qualification deadline'};Start-Sleep -Seconds 2}
 if(!$q.passed){throw 'Inspect preceding device failure'}
 $q=Get-Content "$vm\native-desktop-admission-v58\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.cleanup_passed -or !$q.memory_integrity_after){throw 'Desktop cleanup incomplete'}
 if(@(Get-Process nr_registered*,nr_native*,nr_ptl*,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Graphics experiment active'}
 $report.active_phase='diagnostic-build';Save-Report
 & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$vm\build-ptl-complex-original-comparison-v1.ps1" > "$out\build.stdout.log" 2> "$out\build.stderr.log";if($LASTEXITCODE){throw 'Diagnostic build failed'}
 $report.active_phase='original-npu-comparisons';Save-Report
 $hash=(Get-FileHash "$r\builds\ptl-complex-original-comparison-v1\Release\nr_ptl_complex_original_compare.exe").Hash.ToLowerInvariant()
 & "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" "$vm\run-ptl-complex-original-timing-v1.py" --model-sha256 '47f69b5861732a03014b58160116ebbb928fd6e818a11b7f553e423a4cf3c43a' --executable-sha256 $hash > "$out\timing.stdout.log" 2> "$out\timing.stderr.log";if($LASTEXITCODE){throw 'Original NPU measurement incomplete'}
 $q=Get-Content "$vm\ptl-complex-original-timing-v1\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.passed -or !$q.passed_means_measurement_complete -or @($q.cases).Count -ne 10){throw 'Measurement incomplete'}
 $report.passed=$true;$report.byte_equivalent=$q.byte_equivalent;$report.independent_fp32_checked=$true
}catch{$report.error=$_.Exception.Message}
finally{$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out\report.json" -Raw;if(!$report.passed){exit 1}
