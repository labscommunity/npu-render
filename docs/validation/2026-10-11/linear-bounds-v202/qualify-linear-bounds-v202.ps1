$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$src='C:\Users\tatef\AppData\Local\Temp\npu-graphics-probe-0146a2388f25';$build="$r\builds\present-umd-v202";$out="$r\vm\linear-bounds-v202"
if(Test-Path $out){throw 'Evidence exists'}
$manifest=Get-Content "$build\registered-build-report.json" -Raw|ConvertFrom-Json
if(!$manifest.clean_build){throw 'Clean build required'}
foreach($f in $manifest.binary_sha256.psobject.Properties){if((Get-FileHash "$build\Release\$($f.Name)").Hash -ne $f.Value){throw 'Binary hash mismatch'}}
foreach($f in $manifest.source_sha256.psobject.Properties){if((Get-FileHash "$src\$($f.Name)").Hash -ne $f.Value){throw 'Source hash mismatch'}}
$stream=Get-Content "$r\vm\indexed-dwm-stream-v103\report.json" -Raw|ConvertFrom-Json;if(!$stream.completed -or !$stream.configuration_removed){throw 'Live stream still active'}
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
 $env:PATH="$src\venv\Lib\site-packages\openvino\libs;$env:PATH";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-v118"
 Run 'linear-NPU' 'nr_registered_linear_texture_draw_test.exe' 'NPU'
 Run 'modulated-point-NPU' 'nr_registered_linear_texture_draw_test.exe' 'NPU --point'
 Run 'opaque-point-NPU' 'nr_registered_linear_texture_draw_test.exe' 'NPU --point-opaque'
 foreach($test in @(@('triangles','nr_registered_triangle_texture_draw_test.exe'),@('derivatives','nr_registered_derivative_test.exe'),@('compositor','nr_registered_compositor_test.exe'),@('typed-load','nr_registered_typed_load_test.exe'),@('loops','nr_registered_loop_test.exe'),@('masks','nr_registered_mask_test.exe'),@('mirror','nr_registered_mirror_test.exe'))){Run ($test[0]+'-NPU') $test[1] 'NPU'}
 foreach($case in @(@('1092','1','c77ed814e572be6bd4840567f4cbd8b4002e79f9e8456bd0ab021f9a6961b60b'),@('5112','2','cc65c3eafa5754e943f0fe074c28e1918664c208452ed362de552de549c508e4'))){
  $capture="$r\vm\v100-early-draw-$($case[0])-$($case[1]).bin"
  if((Get-FileHash $capture).Hash -ne $case[2]){throw 'Original capture mismatch'}
  foreach($backend in 'CPU','NPU'){
   Run ('capture-'+$case[0]+'-'+$backend) 'nr_registered_draw_replay_probe.exe' ('--capture "'+$capture+'" --backend '+$backend+' --output "'+$out+'\'+$case[0]+'-'+$backend+'.pixels"')
  }
  if((Get-FileHash "$out\$($case[0])-CPU.pixels").Hash -ne (Get-FileHash "$out\$($case[0])-NPU.pixels").Hash){throw 'CPU/NPU output mismatch'}
 }
 Run 'lab-NPU' 'nr_registered_lab_test.exe' 'NPU'
 $oldBuild="$r\builds\present-umd-v201";$oldManifest=Get-Content "$oldBuild\registered-build-report.json" -Raw|ConvertFrom-Json
 foreach($f in $oldManifest.binary_sha256.psobject.Properties){if((Get-FileHash "$oldBuild\Release\$($f.Name)").Hash -ne $f.Value){throw 'Baseline binary mismatch'}}
 foreach($variant in 'baseline','candidate'){
  & "$src\venv\Scripts\python.exe" "$r\vm\prepare-linear-bounds-packets.py" "$out\$variant" *> "$out\$variant-preparation.log"
  if($LASTEXITCODE -ne 0){throw 'Packet preparation failed'}
 }
 Run 'baseline-worker' "$oldBuild\Release\nr_registered_indexed_worker.exe" ('"'+$out+'\baseline" 12')
 Run 'candidate-worker' 'nr_registered_indexed_worker.exe' ('"'+$out+'\candidate" 12')
 & "$src\venv\Scripts\python.exe" "$r\vm\compare-linear-bounds-packets.py" "$out" *> "$out\comparison.log"
 if($LASTEXITCODE -ne 0){throw 'Clipped packet comparison failed'}
 Get-Content "$out\packet-comparison.json"
 [ordered]@{build='present-umd-v202';all_checks_passed=$true;native_npu_execution=$true;desktop_offload_tested=$false;tahoma_performance_tested=$false}|ConvertTo-Json|Tee-Object "$out\report.json"
}finally{$env:PATH=$oldPath;$env:NR_RENDER_NPU_CACHE=$oldCache}
