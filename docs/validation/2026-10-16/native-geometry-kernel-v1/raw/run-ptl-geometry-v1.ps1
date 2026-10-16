$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm"
$deadline=[DateTime]::UtcNow.AddMinutes(12)
do {
 $q=Get-Content "$vm\ptl-native-live-v3\report.json" -Raw|ConvertFrom-Json
 if($q.completed){break}
 if([DateTime]::UtcNow -gt $deadline){throw 'Live trial still active; geometry execution deferred'}
 Start-Sleep -Seconds 5
}while($true)
if(!$q.passed){throw 'Live qualification did not pass'}
$q=Get-Content "$vm\native-desktop-admission-v49\report.json" -Raw|ConvertFrom-Json
if(!$q.completed -or !$q.cleanup_passed){throw 'Ordinary desktop recovery incomplete'}
if(@(Get-Process nr_registered_local_indexed_worker,nr_native_topology,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Another graphics experiment active'}
$sources=Get-Content "$vm\ptl-six-span-geometry-v1-source.json" -Raw|ConvertFrom-Json
foreach($f in $sources.psobject.Properties){if((Get-FileHash "$vm\$($f.Name)").Hash -ne $f.Value){throw 'Geometry source identity mismatch'}}
$env:PYTHONPATH="$vm\ptl-reusable-harness-v1\src"
& "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" "$vm\ptl-reusable-harness-v1\tools\build_ptl_kernel.py" --toolchain "$vm\movitools-package-v2\extracted\Driver\MVC_DEPEND" --source "$vm\ptl-six-span-geometry-v1.c" --output "$vm\ptl-six-span-geometry-build-v1"
if($LASTEXITCODE){throw 'Geometry kernel build failed'}
$q=Get-Content "$vm\ptl-six-span-geometry-build-v1\report.json" -Raw|ConvertFrom-Json
if(!$q.completed -or !$q.passed -or $q.source_sha256 -ne $sources.'ptl-six-span-geometry-v1.c' -or $q.child_result.text_bytes -gt 18256){throw 'Geometry build contract failed'}
& "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" "$vm\qualify-ptl-six-span-geometry-v1.py" --elf-sha256 $q.child_result.elf_sha256
if($LASTEXITCODE){throw 'Geometry component qualification failed'}
