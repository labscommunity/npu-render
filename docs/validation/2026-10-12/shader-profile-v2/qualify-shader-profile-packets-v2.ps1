$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$src='C:\Users\tatef\AppData\Local\Temp\npu-graphics-probe-0146a2388f25';$build="$r\builds\shader-profile-diagnostic-v2";$out="$r\vm\shader-profile-packets-v2"
if(Test-Path $out){throw 'Evidence exists'}
$manifest=Get-Content "$build\probe-build-report.json" -Raw|ConvertFrom-Json
if((Get-FileHash "$build\Release\nr_registered_indexed_worker.exe").Hash -ne $manifest.worker_sha256){throw 'Diagnostic worker mismatch'}
foreach($f in $manifest.sources.psobject.Properties){if((Get-FileHash "$r\vm\shader-profile-diagnostic-v2\$($f.Name)").Hash -ne $f.Value){throw 'Diagnostic source mismatch'}}
foreach($f in $manifest.runtime){if((Get-FileHash "$($manifest.runtime_directory)\$($f.name)").Hash -ne $f.sha256){throw 'Runtime mismatch'}}
$stream=Get-Content "$r\vm\indexed-dwm-stream-v110\report.json" -Raw|ConvertFrom-Json;if(!$stream.completed -or !$stream.configuration_removed){throw 'Live stream still active'}
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
 & "$src\venv\Scripts\python.exe" "$r\vm\prepare-pipeline-cache-packets.py" "$out\captured-packets" *> "$out\captured-packets-preparation.log"
 if($LASTEXITCODE){throw 'Packet preparation failed'}
 Run 'profile-worker' 'nr_registered_indexed_worker.exe' ('"'+$out+'\captured-packets" 63') 360000
 & "$src\venv\Scripts\python.exe" "$r\vm\compare-performance-packets-v216.py" "$r\vm\pipeline-cache-diagnostic-v3-results\baseline" "$out\captured-packets" *> "$out\captured-packets-comparison.log"
 if($LASTEXITCODE){throw 'Captured packet comparison failed'}
 [ordered]@{build='shader-profile-diagnostic-v2';all_checks_passed=$true;native_npu_execution=$true;desktop_offload_tested=$false;tahoma_performance_tested=$false}|ConvertTo-Json|Set-Content "$out\report.json"
}finally{$env:PATH=$oldPath;$env:NR_RENDER_NPU_CACHE=$oldCache}
