# Finite authenticated relay for the disposable guest's indexed-draw callback.
# Installs no driver. Its owner runs installation/observation/removal separately.
param(
 [Parameter(Mandatory=$true)][System.Management.Automation.Runspaces.PSSession]$Session,
 [Parameter(Mandatory=$true)][string]$BuildDirectory,
 [Parameter(Mandatory=$true)][string]$RuntimeDirectory,
 [Parameter(Mandatory=$true)][string]$EvidenceDirectory,
 [ValidateRange(30,240)][int]$DurationSeconds=180
)
$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue'
if($Session.State -ne 'Opened' -or (Test-Path $EvidenceDirectory)){throw 'Opened guest session and new evidence directory required'}
$build=Get-Content "$BuildDirectory\registered-build-report.json" -Raw|ConvertFrom-Json
if(!$build.clean_build){throw 'Clean build required'}
foreach($item in $build.binary_sha256.psobject.Properties){
 if($item.Name -notmatch '^[a-zA-Z0-9_.-]+$' -or (Get-FileHash (Join-Path "$BuildDirectory\Release" $item.Name)).Hash -ne $item.Value){throw 'Build binary hash mismatch'}
}
if(!(Test-Path "$RuntimeDirectory\openvino.dll")){throw 'Pinned OpenVINO runtime required'}
$job=[Guid]::NewGuid().ToString('N');$guest="C:\npu-lab\indexed-exchange-$job";$active='C:\npu-lab\indexed-exchange-active.txt'
New-Item -ItemType Directory $EvidenceDirectory|Out-Null
$packets=Join-Path $EvidenceDirectory 'packets';New-Item -ItemType Directory $packets|Out-Null
Copy-Item "$BuildDirectory\registered-build-report.json" $EvidenceDirectory
$worker=Join-Path "$BuildDirectory\Release" 'nr_registered_indexed_worker.exe'
$process=$null;$stdout=$null;$stderr=$null;$configured=$false;$prepared=$false;$workerStarted=$false;$oldPath=$env:PATH;$seen=@{};$requests=@()
$report=[ordered]@{schema=1;kind='indexed-npu-authenticated-relay';started=[DateTime]::UtcNow.ToString('o');completed=$false;state='preparing';job_id=$job;transport='authenticated WinRM file copies';worker_sha256=(Get-FileHash $worker).Hash;runtime_sha256=(Get-FileHash "$RuntimeDirectory\openvino.dll").Hash;desktop_offload_tested=$false;displayed_frames=0;performance_tested=$false}
function Save-Report {$report.requests=$requests;$report|ConvertTo-Json -Depth 8|Set-Content "$EvidenceDirectory\report.json"}
Save-Report
try {
 Invoke-Command -Session $Session -ArgumentList $guest,$active -ScriptBlock {
  param($directory,$config)
  if($env:COMPUTERNAME -ne 'NR-GFX-LAB' -or [IO.DriveInfo]::new('C:\').VolumeLabel -ne 'NR_WINDOWS' -or (Get-CimInstance Win32_ComputerSystem).Manufacturer -notmatch 'QEMU'){throw 'Owned disposable guest required'}
  if((Test-Path $directory) -or (Test-Path $config)){throw 'An indexed relay is already configured'}
  New-Item -ItemType Directory $directory|Out-Null
  & icacls.exe $directory /grant '*S-1-5-11:(OI)(CI)M' *> "$directory\acl.log"
  if($LASTEXITCODE -ne 0){throw 'Guest relay ACL failed'}
 }
 $prepared=$true
 $env:PATH="$RuntimeDirectory;$oldPath"
 $process=New-Object System.Diagnostics.Process
 $process.StartInfo.FileName=$worker;$process.StartInfo.Arguments='"'+$packets+'" 128'
 $process.StartInfo.UseShellExecute=$false;$process.StartInfo.CreateNoWindow=$true
 $process.StartInfo.RedirectStandardOutput=$true;$process.StartInfo.RedirectStandardError=$true
 if(!$process.Start()){throw 'Indexed NPU worker did not start'}
 $workerStarted=$true
 $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
 Invoke-Command -Session $Session -ArgumentList $guest,$active -ScriptBlock {
  param($directory,$config)
  if(Test-Path $config){throw 'Guest relay configuration changed'}
  $temporary="$config.tmp";if(Test-Path $temporary){throw 'Stale relay configuration publication'}
  [IO.File]::WriteAllText($temporary,$directory,[Text.Encoding]::ASCII)
  & icacls.exe $temporary /inheritance:r /grant:r '*S-1-5-18:F' '*S-1-5-32-544:F' '*S-1-5-11:R' *> "$directory\config-acl.log"
  if($LASTEXITCODE -ne 0){throw 'Guest relay configuration ACL failed'}
  [IO.File]::Move($temporary,$config)
 }
 $configured=$true;$report.state='relay_active';Save-Report
 $deadline=[DateTime]::UtcNow.AddSeconds($DurationSeconds)
 while([DateTime]::UtcNow -lt $deadline){
  if($process.HasExited){throw "Indexed NPU worker exited with $($process.ExitCode)"}
  $files=@(Invoke-Command -Session $Session -ArgumentList $guest -ScriptBlock {
   param($directory)
   $items=@(Get-ChildItem $directory -Filter 'request-*.bin' -File)
   if($items.Count -gt 128){throw 'Guest relay request limit exceeded'}
   foreach($file in $items){
    if($file.Name -notmatch '^request-[0-9]+-[0-9a-f]{32}-[0-9]+\.bin$' -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $file.Length -lt 80 -or $file.Length -gt 32MB){throw 'Irregular or unbounded indexed request'}
    $file.Name
   }
  })
  foreach($name in $files){
   if($seen.ContainsKey($name)){continue};$seen[$name]=$true
   $reply=$name -replace '^request-','reply-';$started=[DateTime]::UtcNow.ToString('o')
   Copy-Item "$guest\$name" "$packets\$name.incoming" -FromSession $Session
   $requestHash=(Get-FileHash "$packets\$name.incoming").Hash
   $guestHash=Invoke-Command -Session $Session -ArgumentList "$guest\$name" -ScriptBlock {param($path) (Get-FileHash $path).Hash}
   if($requestHash -ne $guestHash){throw 'Indexed request transfer hash mismatch'}
   [IO.File]::Move("$packets\$name.incoming","$packets\$name")
   $replyDeadline=[DateTime]::UtcNow.AddSeconds(85)
   while(!(Test-Path "$packets\$reply")){
    if($process.HasExited -or [DateTime]::UtcNow -gt $replyDeadline){throw 'Indexed NPU worker produced no reply'}
    Start-Sleep -Milliseconds 25
   }
   $file=Get-Item "$packets\$reply" -Force
   if($file.PSIsContainer -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $file.Length -lt 80 -or $file.Length -gt 32MB){throw 'Irregular indexed reply'}
   $replyHash=(Get-FileHash $file.FullName).Hash
   Copy-Item $file.FullName "$guest\$reply.tmp" -ToSession $Session
   Invoke-Command -Session $Session -ArgumentList "$guest\$reply",$replyHash -ScriptBlock {
    param($path,$expected)
    if((Get-FileHash "$path.tmp").Hash -ne $expected){throw 'Indexed reply transfer hash mismatch'}
    [IO.File]::Move("$path.tmp",$path)
   }
   $requests += [ordered]@{request=$name;started=$started;reply_published=[DateTime]::UtcNow.ToString('o');request_sha256=$requestHash;reply_sha256=$replyHash}
   Save-Report;Write-Output "Verified and delivered indexed reply $($requests.Count)"
  }
  Start-Sleep -Milliseconds 50
 }
 $report.state='observation_finished'
} catch {$report.error=$_.Exception.Message;$report.state='failed';Write-Warning $report.error}
finally {
 if($prepared){
  try {Invoke-Command -Session $Session -ArgumentList $guest,$active -ScriptBlock {
   param($directory,$config)
   if(!(Test-Path $config)){return}
   if([IO.File]::ReadAllText($config) -ne $directory){throw 'Relay ownership changed; refusing configuration removal'}
   Remove-Item -LiteralPath $config
  };$report.configuration_removed=$true} catch {$report.configuration_remove_error=$_.Exception.Message}
 }
 if($process -and $workerStarted){
  [IO.File]::WriteAllText("$packets\stop",'stop')
  if(!$process.HasExited -and !$process.WaitForExit(10000)){$process.Kill();$process.WaitForExit();$report.worker_terminated=$true}
  $report.worker_exit_code=$process.ExitCode
  if($stdout){[IO.File]::WriteAllText("$EvidenceDirectory\worker.stdout.jsonl",$stdout.GetAwaiter().GetResult())}
  if($stderr){[IO.File]::WriteAllText("$EvidenceDirectory\worker.stderr.log",$stderr.GetAwaiter().GetResult())}
 }
 if($process){$process.Dispose()}
 $env:PATH=$oldPath;$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report
 Get-ChildItem $EvidenceDirectory -File|ForEach-Object {[ordered]@{name=$_.Name;sha256=(Get-FileHash $_.FullName).Hash}}|ConvertTo-Json|Set-Content "$EvidenceDirectory\sha256.json"
}
if($report.error -or $report.configuration_remove_error){throw 'Indexed NPU relay failed; inspect preserved report'}
