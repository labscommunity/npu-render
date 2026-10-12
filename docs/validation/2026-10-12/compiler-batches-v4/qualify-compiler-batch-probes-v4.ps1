$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$out="$r\vm\compiler-batch-comparison-v4"
if(Test-Path $out){throw 'Comparison evidence exists'}
$stream=Get-Content "$r\vm\indexed-dwm-stream-v109\report.json" -Raw|ConvertFrom-Json
if(!$stream.completed -or !$stream.configuration_removed){throw 'Live NPU stream active'}
New-Item -ItemType Directory $out|Out-Null;$runs=[Collections.Generic.List[object]]::new();$oldPath=$env:PATH;$oldCache=$env:NR_RENDER_NPU_CACHE
try{
 foreach($variant in '2026.4'){
  $build="$r\builds\compiler-batch-probe-v4";$m=Get-Content "$build\probe-build-report.json" -Raw|ConvertFrom-Json
  if((Get-FileHash "$build\Release\nr_compositor_batch_probe.exe").Hash -ne $m.binary_sha256){throw 'Probe binary mismatch'}
  foreach($dll in $m.runtime){if((Get-FileHash "$($m.runtime_directory)\$($dll.name)").Hash -ne $dll.sha256){throw 'Runtime mismatch'}}
  $env:PATH="$($m.runtime_directory);$oldPath";$env:NR_RENDER_NPU_CACHE="$out\cache-$variant";New-Item -ItemType Directory $env:NR_RENDER_NPU_CACHE|Out-Null
  foreach($backend in 'CPU','NPU'){
   foreach($section in 'desktop','fade','three','three-base'){
    foreach($batch in 8192,16384){
     $name="$variant-$backend-$section-$batch";$info=[Diagnostics.ProcessStartInfo]::new();$info.FileName="$build\Release\nr_compositor_batch_probe.exe";$info.Arguments="$backend $section $batch";$info.UseShellExecute=$false;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
     $process=[Diagnostics.Process]::new();$process.StartInfo=$info;if(!$process.Start()){throw 'Probe launch failed'}
     $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync();$watch=[Diagnostics.Stopwatch]::StartNew();$timeout=$false
     if(!$process.WaitForExit(360000)){$timeout=$true;$process.Kill();$process.WaitForExit()}
     $o=$stdout.GetAwaiter().GetResult();$e=$stderr.GetAwaiter().GetResult();[IO.File]::WriteAllText("$out\$name.stdout.jsonl",$o);[IO.File]::WriteAllText("$out\$name.stderr.log",$e)
     $row=[ordered]@{name=$name;variant=$variant;backend=$backend;section=$section;batch=$batch;exit_code=$process.ExitCode;timed_out=$timeout;elapsed_ms=$watch.ElapsedMilliseconds;passed=(!$timeout -and $process.ExitCode -eq 0)};$runs.Add($row);$runs|ConvertTo-Json|Set-Content "$out\runs.json";$row|ConvertTo-Json -Compress
     Get-Content "$out\$name.stdout.jsonl" -Tail 1;Get-Content "$out\$name.stderr.log" -Tail 2;$process.Dispose()
     if($backend -eq 'CPU' -and !$row.passed){throw 'Scalar CPU baseline failed'}
     if($e -match 'ZE_RESULT_ERROR_DEVICE_LOST|device lost|Device lost'){throw 'NPU device lost; stop compiler experiment'}
    }
   }
  }
 }
 [ordered]@{completed=$true;native_npu_tests_performed=$true;shipping_batch_policy_changed=$false;desktop_offload_tested=$false;tahoma_performance_tested=$false}|ConvertTo-Json|Set-Content "$out\report.json"
}finally{$env:PATH=$oldPath;$env:NR_RENDER_NPU_CACHE=$oldCache}
