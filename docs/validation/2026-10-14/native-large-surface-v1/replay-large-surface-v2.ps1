$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$b="$r\builds\native-routing-v12b";$out="$r\vm\large-surface-v2-replay";$trace="$r\vm\native-desktop-admission-v10\T8-NpuRender-NativeTraceV1\draw-17280.bin"
if(Test-Path $out){throw 'Replay evidence exists'}
$exe="$b\Release\nr_registered_draw_replay_probe.exe";$build=Get-Content "$b\registered-build-report.json" -Raw|ConvertFrom-Json
if(!$build.clean_build -or (Get-FileHash $exe).Hash -ne $build.binary_sha256.'nr_registered_draw_replay_probe.exe' -or (Get-FileHash $trace).Hash -ne 'BB4AC8F898081FCB01C36A7C9740959B05AC31549A79610C7A7EE303AA59B9A4'){throw 'Replay identity mismatch'}
New-Item -ItemType Directory $out|Out-Null
$env:PATH="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs;$env:PATH";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-2026-4-v209";$env:NR_RENDER_SHARED_QUEUE='1';$env:NR_RENDER_PIPELINE_CACHE='expanded';$env:NR_RENDER_MEMORY_CACHE='1';$env:NR_RENDER_COMPACT_COVERAGE='0'
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');passed=$false;completed=$false;driver_installed=$false;desktop_changed=$false;replays=@()}
try{
 foreach($backend in @('CPU','NPU')){
  $name=$backend.ToLower();$p=Start-Process $exe -ArgumentList @('--capture',('"'+$trace+'"'),'--backend',$backend,'--output',('"'+$out+'\'+$name+'-pixels.bin"')) -PassThru -NoNewWindow -RedirectStandardOutput "$out\$name.stdout.jsonl" -RedirectStandardError "$out\$name.stderr.log";[void]$p.Handle
  if(!$p.WaitForExit(240000)){$p.Kill();$p.WaitForExit();throw 'Owned replay exceeded 240 seconds'};$p.Refresh();$code=$p.ExitCode;$p.Dispose()
  $report.replays+=@{backend=$backend;exit_code=$code};$report|ConvertTo-Json -Depth 5|Set-Content "$out\report.json"
  if($code){throw "$backend replay failed"}
 }
 $cpu=Get-FileHash "$out\cpu-pixels.bin";$npu=Get-FileHash "$out\npu-pixels.bin";$report.exact_cpu_npu_match=$cpu.Hash -eq $npu.Hash;$report.cpu_pixels_sha256=$cpu.Hash;$report.npu_pixels_sha256=$npu.Hash;$report.bytes=(Get-Item "$out\npu-pixels.bin").Length
 if(!$report.exact_cpu_npu_match){throw 'CPU and NPU outputs differ'};$report.passed=$true
}catch{$report.error=$_.Exception.Message}
finally{$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');$report|ConvertTo-Json -Depth 5|Set-Content "$out\report.json"}
Get-Content "$out\report.json";if(!$report.passed){exit 1}
