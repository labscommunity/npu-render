# Real guest pipes and the selected remote provider; installs no display drivers.
param([Parameter(Mandatory=$true)][ValidatePattern('^[a-z0-9-]+$')][string]$Name)
$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue'
if($env:COMPUTERNAME -ne 'NR-GFX-LAB' -or [IO.DriveInfo]::new('C:\').VolumeLabel -ne 'NR_WINDOWS'){throw 'Owned disposable guest required'}
$pipeName=[IO.File]::ReadAllText('C:\npu-lab\indexed-pipe-active.txt')
if($pipeName -notmatch '^\\\\\.\\pipe\\npu-render-indexed-[0-9a-f]{32}$'){throw 'Invalid owned pipe config'}
$job=$pipeName.Substring($pipeName.Length-32);$directory="C:\npu-lab\indexed-stream-$job";$fixture="$directory\departure-request.bin"
& "$directory\nr_registered_indexed_exchange_test.exe" --emit-request $fixture *> "$directory\departure-fixture.log"
if($LASTEXITCODE -ne 0){throw 'Fixture generation failed'}
$original=[IO.File]::ReadAllBytes($fixture);$results=@();$clients=@()
function Connect-Client {
 $p=[IO.Pipes.NamedPipeClientStream]::new('.',$pipeName.Substring(9),[IO.Pipes.PipeDirection]::InOut,[IO.Pipes.PipeOptions]::Asynchronous)
 $p.Connect(20000);return $p
}
function Write-Bytes($pipe,[byte[]]$data){$t=$pipe.WriteAsync($data,0,$data.Length);if(!$t.Wait(20000)){throw 'Client write timeout'};[void]$t.GetAwaiter().GetResult()}
function Read-Bytes($pipe,[int]$count){$b=New-Object byte[] $count;$at=0;while($at -lt $count){$t=$pipe.ReadAsync($b,$at,$count-$at);if(!$t.Wait(90000)){throw 'Client read timeout'};$n=$t.GetAwaiter().GetResult();if(!$n){throw 'Truncated reply'};$at+=$n};return ,$b}
function Packet([uint32]$id){$b=[byte[]]$original.Clone();[BitConverter]::GetBytes($id).CopyTo($b,56);[BitConverter]::GetBytes($id).CopyTo($b,64);return ,$b}
function Send-Packet($p,[byte[]]$b){Write-Bytes $p ([BitConverter]::GetBytes([uint32]$b.Length));Write-Bytes $p $b}
function Check-Reply($p,[byte[]]$request){
 $n=[BitConverter]::ToUInt32((Read-Bytes $p 4),0);if($n -ne (128+64*64*4)){throw 'Unexpected reply length'};$b=Read-Bytes $p $n
 foreach($offset in @(0,8,16)){if([BitConverter]::ToUInt64($b,$offset) -ne [BitConverter]::ToUInt64($request,$offset)){throw 'Reply identity mismatch'}}
 foreach($offset in @(24,28,32,36,40,44,48,52,56,64,76)){if([BitConverter]::ToUInt32($b,$offset) -ne [BitConverter]::ToUInt32($request,$offset)){throw 'Reply shape mismatch'}}
 if([BitConverter]::ToUInt32($b,68) -ne 2 -or [BitConverter]::ToUInt32($b,72) -ne 3){throw 'Successful NPU reply required'}
 for($i=128;$i -lt $n;$i++){if($b[$i] -ne $(if(($i-128)%4 -eq 3){255}else{0})){throw 'NPU pixel mismatch'}}
}
$cases=@('before-header','partial-request','invalid-request','after-reply-header','missing-ack','wrong-ack')
try {
 for($i=0;$i -lt $cases.Count;$i++){
  $case=$cases[$i];$bad=Connect-Client;$clients+=,$bad;$b=Packet (9001+$i)
  if($case -eq 'partial-request'){
   Write-Bytes $bad ([BitConverter]::GetBytes([uint32]$b.Length));Write-Bytes $bad $b[0..95]
  }elseif($case -eq 'invalid-request'){
   [BitConverter]::GetBytes([uint32]99).CopyTo($b,68);Send-Packet $bad $b
  }elseif($case -in @('after-reply-header','missing-ack','wrong-ack')){
   Send-Packet $bad $b
   if($case -eq 'after-reply-header'){if([BitConverter]::ToUInt32((Read-Bytes $bad 4),0) -ne 16512){throw 'Unexpected abandoned reply length'}}else{Check-Reply $bad $b}
   if($case -eq 'wrong-ack'){Write-Bytes $bad ([BitConverter]::GetBytes([uint32]4294967295))}
  }
  # Keep one complete reply unacknowledged while a second client renders.
  # Its provider turn must already have been released.
  if($case -ne 'missing-ack'){$bad.Dispose()}
  $healthy=Connect-Client;$clients+=,$healthy;$request=Packet (9101+$i)
  Send-Packet $healthy $request;Check-Reply $healthy $request
  Write-Bytes $healthy ([BitConverter]::GetBytes([uint32](9101+$i)))
  $healthy.Dispose();$bad.Dispose()
  $results+=[ordered]@{case=$case;healthy_client_pid=9101+$i;execution_device='NPU';verified_components=16384;passed=$true}
 }
 [ordered]@{cases=$results;expected_provider_requests=9;expected_acknowledged_replies=6;expected_client_departures=6;desktop_offload_tested=$false;passed=$true}|ConvertTo-Json -Depth 5|Tee-Object "C:\npu-lab\$Name.json"
}catch{
 [ordered]@{cases=$results;failed_case=$case;error=$_.Exception.Message;desktop_offload_tested=$false;passed=$false}|ConvertTo-Json -Depth 5|Set-Content "C:\npu-lab\$Name.json"
 throw
}finally{foreach($p in $clients){$p.Dispose()}}
