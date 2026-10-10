# Finite interactive probe for the owned disposable graphics guest.
param([Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-f]{32}$')][string]$JobId)
$ErrorActionPreference='Stop'; $ProgressPreference='SilentlyContinue'
if($env:COMPUTERNAME -ne 'NR-GFX-LAB' -or (Get-Volume -DriveLetter C).FileSystemLabel -ne 'NR_WINDOWS'){throw 'Owned graphics guest required'}
$directory="C:\npu-lab\exchange-$JobId"
$started=[DateTime]::UtcNow.ToString('o');$code=1
Start-Transcript "$directory\runner.log"
try {
$config=Get-Content "$directory\config.json" -Raw | ConvertFrom-Json
$probe="$directory\probe.exe"
if((Get-FileHash $probe -Algorithm SHA256).Hash -ne $config.probe_sha256){throw 'Probe hash mismatch'}
$device=Get-PnpDevice -Class Display -PresentOnly | Where-Object {
 ($_ | Get-PnpDeviceProperty -KeyName DEVPKEY_Device_HardwareIds).Data -contains 'ROOT\NPURENDER_KMD_LAB'
}
if(@($device).Count -ne 1 -or $device.Status -ne 'OK'){throw 'Expected healthy lab driver required'}
$version=($device | Get-PnpDeviceProperty -KeyName DEVPKEY_Device_DriverVersion).Data
if($version -ne $config.driver_version){throw 'Installed lab driver version mismatch'}
if(Test-Path "$directory\probe.stdout.jsonl"){throw 'Evidence already exists'}
$env:NR_REGISTERED_RUNTIME='C:\npu-lab\registered-runtime-v31'
foreach($file in (Get-Content "$env:NR_REGISTERED_RUNTIME\sha256.json" -Raw | ConvertFrom-Json)){
 if($file.name -notmatch '^[a-zA-Z0-9_.-]+$' -or (Get-FileHash (Join-Path $env:NR_REGISTERED_RUNTIME $file.name) -Algorithm SHA256).Hash -ne $file.sha256){throw 'Runtime dependency mismatch'}
}
$env:NR_REGISTERED_LAB_EXCHANGE=$directory
$started=[DateTime]::UtcNow.ToString('o')
$process=Start-Process -FilePath $probe -ArgumentList '--present-draw-npu-remote' -WindowStyle Hidden -PassThru -Wait -RedirectStandardOutput "$directory\probe.stdout.jsonl" -RedirectStandardError "$directory\probe.stderr.log"
$code=$process.ExitCode
} catch { Write-Warning $_.Exception.Message }
finally {
[ordered]@{started=$started;finished=[DateTime]::UtcNow.ToString('o');probe_sha256=$config.probe_sha256;exit_code=$code;local_npu_execution_tested=$false;desktop_offload_tested=$false} | ConvertTo-Json | Set-Content "$directory\probe-exit.json"
Stop-Transcript
}
exit $code
