# Transfer three bounded frames through an already authenticated WinRM session.
# Installs nothing and opens no listener. This is not a performance benchmark.
param(
 [Parameter(Mandatory=$true)][System.Management.Automation.Runspaces.PSSession]$Session,
 [Parameter(Mandatory=$true)][string]$BuildDirectory,
 [Parameter(Mandatory=$true)][string]$RuntimeDirectory,
 [Parameter(Mandatory=$true)][string]$EvidenceDirectory,
 [Parameter(Mandatory=$true)][ValidatePattern('^\d+\.\d+\.\d+\.\d+$')][string]$DriverVersion
)
$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue'
if($Session.State -ne 'Opened'){throw 'Authenticated guest session required'}
if(Test-Path $EvidenceDirectory){throw 'Evidence directory must be new'}
$build=Get-Content "$BuildDirectory\registered-build-report.json" -Raw | ConvertFrom-Json
if(!$build.clean_build){throw 'Clean build required'}
foreach($item in $build.binary_sha256.psobject.Properties){
 if($item.Name -notmatch '^[a-zA-Z0-9_.-]+$' -or (Get-FileHash (Join-Path "$BuildDirectory\Release" $item.Name) -Algorithm SHA256).Hash -ne $item.Value){throw 'Build hash mismatch'}
}
$worker="$BuildDirectory\Release\nr_registered_npu_worker.exe"
$probe="$BuildDirectory\Release\nr_registered_clear_probe.exe"
$runner=Join-Path $PSScriptRoot 'run_registered_npu_guest.ps1'
if(!(Test-Path "$RuntimeDirectory\openvino.dll") -or !(Test-Path $runner)){throw 'Runtime and guest runner required'}
$jobId=[Guid]::NewGuid().ToString('N');$guestDirectory="C:\npu-lab\exchange-$jobId"
$task="NpuRenderLabExchange-$jobId"
New-Item -ItemType Directory $EvidenceDirectory | Out-Null
Copy-Item "$BuildDirectory\registered-build-report.json" $EvidenceDirectory
Copy-Item $runner $EvidenceDirectory
$config=[ordered]@{probe_sha256=(Get-FileHash $probe -Algorithm SHA256).Hash;driver_version=$DriverVersion}
$config | ConvertTo-Json | Set-Content "$EvidenceDirectory\config.json"
$started=[DateTime]::UtcNow.ToString('o');$originalPath=$env:PATH;$frames=@();$success=$false;$failure=$null
try {
 Invoke-Command -Session $Session -ArgumentList $guestDirectory -ScriptBlock {
  param($directory)
  if($env:COMPUTERNAME -ne 'NR-GFX-LAB' -or (Get-Volume -DriveLetter C).FileSystemLabel -ne 'NR_WINDOWS' -or (Get-CimInstance Win32_ComputerSystem).Manufacturer -notmatch 'QEMU'){throw 'Owned disposable guest required'}
  if(Test-Path $directory){throw 'Exchange already exists'}
  New-Item -ItemType Directory $directory | Out-Null
 }
 Copy-Item $probe -Destination "$guestDirectory\probe.exe" -ToSession $Session
 Copy-Item $runner -Destination "$guestDirectory\run.ps1" -ToSession $Session
 Copy-Item "$EvidenceDirectory\config.json" -Destination "$guestDirectory\config.json" -ToSession $Session
 Invoke-Command -Session $Session -ArgumentList $guestDirectory,$jobId,$task -ScriptBlock {
  param($directory,$id,$taskName)
  $action=New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File $directory\run.ps1 -JobId $id"
  $principal=New-ScheduledTaskPrincipal -UserId 'NR-GFX-LAB\npu-lab' -LogonType Interactive -RunLevel Highest
  $settings=New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 8) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
  Register-ScheduledTask -TaskName $taskName -Action $action -Principal $principal -Settings $settings | Out-Null
  Start-ScheduledTask -TaskName $taskName
 }
 $env:PATH="$RuntimeDirectory;$originalPath"
 for($sequence=1;$sequence -le 3;$sequence++){
  $requestName="request-$sequence.bin";$replyName="reply-$sequence.bin"
  $deadline=[DateTime]::UtcNow.AddSeconds(125)
  do {
   $status=Invoke-Command -Session $Session -ArgumentList $guestDirectory,$requestName,$task -ScriptBlock {
    param($directory,$name,$taskName)
    if(Test-Path "$directory\$name"){
     $file=Get-Item "$directory\$name" -Force
     if($file.PSIsContainer -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $file.Length -lt 64 -or $file.Length -gt (64+2*32*1024*1024)){throw 'Unbounded or irregular guest request'}
    }
    [pscustomobject]@{ready=(Test-Path "$directory\$name");finished=(Test-Path "$directory\probe-exit.json");task_state=[string](Get-ScheduledTask -TaskName $taskName).State}
   }
   if($status.ready){break}
   if($status.finished -or [DateTime]::UtcNow -gt $deadline){throw 'Guest produced no next request'}
   Start-Sleep -Milliseconds 250
  } while($true)
  Copy-Item "$guestDirectory\$requestName" -Destination "$EvidenceDirectory\$requestName" -FromSession $Session
  $localHash=(Get-FileHash "$EvidenceDirectory\$requestName" -Algorithm SHA256).Hash
  $remoteHash=Invoke-Command -Session $Session -ArgumentList "$guestDirectory\$requestName" -ScriptBlock {param($path) (Get-FileHash $path -Algorithm SHA256).Hash}
  if($localHash -ne $remoteHash){throw 'Request transfer hash mismatch'}
  $frameStarted=[DateTime]::UtcNow.ToString('o')
  # Generated argument paths are quoted explicitly for Start-Process on Windows PowerShell.
  $arguments='"'+(Join-Path $EvidenceDirectory $requestName)+'" "'+(Join-Path $EvidenceDirectory $replyName)+'"'
  $process=Start-Process -FilePath $worker -ArgumentList $arguments -PassThru -RedirectStandardOutput "$EvidenceDirectory\worker-$sequence.stdout.jsonl" -RedirectStandardError "$EvidenceDirectory\worker-$sequence.stderr.log"
  if(!$process.WaitForExit(90000)){$process.Kill();throw 'Finite NPU worker timed out'}
  $process.Refresh();$code=$process.ExitCode
  if($code -ne 0){throw "NPU worker failed with exit $code"}
  $result=Get-Content "$EvidenceDirectory\worker-$sequence.stdout.jsonl" -Raw | ConvertFrom-Json
  if(!$result.passed -or $result.sequence -ne $sequence -or $result.execution_device -notmatch '^NPU(?:\.|$)'){throw 'NPU worker identity mismatch'}
  $replyHash=(Get-FileHash "$EvidenceDirectory\$replyName" -Algorithm SHA256).Hash
  Copy-Item "$EvidenceDirectory\$replyName" -Destination "$guestDirectory\$replyName.tmp" -ToSession $Session
  Invoke-Command -Session $Session -ArgumentList "$guestDirectory\$replyName",$replyHash -ScriptBlock {
   param($path,$expected)
   if((Get-FileHash "$path.tmp" -Algorithm SHA256).Hash -ne $expected){throw 'Reply transfer hash mismatch'}
   [IO.File]::Move("$path.tmp",$path)
  }
  $frames += [ordered]@{sequence=$sequence;worker_started=$frameStarted;reply_published=[DateTime]::UtcNow.ToString('o');exit_code=$code;execution_device=$result.execution_device;request_sha256=$localHash;reply_sha256=$replyHash}
  Write-Output "Frame $sequence executed on $($result.execution_device); reply verified and delivered"
 }
 $deadline=[DateTime]::UtcNow.AddSeconds(45)
 do {
  $done=Invoke-Command -Session $Session -ArgumentList $guestDirectory -ScriptBlock {param($directory) Test-Path "$directory\probe-exit.json"}
  if($done){break};if([DateTime]::UtcNow -gt $deadline){throw 'Guest completion timed out'}
  Start-Sleep -Milliseconds 250
 } while($true)
 Copy-Item "$guestDirectory\probe-exit.json" -Destination $EvidenceDirectory -FromSession $Session
 $exit=Get-Content "$EvidenceDirectory\probe-exit.json" -Raw | ConvertFrom-Json
 if($exit.exit_code -ne 0){throw 'Registered presentation probe failed'}
 $success=$true
} catch { $failure=$_.Exception.Message; Write-Warning $failure }
finally {
 $env:PATH=$originalPath
 foreach($name in 'probe.stdout.jsonl','probe.stderr.log','probe-exit.json','runner.log'){
  try {Copy-Item "$guestDirectory\$name" -Destination $EvidenceDirectory -FromSession $Session -ErrorAction Stop} catch {Write-Warning "Could not collect $name"}
 }
 # Do not kill the probe during graphics cleanup; its own deadline and task bound remain active.
 [ordered]@{schema=1;started=$started;finished=[DateTime]::UtcNow.ToString('o');job_id=$jobId;passed=$success;failure=$failure;frames=$frames;worker_sha256=(Get-FileHash $worker -Algorithm SHA256).Hash;remote_npu_execution_tested=($frames.Count -gt 0);local_npu_execution_tested=$false;desktop_offload_tested=$false;performance_tested=$false;transport='authenticated WinRM file copies'} | ConvertTo-Json -Depth 8 | Set-Content "$EvidenceDirectory\report.json"
 Get-ChildItem $EvidenceDirectory -File | ForEach-Object {[ordered]@{name=$_.Name;sha256=(Get-FileHash $_.FullName -Algorithm SHA256).Hash}} | ConvertTo-Json | Set-Content "$EvidenceDirectory\sha256.json"
}
if(!$success){throw "Registered NPU exchange failed: $failure"}
