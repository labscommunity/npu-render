$ErrorActionPreference='Stop'
if($env:COMPUTERNAME -ne 'NR-GFX-LAB' -or [IO.DriveInfo]::new('C:\').VolumeLabel -ne 'NR_WINDOWS'){throw 'Owned guest required'}
function Read-LogSnapshot([string]$path){
 $s=[IO.File]::Open($path,'Open','Read','ReadWrite');try{$n=[int]$s.Length;$b=New-Object byte[] $n;$at=0;while($at -lt $n){$k=$s.Read($b,$at,$n-$at);if(!$k){break};$at+=$k};return [Text.Encoding]::UTF8.GetString($b,0,$at)}finally{$s.Dispose()}
}
$report=@()
foreach($packet in @(@('v60-indexed-warm-packet.bin',3147816,'d2cdb07798b599560e35abad5e4f87f4205ee60e72e14d850b9590f801080978'),@('v62-texture-warm-packet.bin',10520728,'d2cdb07798b599560e35abad5e4f87f4205ee60e72e14d850b9590f801080978'),@('v63-scissor-warm-packet.bin',3147832,'d2cdb07798b599560e35abad5e4f87f4205ee60e72e14d850b9590f801080978'),@('v65-linear-warm-packet.bin',6293636,'d2cdb07798b599560e35abad5e4f87f4205ee60e72e14d850b9590f801080978'),@('v73-modulated-point-warm-packet.bin',3393668,'5ec41466efdca5f29f20aacf776a731cfd2a5bcf36ae9c0a3391731b0985b157'),@('v75-dwm-warm-packet.bin',3161656,'ab59a5462f80d0d6438792cae7c575a5f3d8275aab4a624e2bd6b1ec9ab44ff1'),@('v75-shell-warm-packet.bin',8508,'2267520865ca56b7eeac16c71797c1d381eab5a06e2accd74d344f118611c37b'),@('v76-texture-triangles-warm-packet.bin',3158220,'abcbc09b3fc9db7ce799235a44d1db85a3ee35867b53bf994d5bdd7bc75bbc49'),@('v76-resource-warm-packet.bin',21936,'f8fbf1dbb99e369d4d03ace35f81524d602c405c1f84e89090d58e6497326921'),@('v77-dwm-81-warm-packet.bin',3171276,'3e9336d93a14d4d669b39e517e1b93478972cf6307740a24ef69ca8a3180bb08'),@('v77-dwm-24-warm-packet.bin',3156684,'32c58155b8d830e05485aa53d51951aa7b18fff1fd75ffce245e2079f352ad77'),@('v77-dwm-81-warm-packet.bin',3171276,'3e9336d93a14d4d669b39e517e1b93478972cf6307740a24ef69ca8a3180bb08'),@('v77-dwm-24-warm-packet.bin',3156684,'32c58155b8d830e05485aa53d51951aa7b18fff1fd75ffce245e2079f352ad77'),@('v104-slow-1-warm-packet.bin',143040,'aa57e309ccbc0c47cd960d777748171817ff7ff15c595d99e3ef5a421ec183ea'),@('v104-slow-2-warm-packet.bin',110272,'dec1e9f4b5dc131f95cef71a2f303d335b35f62fc62bdab11f71aed9d6f9adfd'),@('v104-slow-3-warm-packet.bin',5665272,'9159a080bbe41638752d8a59d7870a46c9c510012cbc5bc7de758198e15369ab'),@('v104-slow-4-warm-packet.bin',4977032,'673cfde1f8091b7d02cfb8c83f5b4d51b3347b0022a940750c092b77b998787b'))){
$bytes=[IO.File]::ReadAllBytes(('C:\npu-lab\'+$packet[0]))
if($bytes.Length -ne $packet[1]){throw 'Unexpected captured draw packet size'}
$pixelBytes=[int64][BitConverter]::ToUInt32($bytes,28)*[BitConverter]::ToUInt32($bytes,32)*4
$name=[IO.File]::ReadAllText('C:\npu-lab\indexed-pipe-active.txt')
if($name -notmatch '^\\\\\.\\pipe\\npu-render-indexed-[0-9a-f]{32}$'){throw 'Invalid owned pipe config'}
$pipe=[IO.Pipes.NamedPipeClientStream]::new('.', $name.Substring(9), [IO.Pipes.PipeDirection]::InOut, [IO.Pipes.PipeOptions]::Asynchronous)
function Write-Bytes([byte[]]$data){$task=$pipe.WriteAsync($data,0,$data.Length);if(!$task.Wait(90000)){throw 'Warmup write timed out'};[void]$task.GetAwaiter().GetResult()}
function Read-Bytes([int]$length){$data=New-Object byte[] $length;$offset=0;while($offset -lt $length){$task=$pipe.ReadAsync($data,$offset,$length-$offset);if(!$task.Wait(90000)){throw 'Warmup read timed out'};$n=$task.GetAwaiter().GetResult();if($n -le 0){throw 'Warmup reply truncated'};$offset+=$n};return ,$data}
try {
 $watch=[Diagnostics.Stopwatch]::StartNew();$pipe.Connect(10000)
 Write-Bytes ([BitConverter]::GetBytes([uint32]$bytes.Length));Write-Bytes $bytes
 $size=[BitConverter]::ToUInt32((Read-Bytes 4),0)
 if($size -ne (80+48+$pixelBytes)){throw 'Unexpected captured draw response size'}
 $reply=Read-Bytes $size
 foreach($offset in @(0,8,16)){if([BitConverter]::ToUInt64($bytes,$offset) -ne [BitConverter]::ToUInt64($reply,$offset)){throw 'Warmup identity mismatch'}}
 foreach($offset in @(24,28,32,36,40,44,48,52,56,64,76)){if([BitConverter]::ToUInt32($bytes,$offset) -ne [BitConverter]::ToUInt32($reply,$offset)){throw 'Warmup shape or request mismatch'}}
 if([BitConverter]::ToUInt32($reply,68) -ne 2 -or [BitConverter]::ToUInt32($reply,72) -ne 3 -or [BitConverter]::ToUInt32($reply,60) -ne (48+$pixelBytes)){throw 'Warmup response is not successful NPU output'}
 Write-Bytes ([BitConverter]::GetBytes([BitConverter]::ToUInt32($bytes,56)))
 $sha=[Security.Cryptography.SHA256]::Create();try{$hash=[BitConverter]::ToString($sha.ComputeHash($reply,128,$pixelBytes)).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
 if($hash -ne $packet[2]){throw 'Captured draw output differs from verified NPU replay'}
 $report += [ordered]@{kind='captured-dwm-packet-prewarm';packet=$packet[0];execution_device='NPU';verified_pixels=($pixelBytes/4);output_sha256=$hash;elapsed_ms=$watch.ElapsedMilliseconds;displayed_frames=0;passed=$true}
} finally {$pipe.Dispose()}
}
# A successful response alone does not prove the relay accepted its receipt.
# Verify each acknowledged packet before any device installation starts.
$job=$name.Substring($name.Length-32)
$relayDirectory='C:\npu-lab\indexed-stream-'+$job
$receiptDeadline=[Diagnostics.Stopwatch]::StartNew();$confirmed=$false
while($receiptDeadline.ElapsedMilliseconds -lt 10000){
 $receipts=@(((Read-LogSnapshot "$relayDirectory\relay.stdout.jsonl") -split "`n")|Where-Object {$_ -match '"client_pid":42'}|ForEach-Object {$_|ConvertFrom-Json})
 if($receipts.Count -eq 17 -and @($receipts|Where-Object {!$_.worker_success}).Count -eq 0){$confirmed=$true;break}
 if((Get-Item "$relayDirectory\relay.stderr.log").Length -gt 0){throw 'Relay rejected a prewarm receipt'}
 Start-Sleep -Milliseconds 100
}
if(!$confirmed){throw 'Relay did not confirm all seventeen prewarm receipts'}
$report|ConvertTo-Json|Set-Content C:\npu-lab\warm-indexed-v108.json
& C:\npu-lab\observe_registered_desktop.ps1 -RenderName lab-v108-render -RenderVersion 0.0.96.0 -RenderPackage C:\npu-lab\driver-package-v108-compositor -IndirectName lab-v9ap-readback -IndirectVersion 0.0.9.0 -IndirectPackage C:\npu-lab\driver-package-idd-readback-v9 -TopologyName idd-topology-v9ap -TopologyProbe C:\npu-lab\nr_indirect_topology_gui_v30.exe -TopologySha256 76f5fb379b37500ed38180468007cdd2c274356711575e2e5084f5dd1d7c3fee -ObservationSeconds 420
