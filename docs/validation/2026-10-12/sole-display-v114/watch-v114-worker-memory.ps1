$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$out="$r\vm\worker-memory-v114.jsonl"
if(Test-Path $out){throw 'Memory evidence exists'}
$watch=[Diagnostics.Stopwatch]::StartNew();$process=$null
while($watch.ElapsedMilliseconds -lt 120000){
 $path="$r\vm\indexed-dwm-stream-v114\report.json"
 if(Test-Path $path){
  try{$report=Get-Content $path -Raw|ConvertFrom-Json}catch{Start-Sleep -Milliseconds 500;continue}
  if($report.worker_pid){
   $p=Get-CimInstance Win32_Process -Filter ("ProcessId="+[int]$report.worker_pid)
   if($p.ExecutablePath -ne "$r\builds\present-umd-v223\Release\nr_registered_indexed_stream_worker.exe"){throw 'Unexpected worker identity'}
   $process=Get-Process -Id $report.worker_pid;break
  }
 }
 Start-Sleep -Milliseconds 500
}
if(!$process){throw 'Worker did not start'}
$writer=[IO.StreamWriter]::new($out,$false,[Text.UTF8Encoding]::new($false));$writer.AutoFlush=$true
try{
 while($watch.ElapsedMilliseconds -lt 720000){
  try{$process.Refresh();if($process.HasExited){break}
   $row=[ordered]@{at=[DateTime]::UtcNow.ToString('o');pid=$process.Id;private_bytes=$process.PrivateMemorySize64;working_bytes=$process.WorkingSet64;peak_working_bytes=$process.PeakWorkingSet64;cpu_ms=$process.TotalProcessorTime.TotalMilliseconds}
   $writer.WriteLine(($row|ConvertTo-Json -Compress))
  }catch [InvalidOperationException]{break}
  Start-Sleep -Milliseconds 500
 }
}finally{$writer.Dispose();$process.Dispose()}
'Bounded worker memory sampling finished'
