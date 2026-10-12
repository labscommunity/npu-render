$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$src='C:\Users\tatef\AppData\Local\Temp\npu-graphics-probe-0146a2388f25';$build="$r\builds\present-umd-v221";$out="$r\vm\border-v221"
if(Test-Path $out){throw 'Evidence exists'}
$manifest=Get-Content "$build\registered-build-report.json" -Raw|ConvertFrom-Json
if(!$manifest.clean_build){throw 'Clean build required'}
foreach($f in $manifest.binary_sha256.psobject.Properties){if((Get-FileHash "$build\Release\$($f.Name)").Hash -ne $f.Value){throw 'Binary hash mismatch'}}
foreach($f in $manifest.source_sha256.psobject.Properties){if((Get-FileHash "$src\$($f.Name)").Hash -ne $f.Value){throw 'Source hash mismatch'}}
$stream=Get-Content "$r\vm\indexed-dwm-stream-v111\report.json" -Raw|ConvertFrom-Json;if(!$stream.completed -or !$stream.configuration_removed){throw 'Live stream still active'}
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
 $env:PATH="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs;$env:PATH";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-2026-4-v209";New-Item -ItemType Directory $env:NR_RENDER_NPU_CACHE -ErrorAction SilentlyContinue|Out-Null
 Run 'border-samples-NPU' 'nr_registered_border_test.exe' 'NPU samples' 600000
 Run 'border-raster-NPU' 'nr_registered_border_test.exe' ('NPU raster --emit-packet "'+$out+'\border-packet.bin"') 360000
 Run 'border-pipeline-NPU' "$src\venv\Scripts\python.exe" ('"'+$r+'\vm\check-border-pipeline-v221.py" "'+$build+'\Release" "'+$out+'\border-packet.bin" "'+$out+'\border-pipeline"') 360000
 $failed="$r\vm\v111-early-draw-1076-1.bin"
 if((Get-FileHash $failed).Hash -ne '5094971fc10e251f66da89631740a4c5b932ae5bb891d842776597e4e35bb246'){throw 'Border capture mismatch'}
 foreach($backend in 'CPU','NPU'){
  Run ('border-capture-'+$backend) 'nr_registered_draw_replay_probe.exe' ('--capture "'+$failed+'" --backend '+$backend+' --output "'+$out+'\border-'+$backend+'.pixels" --repeat 2 --batch 4096') 360000
 }
 & "$src\venv\Scripts\python.exe" "$r\vm\compare-border-v221.py" $out
 if($LASTEXITCODE){throw 'Border capture differs from CPU/NPU oracle contract'}
 foreach($section in 'desktop','fade','three','three-base'){
  Run ('compositor-'+$section+'-NPU') 'nr_registered_compositor_test.exe' ('NPU '+$section) 360000
 }
 foreach($fixture in @(@{name='1041';file='v105-early-draw-5112-1.bin';hash='2bd0001ec19439a2ba859d28429ca68fdc020e0751ad91a1e7953881911f3a07'},@{name='1061';file='v102-early-draw-5112-1.bin';hash='8e634f6ea14a33f0204d2aff4c1f1b1033db814c1959d04a1c5717d4bd63e44c'})){
  if((Get-FileHash "$r\vm\$($fixture.file)").Hash -ne $fixture.hash){throw 'Full capture mismatch'}
  foreach($backend in 'CPU','NPU'){
   $name="$($fixture.name)-$backend"
   Run $name 'nr_registered_draw_replay_probe.exe' ('--capture "'+$r+'\vm\'+$fixture.file+'" --backend '+$backend+' --output "'+$out+'\'+$name+'.pixels" --repeat 2 --batch 8192') 360000
  }
 }
 & "$src\venv\Scripts\python.exe" "$r\vm\compare-performance-v216.py" $out
 if($LASTEXITCODE){throw 'Historical complete-draw pixels changed'}
 Run 'point-NPU' 'nr_registered_point_texture_draw_test.exe' 'NPU' 360000
 Run 'triangles-NPU' 'nr_registered_triangle_texture_draw_test.exe' 'NPU' 360000
 $capture="$r\vm\v108-early-draw-1072-1.bin"
 if((Get-FileHash $capture).Hash -ne '8f2ab21d009d15597525ced343b6e7af9fd858266855402f5017c50470bc19c6'){throw 'Original capture mismatch'}
 foreach($backend in 'CPU','NPU'){
  Run ('capture-'+$backend) 'nr_registered_draw_replay_probe.exe' ('--capture "'+$capture+'" --backend '+$backend+' --output "'+$out+'\'+$backend+'.pixels" --repeat 2 --batch 4096')
 }
 & "$src\venv\Scripts\python.exe" "$r\vm\compare-crop-v212.py" "$out"
 if($LASTEXITCODE -ne 0){throw 'CPU/NPU pixel comparison failed'}
 Run 'lab-NPU' 'nr_registered_lab_test.exe' 'NPU' 600000
 Run 'point-pipeline-NPU' "$src\venv\Scripts\python.exe" ('"'+$r+'\vm\check-point-pipeline-v212.py" "'+$build+'\Release" "'+$out+'\point-pipeline"') 360000

 $legacy="$r\builds\present-umd-v221-legacy";$legacyManifest=Get-Content "$legacy\registered-build-report.json" -Raw|ConvertFrom-Json
 foreach($f in $legacyManifest.binary_sha256.psobject.Properties){if((Get-FileHash "$legacy\Release\$($f.Name)").Hash -ne $f.Value){throw 'Legacy compatibility binary mismatch'}}
 $env:PATH="$src\venv\Lib\site-packages\openvino\libs;$oldPath";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-v118"
 Run 'legacy-border-raster-NPU' "$legacy\Release\nr_registered_border_test.exe" 'NPU raster' 360000
 Run 'legacy-point-NPU' "$legacy\Release\nr_registered_point_texture_draw_test.exe" 'NPU' 360000
 Run 'legacy-triangles-NPU' "$legacy\Release\nr_registered_triangle_texture_draw_test.exe" 'NPU' 600000
 $oldBuild="$r\builds\present-umd-v204";$oldManifest=Get-Content "$oldBuild\registered-build-report.json" -Raw|ConvertFrom-Json
 foreach($f in $oldManifest.binary_sha256.psobject.Properties){if((Get-FileHash "$oldBuild\Release\$($f.Name)").Hash -ne $f.Value){throw 'Baseline binary mismatch'}}
 foreach($variant in 'baseline','candidate'){
  & "$src\venv\Scripts\python.exe" "$r\vm\prepare-runtime-warm-packets.py" "$out\$variant" *> "$out\$variant-preparation.log"
  if($LASTEXITCODE -ne 0){throw 'Prewarm packet preparation failed'}
 }
 $env:PATH="$src\venv\Lib\site-packages\openvino\libs;$oldPath";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-v118"
 Run 'baseline-worker' "$oldBuild\Release\nr_registered_indexed_worker.exe" ('"'+$out+'\baseline" 17') 360000
 $env:PATH="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs;$oldPath";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-2026-4-v209"
 Run 'candidate-worker' 'nr_registered_indexed_worker.exe' ('"'+$out+'\candidate" 17') 360000
 & "$src\venv\Scripts\python.exe" "$r\vm\compare-runtime-warm-packets.py" "$out" *> "$out\prewarm-comparison.log"
 if($LASTEXITCODE -ne 0){throw 'Prewarm pixel comparison failed'}
 $comparison=Get-Content "$out\packet-comparison.json" -Raw|ConvertFrom-Json
 if(!$comparison.all_pixels_exact -or $comparison.pixel_checks.Count -ne 17){throw 'All prewarm pixels must remain exact for installed qualification'}
 Get-Content "$out\packet-comparison.json"
 & "$src\venv\Scripts\python.exe" "$r\vm\prepare-pipeline-cache-packets.py" "$out\captured-packets" *> "$out\captured-packets-preparation.log"
 if($LASTEXITCODE){throw 'Captured packet preparation failed'}
 Run 'captured-packets-worker' 'nr_registered_indexed_worker.exe' ('"'+$out+'\captured-packets" 63') 360000
 & "$src\venv\Scripts\python.exe" "$r\vm\compare-performance-packets-v216.py" "$r\vm\pipeline-cache-diagnostic-v3-results\baseline" "$out\captured-packets" *> "$out\captured-packets-comparison.log"
 if($LASTEXITCODE){throw 'Captured packet pixels or coverage changed'}
 [ordered]@{build='present-umd-v221';all_checks_passed=$true;native_npu_execution=$true;desktop_offload_tested=$false;tahoma_performance_tested=$false}|ConvertTo-Json|Tee-Object "$out\report.json"
}finally{$env:PATH=$oldPath;$env:NR_RENDER_NPU_CACHE=$oldCache}
