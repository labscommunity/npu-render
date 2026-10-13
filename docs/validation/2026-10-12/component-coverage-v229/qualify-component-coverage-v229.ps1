$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$src="$env:LOCALAPPDATA\Temp\npu-graphics-probe-0146a2388f25";$build="$r\builds\present-umd-v229";$out="$r\vm\component-coverage-v229"
if(Test-Path $out){throw 'Evidence exists'}
$prior=Get-Content "$r\vm\indexed-dwm-stream-v119\report.json" -Raw|ConvertFrom-Json;if(!$prior.completed -or !$prior.configuration_removed){throw 'Desktop cleanup required'}
function Require-Build([string]$b){
 $deadline=[DateTime]::UtcNow.AddMinutes(20)
 while(!(Test-Path "$b\registered-build-report.json")){
  if(Test-Path "$r\vm\build-v229-failed.txt"){throw 'Candidate clean build failed'}
  if([DateTime]::UtcNow -gt $deadline){throw 'Candidate build timed out'}
  Start-Sleep -Seconds 5
 }
 $m=Get-Content "$b\registered-build-report.json" -Raw|ConvertFrom-Json;if(!$m.clean_build){throw 'Clean build required'}
 foreach($f in $m.binary_sha256.psobject.Properties){if((Get-FileHash "$b\Release\$($f.Name)").Hash -ne $f.Value){throw 'Binary mismatch'}}
 foreach($f in $m.source_sha256.psobject.Properties){if((Get-FileHash "$src\$($f.Name)").Hash -ne $f.Value){throw 'Source mismatch'}}
}
$capture="$r\vm\v119-early-draw-7092-1.bin";if((Get-FileHash $capture).Hash -ne 'c2295cb197e6928f8bc97cb8852325c22100fb59c6daffddbbeec5237ee9fd01'){throw 'Captured draw mismatch'}
New-Item -ItemType Directory $out|Out-Null;$oldPath=$env:PATH;$oldCache=$env:NR_RENDER_NPU_CACHE
$results=[Collections.Generic.List[object]]::new()
function Run([string]$name,[string]$binary,[string]$arguments,[int]$limit=240000){
 $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=if([IO.Path]::IsPathRooted($binary)){$binary}else{"$build\Release\$binary"};$info.Arguments=$arguments;$info.UseShellExecute=$false;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 $of=[IO.FileStream]::new("$out\$name.stdout.jsonl",[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::ReadWrite,1,$true);$ef=[IO.FileStream]::new("$out\$name.stderr.log",[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::ReadWrite,1,$true)
 $process=[Diagnostics.Process]::new();$process.StartInfo=$info;$watch=[Diagnostics.Stopwatch]::StartNew()
 try{
  if(!$process.Start()){throw 'Cannot start qualification'}
  $ot=$process.StandardOutput.BaseStream.CopyToAsync($of,4096);$et=$process.StandardError.BaseStream.CopyToAsync($ef,4096)
  $timedOut=!$process.WaitForExit($limit);if($timedOut){$process.Kill();[void]$process.WaitForExit(5000)}
  $complete=$ot.Wait(2000) -and $et.Wait(2000);$code=if($process.HasExited){$process.ExitCode}else{-999}
  $entry=[ordered]@{name=$name;exit_code=$code;timed_out=$timedOut;output_complete=$complete;elapsed_ms=$watch.ElapsedMilliseconds;passed=(!$timedOut -and $code -eq 0 -and $complete)};$results.Add($entry);$results|ConvertTo-Json|Set-Content "$out\runs.json"
  Get-Content "$out\$name.stdout.jsonl" -Tail 3;Get-Content "$out\$name.stderr.log" -Tail 4
  if(!$entry.passed){throw "Qualification failed: $name"}
 }finally{$of.Dispose();$ef.Dispose();$process.Dispose()}
}
try{
 foreach($variant in 'modern','legacy'){
  $build=if($variant -eq 'modern'){"$r\builds\present-umd-v229"}else{"$r\builds\present-umd-v229-legacy"}
  Require-Build $build
  $runtime=if($variant -eq 'modern'){"$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs"}else{"$src\venv\Lib\site-packages\openvino\libs"}
  $env:PATH="$runtime;$oldPath";$env:NR_RENDER_NPU_CACHE=if($variant -eq 'modern'){"$r\vm\model-cache-2026-4-v209"}else{"$r\vm\model-cache-v118"}
  foreach($backend in 'CPU','NPU'){
   Run "$variant-captured-$backend" 'nr_registered_draw_replay_probe.exe' ('--capture "'+$capture+'" --backend '+$backend+' --output "'+$out+'\'+$variant+'-'+$backend+'.pixels" --repeat 2 --batch 4096') 360000
  }
  if((Get-FileHash "$out\$variant-CPU.pixels").Hash -ne (Get-FileHash "$out\$variant-NPU.pixels").Hash){throw 'Captured draw pixels differ'}
  Run "$variant-output-merger-NPU" 'nr_registered_output_merger_test.exe' 'NPU' 360000
 }
 $pixelHashes=@('modern-CPU','modern-NPU','legacy-CPU','legacy-NPU')|ForEach-Object{(Get-FileHash "$out\$_.pixels").Hash};if(@($pixelHashes|Select-Object -Unique).Count -ne 1){throw 'Cross-runtime captured draw pixels differ'}
 $build="$r\builds\present-umd-v229";$env:PATH="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs;$oldPath";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-2026-4-v209"
 & "$src\venv\Scripts\python.exe" "$r\vm\check-vertex-capacity-packets-v1.py" prepare "$out\historical" *> "$out\historical-preparation.log"
 if($LASTEXITCODE){throw 'Historical preparation failed'}
 Run 'historical-worker' 'nr_registered_indexed_worker.exe' ('"'+$out+'\historical" 71') 360000
 & "$src\venv\Scripts\python.exe" "$r\vm\check-vertex-capacity-packets-v1.py" compare "$out\historical" *> "$out\historical-comparison.log"
 if($LASTEXITCODE){throw 'Historical exact comparison failed'}
 & "$src\venv\Scripts\python.exe" "$r\vm\check-component-packets-v229.py" prepare *> "$out\packets-preparation.log"
 if($LASTEXITCODE){throw 'Component packet preparation failed'}
 Run 'component-worker' 'nr_registered_indexed_worker.exe' ('"'+$out+'\packets" 3') 360000
 & "$src\venv\Scripts\python.exe" "$r\vm\check-component-packets-v229.py" compare *> "$out\packets-comparison.log"
 if($LASTEXITCODE){throw 'Component packet exact comparison failed'}
 Run 'lab-NPU' 'nr_registered_lab_test.exe' 'NPU' 600000
 [ordered]@{build='present-umd-v229';all_checks_passed=$true;captured_draw_sha256='c2295cb197e6928f8bc97cb8852325c22100fb59c6daffddbbeec5237ee9fd01';cpu_modern_legacy_pixels_exact=$true;captured_draw_repeats_per_backend=2;captured_pixels_per_replay=306936;historical_packets_exact=71;native_component_blend_draws=4;abi15_exact_npu_packets=3;desktop_offload_tested=$false;tahoma_performance_tested=$false}|ConvertTo-Json|Tee-Object "$out\report.json"
}finally{$env:PATH=$oldPath;$env:NR_RENDER_NPU_CACHE=$oldCache}
