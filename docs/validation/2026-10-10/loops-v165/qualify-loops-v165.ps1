$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$src='C:\Users\tatef\AppData\Local\Temp\npu-graphics-probe-0146a2388f25';$build="$r\builds\present-umd-v165";$out="$r\vm\loops-v165"
if(Test-Path $out){throw 'Evidence exists'}
$manifest=Get-Content "$build\registered-build-report.json" -Raw|ConvertFrom-Json
if(!$manifest.clean_build){throw 'Clean build required'}
foreach($f in $manifest.binary_sha256.psobject.Properties){if((Get-FileHash "$build\Release\$($f.Name)").Hash -ne $f.Value){throw 'Binary hash mismatch'}}
foreach($f in $manifest.source_sha256.psobject.Properties){if((Get-FileHash "$src\$($f.Name)").Hash -ne $f.Value){throw 'Source hash mismatch'}}
$stream=Get-Content "$r\vm\indexed-dwm-stream-v90\report.json" -Raw|ConvertFrom-Json;if(!$stream.completed -or !$stream.configuration_removed){throw 'Live stream still active'}
New-Item -ItemType Directory $out|Out-Null;$oldPath=$env:PATH;$oldCache=$env:NR_RENDER_NPU_CACHE
$results=[Collections.Generic.List[object]]::new()
function Run([string]$name,[string]$binary,[string]$arguments,[int]$limit=240000){
 $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName="$build\Release\$binary";$info.Arguments=$arguments;$info.UseShellExecute=$false;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 $process=[Diagnostics.Process]::new();$process.StartInfo=$info
 if(!$process.Start()){throw 'Cannot start qualification'}
 $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync();$watch=[Diagnostics.Stopwatch]::StartNew();$timedOut=$false
 if(!$process.WaitForExit($limit)){$timedOut=$true;$process.Kill();$process.WaitForExit()}
 $code=$process.ExitCode;[IO.File]::WriteAllText("$out\$name.stdout.jsonl",$stdout.GetAwaiter().GetResult());[IO.File]::WriteAllText("$out\$name.stderr.log",$stderr.GetAwaiter().GetResult())
 $entry=[ordered]@{name=$name;exit_code=$code;timed_out=$timedOut;elapsed_ms=$watch.ElapsedMilliseconds;passed=(!$timedOut -and $code -eq 0)};$results.Add($entry);$results|ConvertTo-Json|Set-Content "$out\runs.json"
 Get-Content "$out\$name.stdout.jsonl" -Tail 3;Get-Content "$out\$name.stderr.log";$process.Dispose()
 if(!$entry.passed){throw "Qualification failed: $name"}
}
try{
 $env:PATH="$src\venv\Lib\site-packages\openvino\libs;$env:PATH";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-v118"
 foreach($section in 'arithmetic','loops','captured'){Run "$section-NPU" 'nr_registered_loop_test.exe' "NPU $section"}
 $packets="$out\captured-packets";New-Item -ItemType Directory $packets|Out-Null;$name='request-42-00000000123456780000000000abcdef-1.bin'
 Run 'raster-NPU' 'nr_registered_loop_test.exe' ('NPU raster --emit-packet "'+$packets+'\'+$name+'"')
 Run 'captured-worker-NPU' 'nr_registered_indexed_worker.exe' ('"'+$packets+'" 1')
 $reply=[IO.File]::ReadAllBytes("$packets\reply-42-00000000123456780000000000abcdef-1.bin");$expected=[IO.File]::ReadAllBytes("$packets\$name.pixels")
 if($reply.Length -ne 128+$expected.Length -or [BitConverter]::ToUInt32($reply,24) -ne 8 -or [BitConverter]::ToUInt32($reply,68) -ne 2 -or [BitConverter]::ToUInt32($reply,72) -ne 3){throw 'Wrong captured-loop response'}
 for($n=0;$n -lt $expected.Length;$n++){if($reply[128+$n] -ne $expected[$n]){throw 'Captured-loop packet pixel mismatch'}}
 $small="$out\uniform-packets";New-Item -ItemType Directory $small|Out-Null
 Run 'emit-uniform' 'nr_registered_indexed_exchange_test.exe' ('--emit-loops "'+$small+'\'+$name+'"')
 Run 'uniform-worker-NPU' 'nr_registered_indexed_worker.exe' ('"'+$small+'" 1')
 $reply=[IO.File]::ReadAllBytes("$small\reply-42-00000000123456780000000000abcdef-1.bin")
 if($reply.Length -ne 128+16384 -or [BitConverter]::ToUInt32($reply,68) -ne 2 -or [BitConverter]::ToUInt32($reply,72) -ne 3){throw 'Wrong uniform-loop response'}
 $color=@(128,32,64,255);for($n=128;$n -lt $reply.Length;$n++){if($reply[$n] -ne $color[($n-128)%4]){throw 'Uniform-loop packet pixel mismatch'}}
 foreach($test in 'lab','derivative','typed_load','triangle_texture_draw'){Run "$test-NPU" "nr_registered_${test}_test.exe" 'NPU'}
 Run 'texture-NPU' 'nr_registered_texture_test.exe' '--backend NPU'
 [ordered]@{build='present-umd-v165';all_checks_passed=$true;captured_packet_components=4096;uniform_packet_components=16384;native_npu_execution=$true;desktop_offload_tested=$false;tahoma_performance_tested=$false}|ConvertTo-Json|Tee-Object "$out\report.json"
}finally{$env:PATH=$oldPath;$env:NR_RENDER_NPU_CACHE=$oldCache}
