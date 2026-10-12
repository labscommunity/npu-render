$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$src='C:\Users\tatef\AppData\Local\Temp\npu-graphics-probe-0146a2388f25';$build="$r\builds\present-umd-v223";$out="$r\vm\surface-capacity-v223"
if(Test-Path $out){throw 'Evidence exists'}
$manifest=Get-Content "$build\registered-build-report.json" -Raw|ConvertFrom-Json
if(!$manifest.clean_build){throw 'Clean build required'}
foreach($f in $manifest.binary_sha256.psobject.Properties){if((Get-FileHash "$build\Release\$($f.Name)").Hash -ne $f.Value){throw 'Binary hash mismatch'}}
foreach($f in $manifest.source_sha256.psobject.Properties){if((Get-FileHash "$src\$($f.Name)").Hash -ne $f.Value){throw 'Source hash mismatch'}}
$stream=Get-Content "$r\vm\indexed-dwm-stream-v113\report.json" -Raw|ConvertFrom-Json;if(!$stream.completed -or !$stream.configuration_removed){throw 'Live stream still active'}
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
 $env:PATH="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs;$oldPath";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-2026-4-v209"
 foreach($mode in @(@{name='triangle';args='NPU'},@{name='capacity';args='NPU --capacity'})){
  Run ($mode.name+'-NPU') 'nr_registered_triangle_texture_draw_test.exe' $mode.args 360000
 }
 foreach($group in 'targets','textures'){
  & "$src\venv\Scripts\python.exe" "$r\vm\check-surface-capacity-v3.py" prepare "$out\$group" "$build\Release" $group *> "$out\$group-preparation.log"
  if($LASTEXITCODE){throw 'Surface packet preparation failed'}
  Run ($group+'-worker') 'nr_registered_indexed_worker.exe' ('"'+$out+'\'+$group+'" 64') 360000
  & "$src\venv\Scripts\python.exe" "$r\vm\check-surface-capacity-v3.py" compare "$out\$group" *> "$out\$group-comparison.log"
  if($LASTEXITCODE){throw 'Surface pixel or coverage oracle failed'}
 }
 & "$src\venv\Scripts\python.exe" "$r\vm\check-vertex-capacity-packets-v1.py" prepare "$out\historical" *> "$out\historical-preparation.log"
 if($LASTEXITCODE){throw 'Historical preparation failed'}
 Run 'historical-worker' 'nr_registered_indexed_worker.exe' ('"'+$out+'\historical" 71') 360000
 & "$src\venv\Scripts\python.exe" "$r\vm\check-vertex-capacity-packets-v1.py" compare "$out\historical" *> "$out\historical-comparison.log"
 if($LASTEXITCODE){throw 'Historical exact comparison failed'}
 $records=Get-Content "$r\vm\v113-early-records.json" -Raw|ConvertFrom-Json
 foreach($record in $records){
  $capture="$r\vm\$($record.name)";if((Get-FileHash $capture).Hash -ne $record.sha256){throw 'Capture hash mismatch'}
  $name=[IO.Path]::GetFileNameWithoutExtension($capture)
  Run $name 'nr_registered_draw_replay_probe.exe' ('--capture "'+$capture+'" --backend NPU --output "'+$out+'\'+$name+'.pixels" --repeat 2 --batch 4096') 360000
 }
 & "$src\venv\Scripts\python.exe" "$r\vm\compare-v113-replays-v223.py" $out
 if($LASTEXITCODE){throw 'Failed desktop draw exact comparison failed'}
 Run 'lab-NPU' 'nr_registered_lab_test.exe' 'NPU' 600000
 $legacy="$r\builds\present-umd-v223-legacy";$legacyManifest=Get-Content "$legacy\registered-build-report.json" -Raw|ConvertFrom-Json
 if(!$legacyManifest.clean_build){throw 'Legacy clean build missing'}
 foreach($f in $legacyManifest.binary_sha256.psobject.Properties){if((Get-FileHash "$legacy\Release\$($f.Name)").Hash -ne $f.Value){throw 'Legacy binary mismatch'}}
 foreach($f in $manifest.source_sha256.psobject.Properties){if($legacyManifest.source_sha256.($f.Name) -ne $f.Value){throw 'Host and guest sources differ'}}
 $env:PATH="$src\venv\Lib\site-packages\openvino\libs;$oldPath";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-v118"
 Run 'legacy-triangle-NPU' "$legacy\Release\nr_registered_triangle_texture_draw_test.exe" 'NPU' 360000
 Run 'legacy-capacity-NPU' "$legacy\Release\nr_registered_triangle_texture_draw_test.exe" 'NPU --capacity' 360000
 [ordered]@{build='present-umd-v223';all_checks_passed=$true;native_npu_execution=$true;historical_packets=71;synthetic_packets=128;failed_desktop_captures=4;desktop_offload_tested=$false;tahoma_performance_tested=$false}|ConvertTo-Json|Tee-Object "$out\report.json"
}finally{$env:PATH=$oldPath;$env:NR_RENDER_NPU_CACHE=$oldCache}
