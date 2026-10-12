$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$out="$r\vm\v113-failure-replay-npu"
$worker=Get-Process -Id 8284 -ErrorAction SilentlyContinue;if($worker -and !$worker.HasExited){throw 'Owned trial worker still active'}
$zip="$r\vm\v113-after-cleanup.zip";Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive=[IO.Compression.ZipFile]::OpenRead($zip)
try{$entry=$archive.Entries|Where-Object{$_.FullName -eq 'lab-v113-render-observation.json'};if(!$entry){throw 'Missing observation'};$reader=[IO.StreamReader]::new($entry.Open());try{$observation=$reader.ReadToEnd()|ConvertFrom-Json}finally{$reader.Dispose()};if(!$observation.completed -or !$observation.cleanup_passed){throw 'Guest cleanup incomplete'}}finally{$archive.Dispose()}
if(Test-Path $out){throw 'Evidence exists'}
New-Item -ItemType Directory $out|Out-Null
$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-2026-4-v209"
$env:PATH="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs;$env:PATH"
$records=Get-Content "$r\vm\v113-early-records.json" -Raw|ConvertFrom-Json
$results=@()
foreach($record in $records){
 $capture="$r\vm\$($record.name)";if((Get-FileHash $capture).Hash.ToLowerInvariant() -ne $record.sha256){throw 'Capture mismatch'}
 $name=[IO.Path]::GetFileNameWithoutExtension($capture);$info=[Diagnostics.ProcessStartInfo]::new();$info.FileName="$r\builds\present-umd-v222\Release\nr_registered_draw_replay_probe.exe";$info.Arguments='--capture "'+$capture+'" --backend NPU --output "'+$out+'\'+$name+'.pixels" --repeat 1 --batch 4096';$info.UseShellExecute=$false;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 $p=[Diagnostics.Process]::new();$p.StartInfo=$info;[void]$p.Start();$stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync();$timeout=!$p.WaitForExit(120000);if($timeout){$p.Kill();$p.WaitForExit()}
 [IO.File]::WriteAllText("$out\$name.stdout.log",$stdout.GetAwaiter().GetResult());[IO.File]::WriteAllText("$out\$name.stderr.log",$stderr.GetAwaiter().GetResult())
 $results+=@{name=$name;sha256=$record.sha256;backend='NPU';exit_code=$p.ExitCode;timed_out=$timeout};$p.Dispose();Get-Content "$out\$name.stderr.log";Get-Content "$out\$name.stdout.log" -Tail 3
 $results|ConvertTo-Json -Depth 5|Set-Content "$out\report.json"
}
