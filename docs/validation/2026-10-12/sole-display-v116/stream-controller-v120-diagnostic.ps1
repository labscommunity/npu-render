# Finite native packet bridge. No physical driver changes; guest-only pipe/port.
# The owner supplies a loopback SSH tunnel and QEMU host forwarding on port 49406.
param(
 [Parameter(Mandatory=$true)][System.Management.Automation.Runspaces.PSSession]$Session,
 [Parameter(Mandatory=$true)][string]$BuildDirectory,
 [Parameter(Mandatory=$true)][string]$RuntimeDirectory,
 [Parameter(Mandatory=$true)][string]$EvidenceDirectory,
 [string]$GuestActionScript='',
 [string]$ModelCacheDirectory='',
 [switch]$SmokeTest,
 [switch]$DesktopSmokeTest,
 [switch]$TextureSmokeTest,
 [switch]$ScissorSmokeTest,
 [ValidateRange(1,4096)][int]$MaximumRequests=512,
 [ValidateRange(60,1800)][int]$WorkerMaximumSeconds=600
)
$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue'
if($Session.State -ne 'Opened' -or (Test-Path $EvidenceDirectory)){throw 'Opened guest session and new evidence directory required'}
if(!$SmokeTest -and !$GuestActionScript){throw 'An explicit finite guest action or smoke test is required'}
if($ModelCacheDirectory -and (![IO.Path]::IsPathRooted($ModelCacheDirectory) -or !(Test-Path -LiteralPath $ModelCacheDirectory -PathType Container))){throw 'Model cache must be an existing absolute directory'}
$build=Get-Content "$BuildDirectory\registered-build-report.json" -Raw|ConvertFrom-Json
if(!$build.clean_build -or !$build.worker_duration_configurable -or $build.worker_maximum_seconds -ne 1800){throw 'Qualified diagnostic duration build required'}
foreach($item in $build.binary_sha256.psobject.Properties){
 if($item.Name -notmatch '^[a-zA-Z0-9_.-]+$' -or (Get-FileHash (Join-Path "$BuildDirectory\Release" $item.Name)).Hash -ne $item.Value){throw 'Build binary hash mismatch'}
}
$runtimeManifest=@()
foreach($name in 'openvino.dll','openvino_ir_frontend.dll','openvino_intel_npu_plugin.dll'){
 $path=Join-Path $RuntimeDirectory $name
 if(!(Test-Path -LiteralPath $path -PathType Leaf)){throw "Native NPU worker runtime is incomplete: missing $name"}
 $runtimeManifest += [ordered]@{name=$name;sha256=(Get-FileHash -LiteralPath $path).Hash;file_version=(Get-Item -LiteralPath $path).VersionInfo.FileVersion}
}
if($DesktopSmokeTest -and !$SmokeTest){throw 'DesktopSmokeTest requires SmokeTest'}
if($TextureSmokeTest -and (!$SmokeTest -or $DesktopSmokeTest)){throw 'TextureSmokeTest requires SmokeTest and excludes DesktopSmokeTest'}
if($ScissorSmokeTest -and (!$SmokeTest -or $DesktopSmokeTest -or $TextureSmokeTest)){throw 'ScissorSmokeTest requires SmokeTest and excludes other fixtures'}
$job=[Guid]::NewGuid().ToString('N');$guest="C:\npu-lab\indexed-stream-$job";$pipe="\\.\pipe\npu-render-indexed-$job"
$active='C:\npu-lab\indexed-pipe-active.txt';$rule="NpuRenderIndexed-$job";$limit=$MaximumRequests;if($SmokeTest){$limit=3}
New-Item -ItemType Directory $EvidenceDirectory|Out-Null
Copy-Item "$BuildDirectory\registered-build-report.json" $EvidenceDirectory
$token="$EvidenceDirectory\private-token.bin";$worker=$null;$stdout=$null;$stderr=$null;$stdoutFile=$null;$stderrFile=$null;$prepared=$false;$oldPath=$env:PATH
$report=[ordered]@{schema=1;kind='indexed-npu-native-stream';job_id=$job;started=[DateTime]::UtcNow.ToString('o');completed=$false;transport='local named pipe and authenticated SSH-carried stream';smoke_test=[bool]$SmokeTest;desktop_offload_tested=$false;displayed_frames=0}
$report.physical_runtime=$runtimeManifest
$report.maximum_requests=$limit;$report.worker_maximum_seconds=$WorkerMaximumSeconds
$report.model_cache_enabled=[bool]$ModelCacheDirectory
function Save-Report {$report|ConvertTo-Json -Depth 8|Set-Content "$EvidenceDirectory\report.json"}
Save-Report
try {
 # Token is never passed on a command line or written to a report.
 [IO.File]::WriteAllBytes($token,(New-Object byte[] 32))
 & icacls.exe $token /inheritance:r /grant:r '*S-1-5-18:F' '*S-1-5-32-544:F' "$($env:USERNAME):F" *> "$EvidenceDirectory\token-acl.log"
 if($LASTEXITCODE -ne 0){throw 'Host token ACL failed'}
 $random=New-Object byte[] 32;$rng=[Security.Cryptography.RandomNumberGenerator]::Create()
 try {$rng.GetBytes($random);[IO.File]::WriteAllBytes($token,$random)} finally {$rng.Dispose();[Array]::Clear($random,0,$random.Length)}
 Invoke-Command -Session $Session -ArgumentList $guest,$active -ScriptBlock {
  param($directory,$config)
  if($env:COMPUTERNAME -ne 'NR-GFX-LAB' -or [IO.DriveInfo]::new('C:\').VolumeLabel -ne 'NR_WINDOWS' -or (Get-CimInstance Win32_ComputerSystem).Manufacturer -notmatch 'QEMU'){throw 'Owned disposable guest required'}
  if((Test-Path $directory) -or (Test-Path $config) -or (Test-Path 'C:\npu-lab\indexed-exchange-active.txt')){throw 'Relay already configured'}
  New-Item -ItemType Directory $directory|Out-Null
  & icacls.exe $directory /inheritance:r /grant:r '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' *> "$directory\acl.log"
  if($LASTEXITCODE -ne 0){throw 'Guest private directory ACL failed'}
 }
 $prepared=$true
 foreach($name in @('nr_registered_indexed_stream_relay.exe','nr_registered_indexed_exchange_test.exe')){
  Copy-Item "$BuildDirectory\Release\$name" "$guest\$name" -ToSession $Session
 }
 Copy-Item $token "$guest\private-token.bin" -ToSession $Session
 $relayHash=$build.binary_sha256.'nr_registered_indexed_stream_relay.exe';$testHash=$build.binary_sha256.'nr_registered_indexed_exchange_test.exe'
 $report.relay_pid=Invoke-Command -Session $Session -ArgumentList $guest,$pipe,$rule,$limit,$relayHash,$testHash -ScriptBlock {
  param($directory,$name,$firewall,$maximum,$expectedRelay,$expectedTest)
  if((Get-FileHash "$directory\nr_registered_indexed_stream_relay.exe").Hash -ne $expectedRelay -or (Get-FileHash "$directory\nr_registered_indexed_exchange_test.exe").Hash -ne $expectedTest){throw 'Guest binary transfer mismatch'}
  $gateways=@([Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties().GetActiveTcpConnections()|Where-Object {$_.LocalEndPoint.Port -eq 5986 -and $_.State -eq 'Established' -and $_.RemoteEndPoint.Address.AddressFamily -eq 'InterNetwork'}|ForEach-Object {$_.RemoteEndPoint.Address.ToString()}|Select-Object -Unique)
  if($gateways.Count -ne 1){throw 'Single authenticated management peer required for the isolated firewall rule'}
  New-NetFirewallRule -Name $firewall -DisplayName $firewall -Direction Inbound -Action Allow -Protocol TCP -LocalPort 49406 -RemoteAddress $gateways[0] -Program "$directory\nr_registered_indexed_stream_relay.exe" -Profile Any|Out-Null
  $arguments='"'+$name+'" "'+$directory+'\private-token.bin" '+$maximum
  $p=Start-Process "$directory\nr_registered_indexed_stream_relay.exe" -ArgumentList $arguments -RedirectStandardOutput "$directory\relay.stdout.jsonl" -RedirectStandardError "$directory\relay.stderr.log" -PassThru
  $p.Id
 }
 $env:PATH="$RuntimeDirectory;$oldPath"
 $worker=New-Object System.Diagnostics.Process
 $worker.StartInfo.FileName="$BuildDirectory\Release\nr_registered_indexed_stream_worker.exe";$worker.StartInfo.Arguments='"'+$token+'" '+$limit
 $worker.StartInfo.EnvironmentVariables['NR_RENDER_STREAM_SECONDS']=[string]$WorkerMaximumSeconds;
 $worker.StartInfo.UseShellExecute=$false;$worker.StartInfo.CreateNoWindow=$true;$worker.StartInfo.RedirectStandardOutput=$true;$worker.StartInfo.RedirectStandardError=$true
 if($ModelCacheDirectory){$worker.StartInfo.EnvironmentVariables['NR_RENDER_NPU_CACHE']=$ModelCacheDirectory}
 else{$worker.StartInfo.EnvironmentVariables.Remove('NR_RENDER_NPU_CACHE')}
 $stdoutFile=[IO.FileStream]::new("$EvidenceDirectory\worker.stdout.jsonl",[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::ReadWrite,1,$true)
 $stderrFile=[IO.FileStream]::new("$EvidenceDirectory\worker.stderr.log",[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::ReadWrite,1,$true)
 if(!$worker.Start()){throw 'Native NPU worker did not start'}
 $stdout=$worker.StandardOutput.BaseStream.CopyToAsync($stdoutFile,4096);$stderr=$worker.StandardError.BaseStream.CopyToAsync($stderrFile,4096);$report.worker_pid=$worker.Id
 $report.worker_logs_streamed=$true
 Invoke-Command -Session $Session -ArgumentList $guest,$pipe,$active -ScriptBlock {
  param($directory,$name,$config)
  $deadline=[DateTime]::UtcNow.AddSeconds(30)
  while([DateTime]::UtcNow -lt $deadline){
   if((Test-Path "$directory\relay.stdout.jsonl") -and (Get-Content "$directory\relay.stdout.jsonl" -Raw) -match '"relay_ready":true'){break}
   Start-Sleep -Milliseconds 100
  }
  if(!(Test-Path "$directory\relay.stdout.jsonl") -or (Get-Content "$directory\relay.stdout.jsonl" -Raw) -notmatch '"relay_ready":true'){throw 'Guest relay did not become ready'}
  if((Test-Path $config) -or (Test-Path "$config.tmp")){throw 'Relay config ownership changed'}
  [IO.File]::WriteAllText("$config.tmp",$name,[Text.Encoding]::ASCII)
  & icacls.exe "$config.tmp" /inheritance:r /grant:r '*S-1-5-18:F' '*S-1-5-32-544:F' '*S-1-5-11:R' *> "$directory\config-acl.log"
  if($LASTEXITCODE -ne 0){throw 'Relay config ACL failed'}
  [IO.File]::Move("$config.tmp",$config)
 }
 $report.state='relay_ready';Save-Report
 if($SmokeTest){
  $report.guest_test_exit=Invoke-Command -Session $Session -ArgumentList $guest,$pipe,([bool]$DesktopSmokeTest),([bool]$TextureSmokeTest),([bool]$ScissorSmokeTest) -ScriptBlock {
   param($directory,$name,$desktop,$texture,$scissor)
   $mode='--pipe';if($desktop){$mode='--pipe-desktop'};if($texture){$mode='--pipe-texture'};if($scissor){$mode='--pipe-scissor'}
   & "$directory\nr_registered_indexed_exchange_test.exe" $mode $name *> "$directory\smoke.log"
   $LASTEXITCODE
  }
  if($report.guest_test_exit -ne 0){throw 'Guest NPU pipe fixture failed'}
  if(!$worker.WaitForExit(10000) -or $worker.ExitCode -ne 0){throw 'Native worker failed or did not finish bounded smoke test'}
  $report.smoke_passed=$true
 } else {
  Invoke-Command -Session $Session -ArgumentList $GuestActionScript -ScriptBlock {
   param($script)
   if($script -notlike 'C:\npu-lab\*.ps1' -or !(Test-Path -LiteralPath $script)){throw 'Expected finite guest laboratory action'}
   $oldPolicy=Get-ExecutionPolicy -Scope Process
   try {Set-ExecutionPolicy -Scope Process Bypass -Force;& $script}
   finally {Set-ExecutionPolicy -Scope Process $oldPolicy -Force}
  }
  $report.guest_action_finished=$true
 }
 $report.state='action_finished'
} catch {$report.error=$_.Exception.Message;$report.state='failed';Write-Warning $report.error}
finally {
 if($prepared){
  try {
   Invoke-Command -Session $Session -ArgumentList $guest,$pipe,$active,$rule -ScriptBlock {
    param($directory,$name,$config,$firewall)
    if(Test-Path $config){if([IO.File]::ReadAllText($config) -ne $name){throw 'Relay config ownership changed'};Remove-Item $config}
    Get-CimInstance Win32_Process -Filter "Name='nr_registered_indexed_stream_relay.exe'"|Where-Object {$_.ExecutablePath -eq "$directory\nr_registered_indexed_stream_relay.exe"}|ForEach-Object {Stop-Process -Id $_.ProcessId -Force}
    if(Get-NetFirewallRule -Name $firewall -ErrorAction SilentlyContinue){Remove-NetFirewallRule -Name $firewall}
    if(Test-Path "$directory\private-token.bin"){Remove-Item "$directory\private-token.bin"}
   }
   $report.configuration_removed=$true
  } catch {$report.cleanup_error=$_.Exception.Message}
  foreach($name in @('relay.stdout.jsonl','relay.stderr.log','smoke.log')){
   try {Copy-Item "$guest\$name" "$EvidenceDirectory\$name" -FromSession $Session -ErrorAction Stop} catch {if($name -ne 'smoke.log'){$report["copy_error_$name"]=$_.Exception.Message}}
  }
 }
 if($worker -and $stdout){
  try {
   if(!$worker.HasExited -and !$worker.WaitForExit(3000)){
    $worker.Kill();$report.worker_terminated=$true
    $report.worker_exit_wait_completed=$worker.WaitForExit(5000)
   }
   if($worker.HasExited){$report.worker_exit_code=$worker.ExitCode}
   else{$report.worker_termination_pending=$true}
   $outDone=$stdout.Wait(2000);$errDone=$stderr.Wait(2000)
   $report.worker_output_complete=$outDone -and $errDone
   if(!$report.worker_output_complete){$report.worker_output_incomplete=$true}
  } catch {$report.worker_cleanup_error=$_.Exception.Message}
 }
 if($stdoutFile){$stdoutFile.Dispose()};if($stderrFile){$stderrFile.Dispose()}
 if($worker){$worker.Dispose()}
 if(Test-Path $token){Remove-Item $token}
 $env:PATH=$oldPath;$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report
 Get-ChildItem $EvidenceDirectory -File|ForEach-Object {[ordered]@{name=$_.Name;sha256=(Get-FileHash $_.FullName).Hash}}|ConvertTo-Json|Set-Content "$EvidenceDirectory\sha256.json"
}
if($report.error -or $report.cleanup_error -or $report.worker_cleanup_error -or $report.worker_termination_pending -or $report.worker_output_incomplete){throw 'Indexed NPU native stream failed; inspect preserved report'}
