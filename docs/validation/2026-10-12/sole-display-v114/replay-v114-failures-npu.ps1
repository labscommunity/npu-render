$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$src='C:\Users\tatef\AppData\Local\Temp\npu-graphics-probe-0146a2388f25';$oldPath=$env:PATH
$stream=Get-Content "$r\vm\indexed-dwm-stream-v114\report.json" -Raw|ConvertFrom-Json
if(!$stream.completed -or !$stream.configuration_removed){throw 'Desktop controller must be stopped'}
foreach($variant in 'modern','legacy'){
 $out="$r\vm\v114-failure-replay-$variant-npu";if(Test-Path $out){throw 'Evidence exists'};New-Item -ItemType Directory $out|Out-Null
 $tag=if($variant -eq 'modern'){'v223'}else{'v223-legacy'}
 $runtime=if($variant -eq 'modern'){"$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs"}else{"$src\venv\Lib\site-packages\openvino\libs"}
 $env:PATH="$runtime;$oldPath";$env:NR_RENDER_NPU_CACHE=if($variant -eq 'modern'){"$r\vm\model-cache-2026-4-v209"}else{"$r\vm\model-cache-v118"}
 $records=Get-Content "$r\vm\v114-early-records-snapshot.json" -Raw|ConvertFrom-Json;$results=@()
 foreach($record in $records){
  $capture="$r\vm\$($record.name)";if((Get-FileHash $capture).Hash -ne $record.sha256){throw 'Capture mismatch'}
  $name=[IO.Path]::GetFileNameWithoutExtension($capture);$info=[Diagnostics.ProcessStartInfo]::new();$info.FileName="$r\builds\present-umd-$tag\Release\nr_registered_draw_replay_probe.exe";$info.Arguments='--capture "'+$capture+'" --backend NPU --output "'+$out+'\'+$name+'.pixels" --repeat 2 --batch 4096';$info.UseShellExecute=$false;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
  $of=[IO.FileStream]::new("$out\$name.stdout.log",[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::ReadWrite,1,$true);$ef=[IO.FileStream]::new("$out\$name.stderr.log",[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::ReadWrite,1,$true)
  $p=[Diagnostics.Process]::new();$p.StartInfo=$info;[void]$p.Start();$ot=$p.StandardOutput.BaseStream.CopyToAsync($of,4096);$et=$p.StandardError.BaseStream.CopyToAsync($ef,4096)
  try{
   $timeout=!$p.WaitForExit(120000);if($timeout){$p.Kill();[void]$p.WaitForExit(5000)}
   $complete=$ot.Wait(2000) -and $et.Wait(2000);$code=if($p.HasExited){$p.ExitCode}else{-999}
   $cpu="$r\vm\v114-failure-replay\$name.pixels";$pixels="$out\$name.pixels";$equal=if(Test-Path $pixels){(Get-FileHash $pixels).Hash -eq (Get-FileHash $cpu).Hash}else{$false}
   $results+=@{name=$name;sha256=$record.sha256;backend='NPU';runtime=$variant;exit_code=$code;timed_out=$timeout;output_complete=$complete;exact_cpu_pixels=$equal};$results|ConvertTo-Json -Depth 5|Set-Content "$out\report.json"
   Get-Content "$out\$name.stderr.log";Get-Content "$out\$name.stdout.log" -Tail 3
   if($timeout -or !$complete -or $code -ne 0 -or !$equal){throw 'Replay failed; stop before another NPU workload'}
  }finally{$of.Dispose();$ef.Dispose();$p.Dispose()}
 }
}
