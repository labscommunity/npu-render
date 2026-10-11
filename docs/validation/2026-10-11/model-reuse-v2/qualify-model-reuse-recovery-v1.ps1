$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$build="$r\builds\present-umd-v209";$out="$r\vm\model-reuse-recovery-v1"
if(Test-Path $out){throw 'Evidence exists'}
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
 $env:PATH="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs;$env:PATH";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-2026-4-v209";$env:NR_RENDER_REUSE_COMPILED='0'
 Copy-Item "$r\vm\model-reuse-diagnostic-v2-results\baseline" "$out\baseline" -Recurse
 & "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" "$r\vm\prepare-model-reuse-packets.py" "$out\recovered" *> "$out\preparation.log"
 if($LASTEXITCODE){throw 'Packet preparation failed'}
 Run 'recovery-worker' 'nr_registered_indexed_worker.exe' ('"'+$out+'\recovered" 42') 360000
 & "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" "$r\vm\compare-model-reuse-packets.py" $out recovered *> "$out\comparison.log"
 if($LASTEXITCODE){throw 'Recovery output mismatch'}
 Get-Content "$out\comparison.log"
 [ordered]@{recovery_passed=$true;qualified_build='present-umd-v209';physical_restart=$false;desktop_offload_tested=$false}|ConvertTo-Json|Tee-Object "$out\report.json"
}finally{$env:PATH=$oldPath;$env:NR_RENDER_NPU_CACHE=$oldCache;$env:NR_RENDER_REUSE_COMPILED=$oldReuse}
