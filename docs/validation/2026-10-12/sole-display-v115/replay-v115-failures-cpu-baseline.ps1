$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$out="$r\vm\v115-failure-replay"
$stream=Get-Content "$r\vm\indexed-dwm-stream-v115\report.json" -Raw|ConvertFrom-Json
$worker=Get-Process -Id $stream.worker_pid -ErrorAction SilentlyContinue;if($worker -and !$worker.HasExited){throw 'NPU stream worker is still active'}
if(Test-Path $out){throw 'Evidence exists'}
New-Item -ItemType Directory $out|Out-Null
$env:PATH="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs;$env:PATH"
$records=Get-Content "$r\vm\v115-early-records-snapshot.json" -Raw|ConvertFrom-Json
$results=@()
foreach($record in $records){
 $capture="$r\vm\$($record.name)";if((Get-FileHash $capture).Hash.ToLowerInvariant() -ne $record.sha256){throw 'Capture mismatch'}
 $name=[IO.Path]::GetFileNameWithoutExtension($capture);$info=[Diagnostics.ProcessStartInfo]::new();$info.FileName="$r\builds\present-umd-v223\Release\nr_registered_draw_replay_probe.exe";$info.Arguments='--capture "'+$capture+'" --backend CPU --output "'+$out+'\'+$name+'.pixels" --repeat 1 --batch 4096';$info.UseShellExecute=$false;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 $p=[Diagnostics.Process]::new();$p.StartInfo=$info;[void]$p.Start();$stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync();$timeout=!$p.WaitForExit(120000);if($timeout){$p.Kill();$p.WaitForExit()}
 [IO.File]::WriteAllText("$out\$name.stdout.log",$stdout.GetAwaiter().GetResult());[IO.File]::WriteAllText("$out\$name.stderr.log",$stderr.GetAwaiter().GetResult())
 $results+=@{name=$name;sha256=$record.sha256;backend='CPU';exit_code=$p.ExitCode;timed_out=$timeout};$p.Dispose();Get-Content "$out\$name.stderr.log";Get-Content "$out\$name.stdout.log" -Tail 3
 $results|ConvertTo-Json -Depth 5|Set-Content "$out\report.json"
}
