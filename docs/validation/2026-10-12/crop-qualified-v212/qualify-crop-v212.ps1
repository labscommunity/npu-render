$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$src='C:\Users\tatef\AppData\Local\Temp\npu-graphics-probe-0146a2388f25';$build="$r\builds\present-umd-v212";$out="$r\vm\crop-v212"
if(Test-Path $out){throw 'Evidence exists'}
$manifest=Get-Content "$build\registered-build-report.json" -Raw|ConvertFrom-Json
if(!$manifest.clean_build){throw 'Clean build required'}
foreach($f in $manifest.binary_sha256.psobject.Properties){if((Get-FileHash "$build\Release\$($f.Name)").Hash -ne $f.Value){throw 'Binary hash mismatch'}}
foreach($f in $manifest.source_sha256.psobject.Properties){if((Get-FileHash "$src\$($f.Name)").Hash -ne $f.Value){throw 'Source hash mismatch'}}
$stream=Get-Content "$r\vm\indexed-dwm-stream-v108\report.json" -Raw|ConvertFrom-Json;if(!$stream.completed -or !$stream.configuration_removed){throw 'Live stream still active'}
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

 $legacy="$r\builds\present-umd-v212-legacy";$legacyManifest=Get-Content "$legacy\registered-build-report.json" -Raw|ConvertFrom-Json
 foreach($f in $legacyManifest.binary_sha256.psobject.Properties){if((Get-FileHash "$legacy\Release\$($f.Name)").Hash -ne $f.Value){throw 'Legacy compatibility binary mismatch'}}
 $env:PATH="$src\venv\Lib\site-packages\openvino\libs;$oldPath";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-v118"
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
 [ordered]@{build='present-umd-v212';all_checks_passed=$true;native_npu_execution=$true;desktop_offload_tested=$false;tahoma_performance_tested=$false}|ConvertTo-Json|Tee-Object "$out\report.json"
}finally{$env:PATH=$oldPath;$env:NR_RENDER_NPU_CACHE=$oldCache}
