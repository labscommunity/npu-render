$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$src='C:\Users\tatef\AppData\Local\Temp\npu-graphics-probe-0146a2388f25';$build="$r\builds\present-umd-v198";$out="$r\vm\replay-v102-dwm"
if(Test-Path $out){throw 'Evidence exists'}
if((Get-FileHash "$r\vm\v102-early-draw-5112-1.bin").Hash -ne '8e634f6ea14a33f0204d2aff4c1f1b1033db814c1959d04a1c5717d4bd63e44c'){throw 'Original capture mismatch'}
$manifest=Get-Content "$build\registered-build-report.json" -Raw|ConvertFrom-Json
foreach($f in $manifest.binary_sha256.psobject.Properties){if((Get-FileHash "$build\Release\$($f.Name)").Hash -ne $f.Value){throw 'Binary mismatch'}}
$stream=Get-Content "$r\vm\indexed-dwm-stream-v102\report.json" -Raw|ConvertFrom-Json;if(!$stream.completed -or !$stream.configuration_removed){throw 'Live stream still active'}
New-Item -ItemType Directory $out|Out-Null;$oldPath=$env:PATH;$oldCache=$env:NR_RENDER_NPU_CACHE;$results=[Collections.Generic.List[object]]::new()
try {
 $env:PATH="$src\venv\Lib\site-packages\openvino\libs;$env:PATH";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-v118"
 foreach($case in @(@('CPU',512),@('NPU',4096),@('NPU',512))){$backend=$case[0];$batch=$case[1];$name=$backend+'-'+$batch
 # one bounded experiment for each explicit graph shape

  $i=[Diagnostics.ProcessStartInfo]::new();$i.FileName="$build\Release\nr_registered_draw_replay_probe.exe";$i.Arguments='--capture "'+$r+'\vm\v102-early-draw-5112-1.bin" --backend '+$backend+' --output "'+$out+'\'+$name+'.pixels" --repeat 1 --batch '+$batch;$i.UseShellExecute=$false;$i.RedirectStandardOutput=$true;$i.RedirectStandardError=$true
  $p=[Diagnostics.Process]::new();$p.StartInfo=$i;if(!$p.Start()){throw 'Replay launch failed'};$stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync();$watch=[Diagnostics.Stopwatch]::StartNew();$timeout=$false
  if(!$p.WaitForExit(180000)){$timeout=$true;$p.Kill();$p.WaitForExit()}
  $code=$p.ExitCode;[IO.File]::WriteAllText("$out\$name.stdout.jsonl",$stdout.GetAwaiter().GetResult());[IO.File]::WriteAllText("$out\$name.stderr.log",$stderr.GetAwaiter().GetResult());$p.Dispose()
  $results.Add([ordered]@{backend=$backend;batch=$batch;exit_code=$code;timed_out=$timeout;elapsed_ms=$watch.ElapsedMilliseconds});$results|ConvertTo-Json|Set-Content "$out\runs.json";Get-Content "$out\$name.stdout.jsonl" -Tail 3;Get-Content "$out\$name.stderr.log"
 }
} finally {$env:PATH=$oldPath;$env:NR_RENDER_NPU_CACHE=$oldCache}
