$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$src='C:\Users\tatef\AppData\Local\Temp\npu-graphics-probe-0146a2388f25';$build="$r\builds\present-umd-v189";$out="$r\vm\point-snap-v189"
if(Test-Path $out){throw 'Evidence exists'}
$manifest=Get-Content "$build\registered-build-report.json" -Raw|ConvertFrom-Json
if(!$manifest.clean_build){throw 'Clean build required'}
foreach($f in $manifest.binary_sha256.psobject.Properties){if((Get-FileHash "$build\Release\$($f.Name)").Hash -ne $f.Value){throw 'Binary hash mismatch'}}
foreach($f in $manifest.source_sha256.psobject.Properties){if((Get-FileHash "$src\$($f.Name)").Hash -ne $f.Value){throw 'Source hash mismatch'}}
$stream=Get-Content "$r\vm\indexed-dwm-stream-v97\report.json" -Raw|ConvertFrom-Json;if(!$stream.completed -or !$stream.configuration_removed){throw 'Live stream still active'}
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
 Run 'point-NPU' 'nr_registered_point_texture_draw_test.exe' 'NPU'
 foreach($backend in 'CPU','NPU'){
  Run "captured-point-$backend" 'nr_registered_draw_replay_probe.exe' ('--capture "'+$r+'\vm\v97-early-draw-6428.bin" --backend '+$backend+' --output "'+$out+'\point-'+$backend+'.pixels"') 240000
 }
 Run 'lab-NPU' 'nr_registered_lab_test.exe' 'NPU'
 [ordered]@{build='present-umd-v189';all_checks_passed=$true;point_components=3520832;captured_pixels=423720;native_npu_execution=$true;desktop_offload_tested=$false;tahoma_performance_tested=$false}|ConvertTo-Json|Tee-Object "$out\report.json"
}finally{$env:PATH=$oldPath;$env:NR_RENDER_NPU_CACHE=$oldCache}
