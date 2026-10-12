$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$build="$r\builds\border-oracle-diagnostic-v1";$out="$r\vm\border-oracle-diagnostic-v1-results"
if(!(Test-Path "$build\probe-build-report.json")){throw 'Diagnostic build incomplete'}
if(Test-Path $out){throw 'Evidence exists'}
New-Item -ItemType Directory $out|Out-Null
$oldPath=$env:PATH;$oldCache=$env:NR_RENDER_NPU_CACHE;$results=[Collections.Generic.List[object]]::new()
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
try {
$env:PATH="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs;$env:PATH";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-2026-4-v209"
foreach($backend in 'CPU','NPU') {
Run ('border-capture-'+$backend) 'nr_registered_draw_replay_probe.exe' ('--capture "'+$r+'\vm\v111-early-draw-1076-1.bin" --backend '+$backend+' --output "'+$out+'\border-'+$backend+'.pixels" --repeat 2 --batch 4096') 360000
}
& "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" "$r\vm\compare-border-v220.py" $out
if($LASTEXITCODE){throw 'Pixel comparison failed'}
}finally{$env:PATH=$oldPath;$env:NR_RENDER_NPU_CACHE=$oldCache}
