$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$out="$r\vm\vertex-capacity-v1"
if(Test-Path $out){throw 'Evidence exists'}
$stream=Get-Content "$r\vm\indexed-dwm-stream-v113\report.json" -Raw|ConvertFrom-Json;if(!$stream.completed -or !$stream.configuration_removed){throw 'Desktop stream active'}
foreach($variant in 'baseline','candidate'){
 $build="$r\builds\vertex-capacity-$variant-v1";$m=Get-Content "$build\probe-build-report.json" -Raw|ConvertFrom-Json
 if(!$m.clean_build -or !$m.cpu_triangles_passed){throw 'Clean qualified CPU build required'}
 foreach($f in $m.binaries){if((Get-FileHash "$build\Release\$($f.name)").Hash -ne $f.sha256){throw 'Diagnostic binary mismatch'}}
 foreach($f in $m.runtime){if((Get-FileHash "$($m.runtime_directory)\$($f.name)").Hash -ne $f.sha256){throw 'Runtime mismatch'}}
 foreach($f in $m.sources.psobject.Properties){if((Get-FileHash "$($m.source_root)\$($f.Name)").Hash -ne $f.Value){throw 'Diagnostic source mismatch'}}
}
New-Item -ItemType Directory $out|Out-Null;$oldPath=$env:PATH;$oldCache=$env:NR_RENDER_NPU_CACHE
$results=[Collections.Generic.List[object]]::new();$success=$false
function Run([string]$name,[string]$binary,[string]$arguments,[int]$limit=240000){
 $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=if([IO.Path]::IsPathRooted($binary)){$binary}else{"$build\Release\$binary"};$info.Arguments=$arguments;$info.UseShellExecute=$false;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 $process=[Diagnostics.Process]::new();$process.StartInfo=$info
 if(!$process.Start()){throw 'Cannot start qualification'}
 $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync();$watch=[Diagnostics.Stopwatch]::StartNew();$timedOut=$false
 $peakPrivate=0L;$peakWorking=0L
 while(!$process.WaitForExit(100)){$process.Refresh();$peakPrivate=[Math]::Max($peakPrivate,$process.PrivateMemorySize64);$peakWorking=[Math]::Max($peakWorking,$process.WorkingSet64);if($watch.ElapsedMilliseconds -gt $limit){$timedOut=$true;$process.Kill();$process.WaitForExit();break}}

 $code=$process.ExitCode;[IO.File]::WriteAllText("$out\$name.stdout.jsonl",$stdout.GetAwaiter().GetResult());[IO.File]::WriteAllText("$out\$name.stderr.log",$stderr.GetAwaiter().GetResult())
 $entry=[ordered]@{name=$name;exit_code=$code;timed_out=$timedOut;elapsed_ms=$watch.ElapsedMilliseconds;passed=(!$timedOut -and $code -eq 0);peak_private_bytes=$peakPrivate;peak_working_bytes=$peakWorking};$results.Add($entry);$results|ConvertTo-Json|Set-Content "$out\runs.json"
 Get-Content "$out\$name.stdout.jsonl" -Tail 3;Get-Content "$out\$name.stderr.log" -Tail 4;$process.Dispose()
 if(!$entry.passed){throw "Qualification failed: $name"}
}
try{
 $env:PATH="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs;$env:PATH";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-2026-4-v209"
 foreach($variant in 'baseline','candidate'){
  $build="$r\builds\vertex-capacity-$variant-v1"
  Run ($variant+'-triangle-NPU') 'nr_registered_triangle_texture_draw_test.exe' 'NPU' 360000
  if($variant -eq 'candidate'){Run 'candidate-capacity-NPU' 'nr_registered_triangle_texture_draw_test.exe' 'NPU --capacity' 360000}
 }
 foreach($variant in 'baseline','candidate','baseline-repeat'){
  $which=if($variant -eq 'candidate'){'candidate'}else{'baseline'};$build="$r\builds\vertex-capacity-$which-v1"
  & "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" "$r\vm\check-vertex-capacity-packets-v1.py" prepare "$out\$variant" *> "$out\$variant-preparation.log"
  if($LASTEXITCODE){throw 'Packet preparation failed'}
  Run ($variant+'-worker') 'nr_registered_indexed_worker.exe' ('"'+$out+'\'+$variant+'" 71') 360000
  & "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" "$r\vm\check-vertex-capacity-packets-v1.py" compare "$out\$variant" *> "$out\$variant-comparison.log"
  if($LASTEXITCODE){throw 'Exact receipt and pixel comparison failed'}
 }
 $success=$true
 [ordered]@{diagnostic='vertex-capacity-v1';all_checks_passed=$true;packets_per_worker=71;desktop_offload_tested=$false;tahoma_performance_tested=$false;production_source_changed=$false;memory_excludes_driver_device=$true}|ConvertTo-Json|Tee-Object "$out\report.json"
}catch{
 $failure=$_.Exception.Message
 try{$build="$r\builds\present-umd-v222";Run 'qualified-baseline-recovery' 'nr_registered_triangle_texture_draw_test.exe' 'NPU' 360000;$recovery=$true}catch{$recovery=$false}
 [ordered]@{diagnostic='vertex-capacity-v1';all_checks_passed=$false;failure=$failure;qualified_baseline_recovered=$recovery;production_source_changed=$false;desktop_offload_tested=$false}|ConvertTo-Json|Set-Content "$out\report.json"
 throw $failure
}finally{$env:PATH=$oldPath;$env:NR_RENDER_NPU_CACHE=$oldCache}
