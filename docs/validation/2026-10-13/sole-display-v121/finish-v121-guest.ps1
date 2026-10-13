$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render"
. ([scriptblock]::Create([IO.File]::ReadAllText("$r\vm\open-guest-console-lite.ps1")))
try {Invoke-Command -Session $nrLab -ScriptBlock {
 $ErrorActionPreference='Stop';. ([scriptblock]::Create([IO.File]::ReadAllText('C:\npu-lab\lab_device_identity.ps1')));Assert-NrLabGuest
 $stop='C:\npu-lab\idd-topology-v9bc.stop'
 if(!(Test-Path $stop)){[IO.File]::WriteAllText($stop,'owned v121 trial visual review complete; restore original topology')}
 [ordered]@{requested_at=[DateTime]::UtcNow.ToString('o');topology='idd-topology-v9bc';stop_requested=$true}|ConvertTo-Json|Set-Content C:\npu-lab\lab-v121-controller-stop.json
}}
finally {Remove-PSSession $nrLab}
