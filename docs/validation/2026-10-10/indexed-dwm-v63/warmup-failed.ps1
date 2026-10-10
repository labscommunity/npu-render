$ErrorActionPreference='Stop'
if($env:COMPUTERNAME -ne 'NR-GFX-LAB' -or [IO.DriveInfo]::new('C:\').VolumeLabel -ne 'NR_WINDOWS'){throw 'Owned guest required'}
$report=@()
foreach($packet in @(@('v60-indexed-warm-packet.bin',3147816),@('v62-texture-warm-packet.bin',10520728),@('v63-scissor-warm-packet.bin',3147832))){
$bytes=[IO.File]::ReadAllBytes(('C:\npu-lab\'+$packet[0]))
if($bytes.Length -ne $packet[1]){throw 'Unexpected captured draw packet size'}
$name=[IO.File]::ReadAllText('C:\npu-lab\indexed-pipe-active.txt')
if($name -notmatch '^\\\\\.\\pipe\\npu-render-indexed-[0-9a-f]{32}$'){throw 'Invalid owned pipe config'}
$pipe=[IO.Pipes.NamedPipeClientStream]::new('.', $name.Substring(9), [IO.Pipes.PipeDirection]::InOut, [IO.Pipes.PipeOptions]::Asynchronous)
function Write-Bytes([byte[]]$data){$task=$pipe.WriteAsync($data,0,$data.Length);if(!$task.Wait(90000)){throw 'Warmup write timed out'};[void]$task.GetAwaiter().GetResult()}
function Read-Bytes([int]$length){$data=New-Object byte[] $length;$offset=0;while($offset -lt $length){$task=$pipe.ReadAsync($data,$offset,$length-$offset);if(!$task.Wait(90000)){throw 'Warmup read timed out'};$n=$task.GetAwaiter().GetResult();if($n -le 0){throw 'Warmup reply truncated'};$offset+=$n};return ,$data}
try {
 $watch=[Diagnostics.Stopwatch]::StartNew();$pipe.Connect(10000)
 Write-Bytes ([BitConverter]::GetBytes([uint32]$bytes.Length));Write-Bytes $bytes
 $size=[BitConverter]::ToUInt32((Read-Bytes 4),0)
 if($size -ne (80+48+3145728)){throw 'Unexpected captured draw response size'}
 $reply=Read-Bytes $size
 foreach($offset in @(0,8,16)){if([BitConverter]::ToUInt64($bytes,$offset) -ne [BitConverter]::ToUInt64($reply,$offset)){throw 'Warmup identity mismatch'}}
 foreach($offset in @(24,28,32,36,40,44,48,52,56,64,76)){if([BitConverter]::ToUInt32($bytes,$offset) -ne [BitConverter]::ToUInt32($reply,$offset)){throw 'Warmup shape or request mismatch'}}
 if([BitConverter]::ToUInt32($reply,68) -ne 2 -or [BitConverter]::ToUInt32($reply,72) -ne 3 -or [BitConverter]::ToUInt32($reply,60) -ne (48+3145728)){throw 'Warmup response is not successful NPU output'}
 Write-Bytes ([BitConverter]::GetBytes([uint32]1))
 $sha=[Security.Cryptography.SHA256]::Create();try{$hash=[BitConverter]::ToString($sha.ComputeHash($reply,128,3145728)).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
 if($hash -ne 'd2cdb07798b599560e35abad5e4f87f4205ee60e72e14d850b9590f801080978'){throw 'Captured draw output differs from verified NPU replay'}
 $report += [ordered]@{kind='captured-dwm-packet-prewarm';packet=$packet[0];execution_device='NPU';verified_pixels=786432;output_sha256=$hash;elapsed_ms=$watch.ElapsedMilliseconds;displayed_frames=0;passed=$true}
} finally {$pipe.Dispose()}
}
$report|ConvertTo-Json|Set-Content C:\npu-lab\warm-indexed-v63.json
& C:\npu-lab\trace-kernel-admission-v63.ps1
