$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render"
if(@(Get-Process nr_registered_local_indexed_worker,nr_registered_indexed_worker,nr_registered_indexed_stream_worker -ErrorAction SilentlyContinue).Count){throw 'Another renderer active'}
$admission=Get-Content "$r\vm\native-admission-v1-report.json" -Raw|ConvertFrom-Json
if(!$admission.passed -or !$admission.clean_build){throw 'Admission contracts not qualified'}
& "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" "$r\vm\qualify-native-session-driver-v1.py" --worker "$r\builds\native-pipe-worker-v1\Release\nr_registered_local_indexed_worker.exe" --client "$r\builds\native-session-driver-v1\Release\nr_native_probe_ddi_test.exe" --runtime "$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs" --cache "$r\vm\model-cache-2026-4-v209" --output "$r\vm\native-session-driver-v1-qualification"
if($LASTEXITCODE){throw 'Leased native DDI component failed'}
