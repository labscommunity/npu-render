$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$src="$r\vm\model-reuse-diagnostic-v2";$build="$r\builds\model-reuse-diagnostic-v2";$out="$r\vm\model-reuse-diagnostic-v2-results"
if(Test-Path $out){throw 'Evidence exists'}
$stream=Get-Content "$r\vm\indexed-dwm-stream-v107\report.json" -Raw|ConvertFrom-Json;if(!$stream.completed -or !$stream.configuration_removed){throw 'Desktop stream active'}
$manifest=Get-Content "$build\registered-build-report.json" -Raw|ConvertFrom-Json;if(!$manifest.clean_build){throw 'Clean build required'}
foreach($f in $manifest.binary_sha256.psobject.Properties){if((Get-FileHash "$build\Release\$($f.Name)").Hash -ne $f.Value){throw 'Diagnostic binary mismatch'}}
foreach($f in $manifest.source_sha256.psobject.Properties){if((Get-FileHash "$src\$($f.Name)").Hash -ne $f.Value){throw 'Diagnostic source mismatch'}}
$baseline="$r\builds\present-umd-v209";$old=Get-Content "$baseline\registered-build-report.json" -Raw|ConvertFrom-Json
foreach($f in $old.binary_sha256.psobject.Properties){if((Get-FileHash "$baseline\Release\$($f.Name)").Hash -ne $f.Value){throw 'Baseline binary mismatch'}}
New-Item -ItemType Directory $out|Out-Null;$oldPath=$env:PATH;$oldCache=$env:NR_RENDER_NPU_CACHE;$oldReuse=$env:NR_RENDER_REUSE_COMPILED
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
 $env:NR_RENDER_REUSE_COMPILED='1'
 Run 'model-pool-NPU' 'nr_registered_model_pool_test.exe' 'NPU' 240000
 foreach($variant in 'baseline','disabled','enabled','disabled-repeat'){
  & "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" "$r\vm\prepare-model-reuse-packets.py" "$out\$variant" *> "$out\$variant-preparation.log"
  if($LASTEXITCODE){throw 'Packet preparation failed'}
  $env:NR_RENDER_REUSE_COMPILED=if($variant -eq 'enabled'){'1'}else{'0'}
  $binary=if($variant -eq 'baseline'){"$baseline\Release\nr_registered_indexed_worker.exe"}else{'nr_registered_indexed_worker.exe'}
  Run ($variant+'-worker') $binary ('"'+$out+'\'+$variant+'" 42') 360000
  if($variant -ne 'baseline'){
   & "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" "$r\vm\compare-model-reuse-packets.py" $out $variant *> "$out\$variant-comparison.log"
   if($LASTEXITCODE){throw 'Exact receipt and pixel comparison failed'}
   Get-Content "$out\$variant-comparison.log"
  }
 }
 [ordered]@{diagnostic='model-reuse-v2';all_checks_passed=$true;packets_per_worker=42;desktop_offload_tested=$false;production_source_changed=$false}|ConvertTo-Json|Tee-Object "$out\report.json"
}finally{$env:PATH=$oldPath;$env:NR_RENDER_NPU_CACHE=$oldCache;$env:NR_RENDER_REUSE_COMPILED=$oldReuse}
