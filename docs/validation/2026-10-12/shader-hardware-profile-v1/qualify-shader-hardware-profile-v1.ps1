$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$build="$r\builds\shader-hardware-profile-v1";$out="$r\vm\shader-hardware-profile-v1-results"
if(Test-Path $out){throw 'Evidence exists'}
$stream=Get-Content "$r\vm\indexed-dwm-stream-v111\report.json" -Raw|ConvertFrom-Json;if(!$stream.completed -or !$stream.configuration_removed){throw 'Desktop stream active'}
$manifest=Get-Content "$build\probe-build-report.json" -Raw|ConvertFrom-Json
if((Get-FileHash "$build\Release\nr_registered_draw_replay_probe.exe").Hash -ne $manifest.binary_sha256){throw 'Diagnostic binary mismatch'}
foreach($f in $manifest.sources.psobject.Properties){if((Get-FileHash "$r\vm\shader-hardware-profile-v1\$($f.Name)").Hash -ne $f.Value){throw 'Diagnostic source mismatch'}}
foreach($f in $manifest.runtime){if((Get-FileHash "$($manifest.runtime_directory)\$($f.name)").Hash -ne $f.sha256){throw 'Runtime mismatch'}}
New-Item -ItemType Directory $out|Out-Null;$oldPath=$env:PATH;$oldCache=$env:NR_RENDER_NPU_CACHE;$runs=[Collections.Generic.List[object]]::new()
try{
 $env:PATH="$($manifest.runtime_directory);$oldPath";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-2026-4-v209"
 foreach($capture in @(@{name='1041';file='v105-early-draw-5112-1.bin';hash='2bd0001ec19439a2ba859d28429ca68fdc020e0751ad91a1e7953881911f3a07'},@{name='1061';file='v102-early-draw-5112-1.bin';hash='8e634f6ea14a33f0204d2aff4c1f1b1033db814c1959d04a1c5717d4bd63e44c'})){
  $file="$r\vm\$($capture.file)";if((Get-FileHash $file).Hash -ne $capture.hash){throw 'Original capture mismatch'}
  foreach($backend in 'NPU'){
   $batches=@(8192);$i=0
   foreach($batch in $batches){
    ++$i;$name="$($capture.name)-$backend-$batch-$i"
    $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName="$build\Release\nr_registered_draw_replay_probe.exe";$info.Arguments='--capture "'+$file+'" --backend '+$backend+' --output "'+$out+'\'+$name+'.pixels" --repeat 2 --batch '+$batch;$info.UseShellExecute=$false;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true;$info.EnvironmentVariables["NPU_PRINT_PROFILING"]="JSON";$info.EnvironmentVariables["NPU_PROFILING_OUTPUT_FILE"]="$out\$name.engine.json"
    $process=[Diagnostics.Process]::new();$process.StartInfo=$info;if(!$process.Start()){throw 'Cannot start replay'}
    $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync();$watch=[Diagnostics.Stopwatch]::StartNew();$timeout=$false
    if(!$process.WaitForExit(360000)){$timeout=$true;$process.Kill();$process.WaitForExit()}
    $o=$stdout.GetAwaiter().GetResult();$e=$stderr.GetAwaiter().GetResult();[IO.File]::WriteAllText("$out\$name.stdout.jsonl",$o);[IO.File]::WriteAllText("$out\$name.stderr.log",$e)
    $row=[ordered]@{name=$name;capture=$capture.name;capture_sha256=$capture.hash;backend=$backend;batch=$batch;exit_code=$process.ExitCode;timed_out=$timeout;elapsed_ms=$watch.ElapsedMilliseconds;passed=(!$timeout -and $process.ExitCode -eq 0)};$runs.Add($row);$runs|ConvertTo-Json|Set-Content "$out\runs.json";$row|ConvertTo-Json -Compress
    Get-Content "$out\$name.stdout.jsonl" -Tail 7;Get-Content "$out\$name.stderr.log" -Tail 3;$process.Dispose()
    if(!$row.passed){throw 'Complete draw replay failed'}
   }
  }
 }
 & "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" "$r\vm\compare-shader-hardware-profile-v1.py" $out
 if($LASTEXITCODE){throw 'Pixel comparison failed'}
 [ordered]@{completed=$true;all_checks_passed=$true;shipping_source_changed=$false;desktop_offload_tested=$false;tahoma_performance_tested=$false}|ConvertTo-Json|Set-Content "$out\report.json"
}finally{$env:PATH=$oldPath;$env:NR_RENDER_NPU_CACHE=$oldCache}
