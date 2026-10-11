$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$src='C:\Users\tatef\AppData\Local\Temp\npu-graphics-probe-0146a2388f25';$build="$r\builds\present-umd-v205";$out="$r\vm\runtime-2026-4-v205"
if(Test-Path $out){throw 'Evidence exists'}
$manifest=Get-Content "$build\registered-build-report.json" -Raw|ConvertFrom-Json
if(!$manifest.clean_build){throw 'Clean build required'}
foreach($f in $manifest.binary_sha256.psobject.Properties){if((Get-FileHash "$build\Release\$($f.Name)").Hash -ne $f.Value){throw 'Binary hash mismatch'}}
foreach($f in $manifest.source_sha256.psobject.Properties){if((Get-FileHash "$src\$($f.Name)").Hash -ne $f.Value){throw 'Source hash mismatch'}}
$stream=Get-Content "$r\vm\indexed-dwm-stream-v105\report.json" -Raw|ConvertFrom-Json;if(!$stream.completed -or !$stream.configuration_removed){throw 'Live stream still active'}
New-Item -ItemType Directory $out|Out-Null;$oldPath=$env:PATH;$oldCache=$env:NR_RENDER_NPU_CACHE
$results=[Collections.Generic.List[object]]::new()
function Run([string]$name,[string]$binary,[string]$arguments,[int]$limit=240000){
 $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=if([IO.Path]::IsPathRooted($binary)){$binary}else{"$build\Release\$binary"};$info.Arguments=$arguments;$info.UseShellExecute=$false;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 $process=[Diagnostics.Process]::new();$process.StartInfo=$info
 if(!$process.Start()){throw 'Cannot start qualification'}
 $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync();$watch=[Diagnostics.Stopwatch]::StartNew();$timedOut=$false
 if(!$process.WaitForExit($limit)){$timedOut=$true;$process.Kill();$process.WaitForExit()}
 $code=$process.ExitCode;[IO.File]::WriteAllText("$out\$name.stdout.jsonl",$stdout.GetAwaiter().GetResult());[IO.File]::WriteAllText("$out\$name.stderr.log",$stderr.GetAwaiter().GetResult())
 $entry=[ordered]@{name=$name;exit_code=$code;timed_out=$timedOut;elapsed_ms=$watch.ElapsedMilliseconds;passed=(!$timedOut -and $code -eq 0)};$results.Add($entry);$results|ConvertTo-Json|Set-Content "$out\runs.json"
 Get-Content "$out\$name.stdout.jsonl" -Tail 3;Get-Content "$out\$name.stderr.log" -Tail 4;$process.Dispose()
 if(!$entry.passed){throw "Qualification failed: $name"}
}
try{
 $env:PATH="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs;$env:PATH";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-2026-4-v205";New-Item -ItemType Directory $env:NR_RENDER_NPU_CACHE -ErrorAction SilentlyContinue|Out-Null
 foreach($test in @(@('triangles','nr_registered_triangle_texture_draw_test.exe'),@('derivatives','nr_registered_derivative_test.exe'),@('compositor','nr_registered_compositor_test.exe'),@('typed-load','nr_registered_typed_load_test.exe'),@('loops','nr_registered_loop_test.exe'),@('masks','nr_registered_mask_test.exe'),@('mirror','nr_registered_mirror_test.exe'))){Run ($test[0]+'-NPU') $test[1] 'NPU' $(if($test[0] -eq 'compositor'){600000}else{240000})}
 $capture="$r\vm\v105-early-draw-5112-1.bin"
 if((Get-FileHash $capture).Hash -ne '2bd0001ec19439a2ba859d28429ca68fdc020e0751ad91a1e7953881911f3a07'){throw 'Original capture mismatch'}
 foreach($backend in 'CPU','NPU'){
  Run ('capture-'+$backend) 'nr_registered_draw_replay_probe.exe' ('--capture "'+$capture+'" --backend '+$backend+' --output "'+$out+'\'+$backend+'.pixels"')
 }
 & "$src\venv\Scripts\python.exe" "$r\vm\compare-three-texture-v199.py" "$out"
 if($LASTEXITCODE -ne 0){throw 'CPU/NPU pixel comparison failed'}

 $capture1061="$r\vm\v102-early-draw-5112-1.bin"
 if((Get-FileHash $capture1061).Hash -ne '8e634f6ea14a33f0204d2aff4c1f1b1033db814c1959d04a1c5717d4bd63e44c'){throw '1061 capture mismatch'}
 New-Item -ItemType Directory "$out\capture1061"|Out-Null
 foreach($backend in 'CPU','NPU'){Run ('capture1061-'+$backend) 'nr_registered_draw_replay_probe.exe' ('--capture "'+$capture1061+'" --backend '+$backend+' --output "'+$out+'\capture1061\'+$backend+'.pixels"')}
 & "$src\venv\Scripts\python.exe" "$r\vm\compare-three-texture-v199.py" "$out\capture1061"
 if($LASTEXITCODE -ne 0){throw '1061 image comparison failed'}
 Run 'lab-NPU' 'nr_registered_lab_test.exe' 'NPU'

 $legacy="$r\builds\present-umd-v205-legacy";$legacyManifest=Get-Content "$legacy\registered-build-report.json" -Raw|ConvertFrom-Json
 foreach($f in $legacyManifest.binary_sha256.psobject.Properties){if((Get-FileHash "$legacy\Release\$($f.Name)").Hash -ne $f.Value){throw 'Legacy compatibility binary mismatch'}}
 $env:PATH="$src\venv\Lib\site-packages\openvino\libs;$oldPath";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-v118"
 Run 'legacy-compositor-NPU' "$legacy\Release\nr_registered_compositor_test.exe" 'NPU'
 Run 'legacy-triangles-NPU' "$legacy\Release\nr_registered_triangle_texture_draw_test.exe" 'NPU'
 $oldBuild="$r\builds\present-umd-v204";$oldManifest=Get-Content "$oldBuild\registered-build-report.json" -Raw|ConvertFrom-Json
 foreach($f in $oldManifest.binary_sha256.psobject.Properties){if((Get-FileHash "$oldBuild\Release\$($f.Name)").Hash -ne $f.Value){throw 'Baseline binary mismatch'}}
 foreach($variant in 'baseline','candidate'){
  & "$src\venv\Scripts\python.exe" "$r\vm\prepare-runtime-warm-packets.py" "$out\$variant" *> "$out\$variant-preparation.log"
  if($LASTEXITCODE -ne 0){throw 'Prewarm packet preparation failed'}
 }
 $env:PATH="$src\venv\Lib\site-packages\openvino\libs;$oldPath";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-v118"
 Run 'baseline-worker' "$oldBuild\Release\nr_registered_indexed_worker.exe" ('"'+$out+'\baseline" 17') 360000
 $env:PATH="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs;$oldPath";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-2026-4-v205"
 Run 'candidate-worker' 'nr_registered_indexed_worker.exe' ('"'+$out+'\candidate" 17') 360000
 & "$src\venv\Scripts\python.exe" "$r\vm\compare-runtime-warm-packets.py" "$out" *> "$out\prewarm-comparison.log"
 if($LASTEXITCODE -ne 0){throw 'Prewarm pixel comparison failed'}
 Get-Content "$out\packet-comparison.json"
 [ordered]@{build='present-umd-v205';all_checks_passed=$true;native_npu_execution=$true;desktop_offload_tested=$false;tahoma_performance_tested=$false}|ConvertTo-Json|Tee-Object "$out\report.json"
}finally{$env:PATH=$oldPath;$env:NR_RENDER_NPU_CACHE=$oldCache}
