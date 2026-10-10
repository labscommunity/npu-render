param(
 [Parameter(Mandatory=$true)][string]$Probe,
 [Parameter(Mandatory=$true)][ValidatePattern('^[0-9A-Fa-f]{64}$')][string]$Sha256,
 [Parameter(Mandatory=$true)][ValidatePattern('^idd-topology-[a-z0-9-]+$')][string]$Name
)
$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue'
if($env:COMPUTERNAME -ne 'NR-GFX-LAB' -or (Get-Volume -DriveLetter C).FileSystemLabel -ne 'NR_WINDOWS'){throw 'Owned disposable guest required'}
if(!(Test-Path $Probe -PathType Leaf) -or !$Probe.StartsWith('C:\npu-lab\',[StringComparison]::OrdinalIgnoreCase) -or (Get-FileHash $Probe -Algorithm SHA256).Hash -ne $Sha256){throw 'Unexpected topology probe'}
$prefix="C:\npu-lab\$Name"
if(@(Get-ChildItem "$prefix*").Count){throw 'Evidence path already exists'}
$started=[DateTime]::UtcNow.ToString('o');$code=1
try {
 $devices=@(Get-PnpDevice -Class Display -PresentOnly)
 foreach($id in 'ROOT\NPURENDER_KMD_LAB','ROOT\NPURENDER_INDIRECT_LAB'){
  $found=@($devices | Where-Object {($_ | Get-PnpDeviceProperty -KeyName DEVPKEY_Device_HardwareIds).Data -contains $id})
  if($found.Count -ne 1 -or $found[0].Status -ne 'OK'){throw 'Both laboratory adapters must be healthy'}
 }
 $devices | Select-Object Status,FriendlyName,InstanceId | ConvertTo-Json | Set-Content "$prefix-display-before.json"
 $process=New-Object System.Diagnostics.Process
 $process.StartInfo.FileName=$Probe;$process.StartInfo.UseShellExecute=$false
 $process.StartInfo.RedirectStandardOutput=$true;$process.StartInfo.RedirectStandardError=$true
 if(!$process.Start()){throw 'Probe did not start'}
 $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
 $process.WaitForExit();$code=$process.ExitCode
 $stdout.Result | Set-Content "$prefix.jsonl";$stderr.Result | Set-Content "$prefix.stderr.log"
 $process.Dispose()
} catch {$_ | Out-String | Set-Content "$prefix.failure.log"}
[ordered]@{started=$started;finished=[DateTime]::UtcNow.ToString('o');probe_sha256=$Sha256;exit_code=$code;desktop_offload_tested=$false} | ConvertTo-Json | Set-Content "$prefix-exit.json"
exit $code
