$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$src='C:\Users\tatef\AppData\Local\Temp\npu-graphics-probe-0146a2388f25';$build="$r\builds\attribute-diagnostic-v2";$out="$r\vm\attribute-diagnostic-v2-results"
if(Test-Path $out){throw 'Evidence exists'}
$stream=Get-Content "$r\vm\indexed-dwm-stream-v107\report.json" -Raw|ConvertFrom-Json;if(!$stream.completed -or !$stream.configuration_removed){throw 'Live stream still active'}
New-Item -ItemType Directory $out|Out-Null;$oldPath=$env:PATH;$oldCache=$env:NR_RENDER_NPU_CACHE
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
 if(!$entry.passed){throw "Attribute diagnostic failed: $name"}

}
try{
 $env:PATH="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs;$env:PATH";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-2026-4-v209"
 foreach($backend in 'CPU','NPU'){Run ('mixed-'+$backend) 'attribute_test.exe' $backend 600000}
 foreach($item in @(@('1041','v105-early-draw-5112-1.bin','2bd0001ec19439a2ba859d28429ca68fdc020e0751ad91a1e7953881911f3a07'),@('1061','v102-early-draw-5112-1.bin','8e634f6ea14a33f0204d2aff4c1f1b1033db814c1959d04a1c5717d4bd63e44c'))){
  $name=$item[0];$capture="$r\vm\$($item[1])"
  if((Get-FileHash $capture).Hash -ne $item[2]){throw 'Captured packet changed'}
  foreach($variant in 'baseline','batched'){
   $folder="$out\$name-$variant";New-Item -ItemType Directory $folder|Out-Null
   $binary=if($variant -eq 'baseline'){"$r\builds\present-umd-v209\Release\nr_registered_draw_replay_probe.exe"}else{'attribute_replay.exe'}
   foreach($backend in 'CPU','NPU'){Run ($name+'-'+$variant+'-'+$backend) $binary ('--capture "'+$capture+'" --backend '+$backend+' --output "'+$folder+'\'+$backend+'.pixels"') 300000}
   & "$src\venv\Scripts\python.exe" "$r\vm\compare-three-texture-v199.py" $folder
   if($LASTEXITCODE){throw 'Full image comparison failed'}
  }
 }
 [ordered]@{component_diagnostic_only=$true;all_processes_passed=$true;desktop_offload_tested=$false}|ConvertTo-Json|Tee-Object "$out\report.json"
}finally{$env:PATH=$oldPath;$env:NR_RENDER_NPU_CACHE=$oldCache}
