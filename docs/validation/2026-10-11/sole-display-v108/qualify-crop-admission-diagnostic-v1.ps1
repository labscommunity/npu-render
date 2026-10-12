$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$out="$r\vm\crop-admission-diagnostic-v1-results";$build="$r\builds\crop-admission-diagnostic-v1"
$stream=Get-Content "$r\vm\indexed-dwm-stream-v108\report.json" -Raw|ConvertFrom-Json;if(!$stream.completed -or !$stream.configuration_removed){throw 'Desktop stream active'}
if(Test-Path $out){throw 'Evidence exists'};New-Item -ItemType Directory $out|Out-Null
$capture="$r\vm\v108-early-draw-1072-1.bin";if((Get-FileHash $capture).Hash -ne '8f2ab21d009d15597525ced343b6e7af9fd858266855402f5017c50470bc19c6'){throw 'Capture hash mismatch'}
$oldPath=$env:PATH;$oldCache=$env:NR_RENDER_NPU_CACHE;$runs=@()
try{$env:PATH="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs;$oldPath";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-2026-4-v209"
foreach($backend in 'CPU','NPU'){
 $i=[Diagnostics.ProcessStartInfo]::new();$i.FileName="$build\Release\nr_crop_admission_probe.exe";$i.Arguments='--capture "'+$capture+'" --backend '+$backend+' --output "'+$out+'\'+$backend+'.pixels"';$i.UseShellExecute=$false;$i.RedirectStandardOutput=$true;$i.RedirectStandardError=$true
 $p=[Diagnostics.Process]::new();$p.StartInfo=$i;[void]$p.Start();$watch=[Diagnostics.Stopwatch]::StartNew();$o=$p.StandardOutput.ReadToEndAsync();$e=$p.StandardError.ReadToEndAsync();$timedOut=$false
 if(!$p.WaitForExit(240000)){$timedOut=$true;$p.Kill();$p.WaitForExit()}
 [IO.File]::WriteAllText("$out\$backend.stdout.jsonl",$o.GetAwaiter().GetResult());$stderr=$e.GetAwaiter().GetResult();[IO.File]::WriteAllText("$out\$backend.stderr.log",$stderr)
 $runs+=@{backend=$backend;exit_code=$p.ExitCode;timed_out=$timedOut;elapsed_ms=$watch.ElapsedMilliseconds};$runs|ConvertTo-Json|Set-Content "$out\runs.json";$stderr;$p.Dispose()
 if($stderr -match 'ZE_RESULT_ERROR_DEVICE_LOST'){throw 'Device lost; stop replay'}
}
}finally{$env:PATH=$oldPath;$env:NR_RENDER_NPU_CACHE=$oldCache}
