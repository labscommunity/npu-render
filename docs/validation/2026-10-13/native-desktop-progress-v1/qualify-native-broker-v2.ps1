$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render"
if(@(Get-Process nr_registered_local_indexed_worker,nr_native_probe_ddi_test -ErrorAction SilentlyContinue).Count){throw 'Another native experiment active'}
$source=Get-Content "$r\vm\native-broker-v2-source.json" -Raw|ConvertFrom-Json
foreach($f in $source.psobject.Properties){if((Get-FileHash "$r\vm\$($f.Name)").Hash -ne $f.Value){throw 'Controller source mismatch'}}
& "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" "$r\vm\qualify-native-broker-v2.py" --worker "$r\builds\native-routing-v3\Release\nr_registered_local_indexed_worker.exe" --client "$r\builds\native-routing-v3\Release\nr_native_probe_ddi_test.exe" --runtime "$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs" --cache "$r\vm\model-cache-2026-4-v209" --output "$r\vm\native-broker-v2-qualification"
if($LASTEXITCODE){throw 'Simultaneous native consumer qualification failed'}
foreach($f in $source.psobject.Properties){if((Get-FileHash "$r\vm\$($f.Name)").Hash -ne $f.Value){throw 'Controller source changed'}}
Copy-Item "$r\vm\native-broker-v2-source.json" "$r\vm\native-broker-v2-qualification\source-sha256.json"
