$ErrorActionPreference='Stop'
if($env:COMPUTERNAME -ne 'NR-GFX-LAB' -or [IO.DriveInfo]::new('C:\').VolumeLabel -ne 'NR_WINDOWS'){throw 'Owned guest required'}
$name=[IO.File]::ReadAllText('C:\npu-lab\indexed-pipe-active.txt')
if($name -notmatch '^\\\\\.\\pipe\\npu-render-indexed-[0-9a-f]{32}$'){throw 'Invalid pipe name'}
$clients=@();$report=@();$watch=[Diagnostics.Stopwatch]::StartNew()
function Read-Bytes($pipe,[int]$length){$data=New-Object byte[] $length;$offset=0;while($offset -lt $length){$task=$pipe.ReadAsync($data,$offset,$length-$offset);if(!$task.Wait(90000)){throw 'Read timeout'};$n=$task.GetAwaiter().GetResult();if($n -le 0){throw 'Truncated reply'};$offset+=$n};return ,$data}
function Write-Bytes($pipe,[byte[]]$data){$task=$pipe.WriteAsync($data,0,$data.Length);if(!$task.Wait(20000)){throw 'Write timeout'};[void]$task.GetAwaiter().GetResult()}
try {
 # Connect all four before submitting any work. A single-instance relay cannot
 # satisfy this barrier. Only bounded captured data is sent to the real NPU.
 for($i=0;$i -lt 4;$i++){
  $p=[IO.Pipes.NamedPipeClientStream]::new('.',$name.Substring(9),[IO.Pipes.PipeDirection]::InOut,[IO.Pipes.PipeOptions]::Asynchronous)
  $clients+=,$p;$p.Connect(20000)
 }
 for($i=0;$i -lt 4;$i++){
  $bytes=[IO.File]::ReadAllBytes('C:\npu-lab\v75-shell-warm-packet.bin')
  if($bytes.Length -ne 8508){throw 'Fixture size mismatch'}
  [BitConverter]::GetBytes([uint32]($i+1)).CopyTo($bytes,56);[BitConverter]::GetBytes([uint32](7001+$i)).CopyTo($bytes,64)
  Write-Bytes $clients[$i] ([BitConverter]::GetBytes([uint32]$bytes.Length));Write-Bytes $clients[$i] $bytes
 }
 for($i=0;$i -lt 4;$i++){
  $size=[BitConverter]::ToUInt32((Read-Bytes $clients[$i] 4),0)
  if($size -ne 1152){throw "Unexpected reply size $size"};$reply=Read-Bytes $clients[$i] $size
  if([BitConverter]::ToUInt32($reply,56) -ne ($i+1) -or [BitConverter]::ToUInt32($reply,64) -ne (7001+$i) -or [BitConverter]::ToUInt32($reply,68) -ne 2 -or [BitConverter]::ToUInt32($reply,72) -ne 3){throw 'NPU response identity mismatch'}
  $sha=[Security.Cryptography.SHA256]::Create();try{$hash=[BitConverter]::ToString($sha.ComputeHash($reply,128,$size-128)).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
  if($hash -ne '2267520865ca56b7eeac16c71797c1d381eab5a06e2accd74d344f118611c37b'){throw 'NPU pixels differ'}
  Write-Bytes $clients[$i] ([BitConverter]::GetBytes([uint32]($i+1)))
  $report+=[ordered]@{client_pid=7001+$i;sequence=$i+1;execution_device='NPU';pixel_sha256=$hash;passed=$true}
 }
 [ordered]@{four_clients_connected_before_first_request=$true;packets=$report;elapsed_ms=$watch.ElapsedMilliseconds;desktop_offload_tested=$false}|ConvertTo-Json -Depth 5|Set-Content C:\npu-lab\fair-relay-v113.json
} finally {foreach($p in $clients){$p.Dispose()}}
