$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$b="$r\builds\present-umd-v225";$out="$r\vm\packed-palette-v226"
$prior=Get-Content "$r\vm\indexed-dwm-stream-v119\report.json" -Raw|ConvertFrom-Json;if(!$prior.completed -or !$prior.configuration_removed){throw 'Desktop trial active'}
if(@(Get-Process nr_registered_indexed_stream_worker,nr_registered_indexed_worker -ErrorAction SilentlyContinue).Count){throw 'Another NPU renderer is running'}
$m=Get-Content "$b\registered-build-report.json" -Raw|ConvertFrom-Json;if((Get-FileHash "$b\Release\nr_registered_indexed_worker.exe").Hash -ne $m.binary_sha256.'nr_registered_indexed_worker.exe'){throw 'Worker mismatch'}
if(Test-Path "$out\worker.stdout.jsonl"){throw 'Evidence exists'}
$old=$env:PATH;$cache=$env:NR_RENDER_NPU_CACHE
try{
 $env:PATH="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs;$old";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-2026-4-v209"
 $p=Start-Process "$b\Release\nr_registered_indexed_worker.exe" -ArgumentList @(('"'+$out+'"'),'1') -RedirectStandardOutput "$out\worker.stdout.jsonl" -RedirectStandardError "$out\worker.stderr.log" -PassThru
 [void]$p.Handle
 if(!$p.WaitForExit(90000)){$p.Kill();throw 'Palette NPU check timed out'};$p.Refresh();if($p.ExitCode){throw 'Palette worker failed'};$p.Dispose()
 & "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" "$r\vm\check-packed-palette-v226.py" compare
 if($LASTEXITCODE){throw 'Palette exact comparison failed'}
}finally{$env:PATH=$old;$env:NR_RENDER_NPU_CACHE=$cache}
