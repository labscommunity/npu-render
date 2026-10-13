$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render"
if(@(Get-Process nr_registered_local_indexed_worker,nr_registered_indexed_worker,nr_registered_indexed_stream_worker -ErrorAction SilentlyContinue).Count){throw 'Another renderer active'}
$dwm=@(Get-Process dwm|Where-Object SessionId -gt 0);if($dwm.Count -ne 1){throw 'Expected one observed compositor'}
$created=$dwm[0].StartTime.ToFileTimeUtc()
$hostProcess=@(Get-Process WUDFHost|Where-Object SessionId -eq 0|Sort-Object Id)[0];if(!$hostProcess){throw 'No observed UMDF host'}
& "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" "$r\vm\qualify-native-routing-peers-v1.py" --umdf-pid $hostProcess.Id --umdf-created $hostProcess.StartTime.ToFileTimeUtc() --dwm-pid $dwm[0].Id --dwm-created $created --dwm-session $dwm[0].SessionId --worker "$r\builds\native-routing-v1\Release\nr_registered_local_indexed_worker.exe" --client "$r\builds\native-adapter-promoted-v1\Release\nr_registered_local_indexed_client_probe.exe" --runtime "$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs" --cache "$r\vm\model-cache-2026-4-v209" --trace "$r\vm\indexed-dwm-stream-v120" --reference "$r\vm\ordered-point-reentry-v1-expanded\report.json" --output "$r\vm\native-routing-peers-v1-qualification"
if($LASTEXITCODE){throw 'Promoted C++ client qualification failed'}
