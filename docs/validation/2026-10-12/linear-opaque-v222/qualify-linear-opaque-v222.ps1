$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$src='C:\Users\tatef\AppData\Local\Temp\npu-graphics-probe-0146a2388f25';$build="$r\builds\present-umd-v222";$out="$r\vm\linear-opaque-v222"
if(Test-Path $out){throw 'Evidence exists'}
$manifest=Get-Content "$build\registered-build-report.json" -Raw|ConvertFrom-Json
if(!$manifest.clean_build){throw 'Clean build required'}
foreach($f in $manifest.binary_sha256.psobject.Properties){if((Get-FileHash "$build\Release\$($f.Name)").Hash -ne $f.Value){throw 'Binary hash mismatch'}}
foreach($f in $manifest.source_sha256.psobject.Properties){if((Get-FileHash "$src\$($f.Name)").Hash -ne $f.Value){throw 'Source hash mismatch'}}
$stream=Get-Content "$r\vm\indexed-dwm-stream-v112\report.json" -Raw|ConvertFrom-Json;if(!$stream.completed -or !$stream.configuration_removed){throw 'Live stream still active'}
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
 $env:PATH="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs;$env:PATH";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-2026-4-v209"
 $failed="$r\vm\v112-early-draw-1076-1.bin"
 if((Get-FileHash $failed).Hash -ne 'fece4f5bcd27e1bd8d9a9c25e617373e00ea71caf92d192a83ad665eb38ba571'){throw 'Opaque linear capture mismatch'}
 foreach($backend in 'CPU','NPU'){
  Run ('capture-'+$backend) 'nr_registered_draw_replay_probe.exe' ('--capture "'+$failed+'" --backend '+$backend+' --output "'+$out+'\capture-'+$backend+'.pixels" --repeat 2 --batch 4096') 360000
 }
 & "$src\venv\Scripts\python.exe" "$r\vm\compare-linear-opaque-v222.py" $out
 if($LASTEXITCODE){throw 'Opaque linear capture comparison failed'}
 foreach($variant in @(@{name='linear';args='NPU'},@{name='point';args='NPU --point'},@{name='point-opaque';args='NPU --point-opaque'},@{name='linear-opaque';args='NPU --linear-opaque'})){
  Run ($variant.name+'-NPU') 'nr_registered_linear_texture_draw_test.exe' $variant.args 360000
 }
 Run 'pipeline-NPU' "$src\venv\Scripts\python.exe" ('"'+$r+'\vm\check-linear-opaque-pipeline-v222.py" "'+$build+'\Release" "'+$out+'\pipeline"') 360000
 Run 'lab-NPU' 'nr_registered_lab_test.exe' 'NPU' 600000
 $legacy="$r\builds\present-umd-v222-legacy";$legacyManifest=Get-Content "$legacy\registered-build-report.json" -Raw|ConvertFrom-Json
 foreach($f in $legacyManifest.binary_sha256.psobject.Properties){if((Get-FileHash "$legacy\Release\$($f.Name)").Hash -ne $f.Value){throw 'Legacy binary mismatch'}}
 $env:PATH="$src\venv\Lib\site-packages\openvino\libs;$oldPath";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-v118"
 Run 'legacy-linear-opaque-NPU' "$legacy\Release\nr_registered_linear_texture_draw_test.exe" 'NPU --linear-opaque' 360000
 Run 'legacy-capture-NPU' "$legacy\Release\nr_registered_draw_replay_probe.exe" ('--capture "'+$failed+'" --backend NPU --output "'+$out+'\legacy-capture-NPU.pixels" --repeat 2 --batch 4096') 360000
 $oldBuild="$r\builds\present-umd-v221";$oldManifest=Get-Content "$oldBuild\registered-build-report.json" -Raw|ConvertFrom-Json
 foreach($f in $oldManifest.binary_sha256.psobject.Properties){if((Get-FileHash "$oldBuild\Release\$($f.Name)").Hash -ne $f.Value){throw 'Baseline binary mismatch'}}
 foreach($variant in 'baseline','candidate'){
  & "$src\venv\Scripts\python.exe" "$r\vm\prepare-runtime-warm-packets.py" "$out\$variant" *> "$out\$variant-preparation.log"
  if($LASTEXITCODE){throw 'Prewarm packet preparation failed'}
 }
 $env:PATH="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs;$oldPath";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-2026-4-v209"
 Run 'baseline-worker' "$oldBuild\Release\nr_registered_indexed_worker.exe" ('"'+$out+'\baseline" 17') 360000
 Run 'candidate-worker' 'nr_registered_indexed_worker.exe' ('"'+$out+'\candidate" 17') 360000
 & "$src\venv\Scripts\python.exe" "$r\vm\compare-runtime-warm-packets.py" "$out" *> "$out\prewarm-comparison.log"
 if($LASTEXITCODE){throw 'Prewarm comparison failed'}
 $comparison=Get-Content "$out\packet-comparison.json" -Raw|ConvertFrom-Json
 if(!$comparison.all_pixels_exact -or $comparison.pixel_checks.Count -ne 17){throw 'Prewarm pixels must remain exact'}
 & "$src\venv\Scripts\python.exe" "$r\vm\prepare-pipeline-cache-packets.py" "$out\captured-packets" *> "$out\captured-packets-preparation.log"
 if($LASTEXITCODE){throw 'Captured packet preparation failed'}
 Run 'captured-packets-worker' 'nr_registered_indexed_worker.exe' ('"'+$out+'\captured-packets" 63') 360000
 & "$src\venv\Scripts\python.exe" "$r\vm\compare-performance-packets-v216.py" "$r\vm\pipeline-cache-diagnostic-v3-results\baseline" "$out\captured-packets" *> "$out\captured-packets-comparison.log"
 if($LASTEXITCODE){throw 'Captured packet pixels or coverage changed'}
 & "$src\venv\Scripts\python.exe" "$r\vm\compare-linear-opaque-v222.py" $out
 if($LASTEXITCODE){throw 'Legacy capture comparison failed'}
 [ordered]@{build='present-umd-v222';all_checks_passed=$true;native_npu_execution=$true;desktop_offload_tested=$false;tahoma_performance_tested=$false}|ConvertTo-Json|Tee-Object "$out\report.json"
}finally{$env:PATH=$oldPath;$env:NR_RENDER_NPU_CACHE=$oldCache}
