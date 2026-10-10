$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$src='C:\Users\tatef\AppData\Local\Temp\npu-graphics-probe-0146a2388f25';$build="$r\builds\present-umd-v120";$out="$r\vm\destination-in-v120"
if(Test-Path $out){throw 'Evidence exists'}
$report=Get-Content "$build\registered-build-report.json" -Raw|ConvertFrom-Json
if(!$report.clean_build){throw 'Clean build required'}
foreach($f in $report.binary_sha256.psobject.Properties){if((Get-FileHash "$build\Release\$($f.Name)").Hash -ne $f.Value){throw 'Binary hash mismatch'}}
$stream=Get-Content "$r\vm\indexed-dwm-stream-v84\report.json" -Raw|ConvertFrom-Json
if(!$stream.completed -or !$stream.configuration_removed){throw 'Previous live worker still active'}
New-Item -ItemType Directory $out|Out-Null
$env:PATH="$src\venv\Lib\site-packages\openvino\libs;$env:PATH";$oldCache=$env:NR_RENDER_NPU_CACHE
try {
 $env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-v118"
 foreach($backend in 'CPU','NPU'){
  $ErrorActionPreference='Continue'
  & "$build\Release\nr_registered_draw_replay_probe.exe" --capture "$r\vm\replay-v84-dwm\draw-1092.bin" --backend $backend --output "$out\$backend.bin" --repeat 2 --batch 4096 *> "$out\replay-$backend.log"
  $code=$LASTEXITCODE;Get-Content "$out\replay-$backend.log"|Select-Object -Last 8
  if($code -ne 0){throw "Captured draw failed on $backend"}
 }
 foreach($test in 'output_merger','uniform_draw','lab'){
  $ErrorActionPreference='Continue'; & "$build\Release\nr_registered_${test}_test.exe" NPU *> "$out\$test-NPU.log"
  $code=$LASTEXITCODE;Get-Content "$out\$test-NPU.log"|Select-Object -Last 2
  if($code -ne 0){throw "$test NPU test failed"}
 }
 $ErrorActionPreference='Stop'
 $packets="$out\packets";New-Item -ItemType Directory $packets|Out-Null
 $ErrorActionPreference='Continue'
 & "$build\Release\nr_registered_indexed_exchange_test.exe" --emit-destination-in "$packets\request-42-00000000123456780000000000abcdef-1.bin" *> "$out\emit-packet.log"
 if($LASTEXITCODE -ne 0){throw 'Packet fixture failed'}
 & "$build\Release\nr_registered_indexed_worker.exe" $packets 1 *> "$out\packet-worker.log"
 $code=$LASTEXITCODE;Get-Content "$out\packet-worker.log";if($code -ne 0){throw 'ABI 9 worker failed'}
 $ErrorActionPreference='Stop'
 $reply=[IO.File]::ReadAllBytes("$packets\reply-42-00000000123456780000000000abcdef-1.bin")
 if($reply.Length -ne (128+64*64*4) -or [BitConverter]::ToUInt32($reply,24) -ne 9 -or [BitConverter]::ToUInt32($reply,68) -ne 2 -or [BitConverter]::ToUInt32($reply,72) -ne 3){throw 'Wrong ABI 9 response'}
 $expected=@(0,1,2,2);for($n=128;$n -lt $reply.Length;$n++){if($reply[$n] -ne $expected[($n-128)%4]){throw 'Destination-in packet pixel mismatch'}}
 [ordered]@{captured_draw='v84-dwm-1092';cpu_sha256=(Get-FileHash "$out\CPU.bin").Hash;npu_sha256=(Get-FileHash "$out\NPU.bin").Hash;abi9_verified_components=16384;desktop_offload_tested=$false;all_checks_passed=$true}|ConvertTo-Json|Tee-Object "$out\report.json"
}finally{$env:NR_RENDER_NPU_CACHE=$oldCache}
