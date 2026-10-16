$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm"
$deadline=[DateTime]::UtcNow.AddMinutes(16)
do {
 $q=Get-Content "$vm\native-automatic-registration-v2\report.json" -Raw|ConvertFrom-Json
 if($q.completed){break}
 if([DateTime]::UtcNow -gt $deadline){throw 'Live trial still active; program execution deferred'}
 Start-Sleep -Seconds 5
}while($true)
if(!$q.passed){throw 'Automatic registered graphics did not pass'}
$q=Get-Content "$vm\native-registration-v32\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.cleanup_passed){throw 'Automatic registration cleanup incomplete'}
$q=Get-Content "$vm\native-desktop-admission-v52\report.json" -Raw|ConvertFrom-Json
if(!$q.completed -or !$q.cleanup_passed){throw 'Ordinary desktop recovery incomplete'}
if(@(Get-Process nr_registered_local_indexed_worker,nr_native_topology,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Another graphics experiment active'}
$sources=Get-Content "$vm\ptl-six-span-program-v3-source.json" -Raw|ConvertFrom-Json
foreach($f in $sources.psobject.Properties){if((Get-FileHash "$vm\$($f.Name)").Hash -ne $f.Value){throw 'Bounded-program source identity mismatch'}}
$env:PYTHONPATH="$vm\ptl-reusable-harness-v1\src"
& "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" "$vm\ptl-reusable-harness-v1\tools\build_ptl_kernel.py" --toolchain "$vm\movitools-package-v2\extracted\Driver\MVC_DEPEND" --source "$vm\ptl-six-span-program-v2.c" --output "$vm\ptl-six-span-program-build-v2"
if($LASTEXITCODE){throw 'Bounded-program kernel build failed'}
$q=Get-Content "$vm\ptl-six-span-program-build-v2\report.json" -Raw|ConvertFrom-Json
if(!$q.completed -or !$q.passed -or $q.source_sha256 -ne $sources.'ptl-six-span-program-v2.c' -or $q.child_result.text_bytes -gt 18256){throw 'Bounded-program build contract failed'}
& "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" "$vm\qualify-ptl-six-span-program-v3.py" --elf-sha256 $q.child_result.elf_sha256
if($LASTEXITCODE){throw 'Bounded-program component qualification failed'}
