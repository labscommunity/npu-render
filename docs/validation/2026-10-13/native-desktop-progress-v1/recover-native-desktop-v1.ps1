param([Parameter(Mandatory=$true)][string]$Root)
$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$state=Get-Content "$Root\ownership.json" -Raw|ConvertFrom-Json
$expected=@{'nr_native_probe.inf'=@('ROOT\NPURENDER_NATIVE_PROBE','0.0.1303.0');'nr_native_indirect_probe.inf'=@('ROOT\NPURENDER_NATIVE_INDIRECT_V1','0.0.1301.0')}
if($state.schema -ne 3 -or $state.packages.Count -ne 2 -or $state.thumbprint -ne '3112355AF6B6C6265E9B849662D3E02F6797343A'){throw 'Recovery ownership mismatch'}
$names=@();foreach($p in $state.packages){if(!$expected.ContainsKey($p.inf) -or $p.hardware_id -ne $expected[$p.inf][0] -or $p.version -ne $expected[$p.inf][1] -or $p.inf_sha256 -notmatch '^[A-Fa-f0-9]{64}$'){throw 'Package ownership mismatch'};$names+=$p.inf};if(@($names|Select-Object -Unique).Count -ne 2){throw 'Duplicate package identity'}
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;devices_removed=@();packages_removed=@();certificate_stores_restored=@();errors=@()};$mutex=[Threading.Mutex]::new($false,'Global\T8.NpuRender.NativeDesktopRecovery.v1');$held=$false
function Record-Error([string]$message){$report.errors+=$message}
try{
 try{$held=$mutex.WaitOne(5000)}catch [Threading.AbandonedMutexException]{$held=$true};if(!$held){throw 'Another recovery owns cleanup'}
 if((Get-FileHash $state.devcon).Hash -ne $state.devcon_sha256){throw 'Recovery utility changed'}
 # Remove the indirect display before its paired renderer. Each owned action is
 # independent so one failure cannot skip the remaining cleanup attempts.
 foreach($name in @('nr_native_indirect_probe.inf','nr_native_probe.inf')){
  $p=@($state.packages|Where-Object inf -eq $name)[0]
  try{
   foreach($device in @(Get-PnpDevice -PresentOnly -Class Display)){
    $ids=@(($device|Get-PnpDeviceProperty -KeyName DEVPKEY_Device_HardwareIds -ErrorAction SilentlyContinue).Data)
    if($ids -notcontains $p.hardware_id){continue};if($device.InstanceId -notmatch '^ROOT\\DISPLAY\\[0-9A-F]+$'){throw 'Unexpected owned device instance'}
    & $state.devcon remove "@$($device.InstanceId)" *>> "$Root\recovery-devcon.log";if($LASTEXITCODE){throw 'Owned device removal failed or requires restart'};$report.devices_removed+=$device.InstanceId
   }
  }catch{Record-Error ($name+': '+$_.Exception.Message)}
  try{
   foreach($driver in @(Get-WindowsDriver -Online|Where-Object {[IO.Path]::GetFileName($_.OriginalFileName) -eq $name -and $_.Version -eq $p.version})){
    if($driver.Driver -notmatch '^oem\d+\.inf$' -or (Get-FileHash "$env:windir\INF\$($driver.Driver)").Hash -ne $p.inf_sha256){throw 'Owned INF identity mismatch'}
    & pnputil.exe /delete-driver $driver.Driver *>> "$Root\recovery-pnputil.log";if($LASTEXITCODE){throw 'Owned package removal failed'};$report.packages_removed+=$driver.Driver
   }
  }catch{Record-Error ($name+': '+$_.Exception.Message)}
 }
 # Stop only the exact controller and worker created by this transaction.
 foreach($owned in @($state.owned_processes)){
  try{
   if($owned.role -notin @('controller','worker') -or $owned.pid -eq $PID -or $owned.sha256 -notmatch '^[a-fA-F0-9]{64}$'){throw 'Owned process contract mismatch'}
   $expectedName=if($owned.role -eq 'controller'){'python.exe'}else{'nr_registered_local_indexed_worker.exe'}
   if([IO.Path]::GetFileName($owned.image) -ne $expectedName){throw 'Owned process image mismatch'}
   $process=Get-Process -Id $owned.pid -ErrorAction SilentlyContinue
   if($process){
    if([uint64]$process.StartTime.ToUniversalTime().ToFileTimeUtc() -eq [uint64]$owned.created){
     if($process.Path -ne $owned.image -or (Get-FileHash $process.Path).Hash -ne $owned.sha256){throw 'Owned process image changed'}
     Stop-Process -InputObject $process -Force;if(!$process.WaitForExit(5000)){throw 'Owned process did not stop'}
    }
    $process.Dispose()
   }
  }catch{Record-Error ('owned-process: '+$_.Exception.Message)}
 }
 # Directories were absent before this transaction, protected, and declared in
 # ownership before creation. Archive them only after removing devices/processes.
 foreach($name in @($state.owned_trace_directories)){
  try{
   if($name -notin @('T8-NpuRender-NativeTraceV1','T8-NpuRender-NativeIddV1')){throw 'Unexpected trace ownership'}
   $source=Join-Path $env:ProgramData $name
   if(Test-Path $source){
    $destination=Join-Path $Root $name
    if(Test-Path $destination){throw 'Existing immutable trace archive'}
    Move-Item $source $destination
   }
  }catch{Record-Error ('trace: '+$_.Exception.Message)}
 }
 foreach($name in @($state.added_stores)){
  try{
   if($name -notin @('Root','TrustedPublisher')){throw 'Unexpected owned certificate store'}
   $store=[Security.Cryptography.X509Certificates.X509Store]::new($name,'LocalMachine');$store.Open('ReadWrite');try{foreach($cert in @($store.Certificates|Where-Object Thumbprint -eq $state.thumbprint)){$store.Remove($cert)}}finally{$store.Close()}
   if(Test-Path "Cert:\LocalMachine\$name\$($state.thumbprint)"){throw 'Owned certificate membership remains'};$report.certificate_stores_restored+=$name
  }catch{Record-Error ($name+': '+$_.Exception.Message)}
 }
 foreach($p in $state.packages){
  try{
   $devices=@(Get-PnpDevice -PresentOnly -Class Display|Where-Object {@(($_|Get-PnpDeviceProperty -KeyName DEVPKEY_Device_HardwareIds -ErrorAction SilentlyContinue).Data) -contains $p.hardware_id})
   $drivers=@(Get-WindowsDriver -Online|Where-Object {[IO.Path]::GetFileName($_.OriginalFileName) -eq $p.inf -and $_.Version -eq $p.version})
   if($devices.Count -or $drivers.Count){throw 'Owned device or package remains'}
  }catch{Record-Error ($p.inf+': '+$_.Exception.Message)}
 }
 foreach($prior in $state.physical_display_before){try{$now=Get-PnpDevice -InstanceId $prior.instance -PresentOnly;if($now.Status -ne $prior.status){throw 'Original display health changed'}}catch{Record-Error $_.Exception.Message}}
 $report.passed=$report.errors.Count -eq 0
 if($report.passed){Unregister-ScheduledTask -TaskName 'T8-NpuRender-NativeDesktopRecovery-v1' -Confirm:$false -ErrorAction SilentlyContinue}
}catch{Record-Error $_.Exception.Message}
finally{$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');$report|ConvertTo-Json -Depth 6|Set-Content "$Root\recovery-report.json";if($held){$mutex.ReleaseMutex()};$mutex.Dispose()}
$report|ConvertTo-Json -Depth 6;if(!$report.passed){exit 1}
