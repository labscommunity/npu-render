param([ValidatePattern('^raster-scratch-v[0-9]+$')][string]$Name)
$ErrorActionPreference='Stop';$r="$env:LOCALAPPDATA\npu-render";$d="$r\vm\$Name";$src='C:\Users\tatef\AppData\Local\Temp\npu-graphics-probe-0146a2388f25';$env:PATH="$src\venv\Lib\site-packages\openvino\libs;$env:PATH";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-v118"
if(Test-Path "$d\build.log"){throw 'Evidence exists'}
& cmake -S $d -B "$d\build" -A x64 "-DOpenVINO_DIR=$src\venv\Lib\site-packages\openvino\cmake" *> "$d\configure.log";if($LASTEXITCODE){throw 'configure failed'}
& cmake --build "$d\build" --config Release -j 4 *> "$d\build.log";if($LASTEXITCODE){Get-Content "$d\build.log" -TotalCount 30;throw 'build failed'}
foreach($backend in 'CPU','NPU'){
 $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName="$d\build\Release\raster_test.exe";$info.Arguments="$backend raster";$info.UseShellExecute=$false;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 $p=[Diagnostics.Process]::new();$p.StartInfo=$info;if(!$p.Start()){throw 'start failed'};$a=$p.StandardOutput.ReadToEndAsync();$b=$p.StandardError.ReadToEndAsync();$w=[Diagnostics.Stopwatch]::StartNew();$timeout=$false
 if(!$p.WaitForExit(240000)){$timeout=$true;$p.Kill();$p.WaitForExit()}
 [IO.File]::WriteAllText("$d\$backend-raster.stdout.jsonl",$a.GetAwaiter().GetResult());[IO.File]::WriteAllText("$d\$backend-raster.stderr.log",$b.GetAwaiter().GetResult())
 [ordered]@{exit_code=$p.ExitCode;timed_out=$timeout;elapsed_ms=$w.ElapsedMilliseconds;passed=(!$timeout -and $p.ExitCode -eq 0)}|ConvertTo-Json|Set-Content "$d\$backend-raster-report.json"
 Get-Content "$d\$backend-raster-report.json";Get-Content "$d\$backend-raster.stdout.jsonl";Get-Content "$d\$backend-raster.stderr.log" -Tail 12;if($p.ExitCode -ne 0 -or $timeout){throw 'Raster failed'};$p.Dispose()
}
