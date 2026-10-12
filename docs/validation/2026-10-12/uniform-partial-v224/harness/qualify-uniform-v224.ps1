$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$src='C:\Users\tatef\AppData\Local\Temp\npu-graphics-probe-0146a2388f25';$build="$r\builds\present-umd-v224";$out="$r\vm\uniform-partial-v224"
if(Test-Path $out){throw 'Evidence exists'}
$manifest=Get-Content "$build\registered-build-report.json" -Raw|ConvertFrom-Json
if(!$manifest.clean_build){throw 'Clean build required'}
foreach($f in $manifest.binary_sha256.psobject.Properties){if((Get-FileHash "$build\Release\$($f.Name)").Hash -ne $f.Value){throw 'Binary hash mismatch'}}
foreach($f in $manifest.source_sha256.psobject.Properties){if((Get-FileHash "$src\$($f.Name)").Hash -ne $f.Value){throw 'Source hash mismatch'}}
$stream=Get-Content "$r\vm\indexed-dwm-stream-v115\report.json" -Raw|ConvertFrom-Json;if(!$stream.completed -or !$stream.configuration_removed){throw 'Live stream still active'}
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
 $env:PATH="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs;$oldPath";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-2026-4-v209"
 $capture="$r\vm\v115-early-draw-8128-1.bin"
 if((Get-FileHash $capture).Hash -ne 'f51eec517722974cfaa66af84b161ddb816eff4dece75fe1221612ce523f8b74'){throw 'Notepad capture mismatch'}
 foreach($backend in 'CPU','NPU'){
  Run ("notepad-"+$backend) 'nr_registered_draw_replay_probe.exe' ('--capture "'+$capture+'" --backend '+$backend+' --output "'+$out+'\notepad-'+$backend+'.pixels" --repeat 2 --batch 4096') 360000
 }
 if((Get-FileHash "$out\notepad-CPU.pixels").Hash -ne (Get-FileHash "$out\notepad-NPU.pixels").Hash){throw 'Notepad NPU pixels differ from CPU'}
 & "$src\venv\Scripts\python.exe" "$r\vm\check-v115-indexed-v224.py" prepare "$out\notepad-indexed" *> "$out\notepad-indexed-prepare.log"
 if($LASTEXITCODE){throw 'Original Notepad packet preparation failed'}
 Run 'notepad-indexed-worker' 'nr_registered_indexed_worker.exe' ('"'+$out+'\notepad-indexed" 1') 360000
 & "$src\venv\Scripts\python.exe" "$r\vm\check-v115-indexed-v224.py" compare "$out\notepad-indexed" *> "$out\notepad-indexed-compare.log"
 if($LASTEXITCODE){throw 'Original Notepad packet output differs from independent replay'}
 Run 'uniform-NPU' 'nr_registered_uniform_draw_test.exe' 'NPU' 360000
 & "$src\venv\Scripts\python.exe" "$r\vm\check-vertex-capacity-packets-v1.py" prepare "$out\historical" *> "$out\historical-preparation.log"
 if($LASTEXITCODE){throw 'Historical preparation failed'}
 Run 'historical-worker' 'nr_registered_indexed_worker.exe' ('"'+$out+'\historical" 71') 360000
 & "$src\venv\Scripts\python.exe" "$r\vm\check-vertex-capacity-packets-v1.py" compare "$out\historical" *> "$out\historical-comparison.log"
 if($LASTEXITCODE){throw 'Historical exact comparison failed'}
 foreach($record in (Get-Content "$r\vm\v114-early-records-snapshot.json" -Raw|ConvertFrom-Json)){
  $previous="$r\vm\$($record.name)";if((Get-FileHash $previous).Hash -ne $record.sha256){throw 'Prior capture mismatch'}
  $name=[IO.Path]::GetFileNameWithoutExtension($previous)
  Run $name 'nr_registered_draw_replay_probe.exe' ('--capture "'+$previous+'" --backend NPU --output "'+$out+'\'+$name+'.pixels" --repeat 2 --batch 4096') 360000
  if((Get-FileHash "$out\$name.pixels").Hash -ne (Get-FileHash "$r\vm\v114-failure-replay\$name.pixels").Hash){throw 'Prior captured pixels changed'}
 }
 Run 'lab-NPU' 'nr_registered_lab_test.exe' 'NPU' 600000
 $legacy="$r\builds\present-umd-v224-legacy";$lm=Get-Content "$legacy\registered-build-report.json" -Raw|ConvertFrom-Json
 if(!$lm.clean_build){throw 'Legacy clean build missing'}
 foreach($f in $lm.binary_sha256.psobject.Properties){if((Get-FileHash "$legacy\Release\$($f.Name)").Hash -ne $f.Value){throw 'Legacy binary mismatch'}}
 foreach($f in $manifest.source_sha256.psobject.Properties){if($lm.source_sha256.($f.Name) -ne $f.Value){throw 'Host and guest sources differ'}}
 $env:PATH="$src\venv\Lib\site-packages\openvino\libs;$oldPath";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-v118"
 Run 'legacy-uniform-NPU' "$legacy\Release\nr_registered_uniform_draw_test.exe" 'NPU' 360000
 Run 'legacy-notepad-NPU' "$legacy\Release\nr_registered_draw_replay_probe.exe" ('--capture "'+$capture+'" --backend NPU --output "'+$out+'\notepad-legacy-NPU.pixels" --repeat 2 --batch 4096') 360000
 if((Get-FileHash "$out\notepad-CPU.pixels").Hash -ne (Get-FileHash "$out\notepad-legacy-NPU.pixels").Hash){throw 'Legacy Notepad pixels differ'}
 $pixels=@(Get-ChildItem $out -Filter '*.pixels'|ForEach-Object{[ordered]@{name=$_.Name;bytes=$_.Length;sha256=(Get-FileHash $_.FullName).Hash}})
 $pixels|ConvertTo-Json|Set-Content "$out\pixel-sha256.json"
 [ordered]@{build='present-umd-v224';all_checks_passed=$true;native_npu_execution=$true;historical_packets=71;failed_desktop_captures=4;notepad_cpu_modern_legacy_exact=$true;uniform_suites_both_npu_runtimes=$true;desktop_offload_tested=$false;tahoma_performance_tested=$false}|ConvertTo-Json|Tee-Object "$out\report.json"
}finally{$env:PATH=$oldPath;$env:NR_RENDER_NPU_CACHE=$oldCache}
