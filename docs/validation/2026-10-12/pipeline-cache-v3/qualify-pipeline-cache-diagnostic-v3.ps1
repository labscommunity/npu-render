$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$build="$r\builds\pipeline-cache-diagnostic-v3";$out="$r\vm\pipeline-cache-diagnostic-v3-results"
if(Test-Path $out){throw 'Evidence exists'}
$stream=Get-Content "$r\vm\indexed-dwm-stream-v109\report.json" -Raw|ConvertFrom-Json;if(!$stream.completed -or !$stream.configuration_removed){throw 'Desktop stream active'}
$manifest=Get-Content "$build\probe-build-report.json" -Raw|ConvertFrom-Json
foreach($f in $manifest.binary_hashes.psobject.Properties){if((Get-FileHash "$build\Release\$($f.Name)").Hash -ne $f.Value){throw 'Diagnostic binary mismatch'}}
foreach($f in $manifest.sources.psobject.Properties){if((Get-FileHash "$r\vm\pipeline-cache-diagnostic-v3\$($f.Name)").Hash -ne $f.Value){throw 'Diagnostic source mismatch'}}
New-Item -ItemType Directory $out|Out-Null;$oldPath=$env:PATH;$oldCache=$env:NR_RENDER_NPU_CACHE
$results=[Collections.Generic.List[object]]::new()
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
 foreach($variant in 'baseline','expanded','baseline-repeat'){
  & "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" "$r\vm\prepare-pipeline-cache-packets.py" "$out\$variant" *> "$out\$variant-preparation.log"
  if($LASTEXITCODE){throw 'Packet preparation failed'}
  $binary=if($variant -eq 'expanded'){'pipeline_cache_16.exe'}else{'pipeline_cache_4.exe'}
  Run ($variant+'-worker') $binary ('"'+$out+'\'+$variant+'" 63') 360000
  if($variant -ne 'baseline'){
   & "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" "$r\vm\compare-pipeline-cache-packets.py" $out $variant *> "$out\$variant-comparison.log"
   if($LASTEXITCODE){throw 'Exact receipt and pixel comparison failed'}
   Get-Content "$out\$variant-comparison.log"
  }
 }
 [ordered]@{diagnostic='pipeline-cache-v3';all_checks_passed=$true;packets_per_worker=63;desktop_offload_tested=$false;production_source_changed=$false}|ConvertTo-Json|Tee-Object "$out\report.json"
}finally{$env:PATH=$oldPath;$env:NR_RENDER_NPU_CACHE=$oldCache}
