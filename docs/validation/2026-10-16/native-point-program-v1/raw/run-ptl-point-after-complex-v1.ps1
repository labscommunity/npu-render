$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\ptl-point-after-complex-v1"
if(Test-Path $out){throw 'Coordinator exists'};New-Item -ItemType Directory $out|Out-Null
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;active_phase='waiting-for-live-cleanup';desktop_changed=$false;tahoma_tested=$false}
function Save-Report{$report|ConvertTo-Json -Depth 5|Set-Content "$out\report.json"};Save-Report
try{
 $deadline=[DateTime]::UtcNow.AddMinutes(20)
 while($true){$q=Get-Content "$vm\ptl-complex-after-desktop-v1\report.json" -Raw|ConvertFrom-Json;if($q.completed){break};if([DateTime]::UtcNow -ge $deadline){throw 'Desktop deadline exceeded'};Start-Sleep -Seconds 2}
 if(!$q.passed){throw 'Inspect preceding component failure before using NPU'}
 $q=Get-Content "$vm\native-desktop-admission-v56\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.cleanup_passed -or !$q.memory_integrity_after){throw 'Native cleanup incomplete'}
 if(@(Get-Process nr_registered*,nr_native*,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Graphics experiment active'}
 $python="$r\toolchains\openvino-2026.4.1\Scripts\python.exe";$env:PYTHONPATH="$vm\ptl-reusable-harness-v1\src"
 $report.active_phase='target-compile';Save-Report
 & $python "$vm\ptl-reusable-harness-v1\tools\build_ptl_kernel.py" --toolchain "$vm\movitools-package-v2\extracted\Driver\MVC_DEPEND" --source "$vm\ptl-six-span-program-point-v1.c" --output "$vm\ptl-six-span-program-point-build-v1" > "$out\build.stdout.log" 2> "$out\build.stderr.log"
 if($LASTEXITCODE){throw 'Target compilation failed'}
 $q=Get-Content "$vm\ptl-six-span-program-point-build-v1\report.json" -Raw|ConvertFrom-Json
 if(!$q.passed -or $q.child_result.text_bytes -gt 18256){throw 'Kernel exceeds qualified code span'}
 $report.text_bytes=$q.child_result.text_bytes;$report.active_phase='device-fixtures';Save-Report
 $hash=(Get-FileHash "$vm\ptl-six-span-program-point-build-v1\kernel.elf").Hash.ToLowerInvariant()
 & $python "$vm\qualify-ptl-six-span-program-point-v1.py" --elf-sha256 $hash > "$out\qualification.stdout.log" 2> "$out\qualification.stderr.log"
 if($LASTEXITCODE){throw 'NPU fixture qualification failed'}
 $q=Get-Content "$vm\ptl-six-span-program-point-v1\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.passed){throw 'NPU proof incomplete'}
 $report.passed=$true;$report.unique_npu_fixtures=424;$report.model_sha256=$q.patched_sha256
}catch{$report.error=$_.Exception.Message}
finally{$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out\report.json" -Raw;if(!$report.passed){exit 1}
