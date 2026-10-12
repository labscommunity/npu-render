$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render"
. ([scriptblock]::Create([IO.File]::ReadAllText("$r\vm\open-guest-console-lite.ps1")))
try{Invoke-Command -Session $nrLab -ScriptBlock {
 $directory='C:\npu-lab\indexed-stream-15a4a98f82664a6ab7ccdce46ffa2a8b';$policy=New-Object -ComObject HNetCfg.FwPolicy2
 $rules=@($policy.Rules|Where-Object{$_.Name -eq 'NpuRenderIndexed-15a4a98f82664a6ab7ccdce46ffa2a8b'})
 [ordered]@{at=[DateTime]::UtcNow.ToString('o');config_exists=[IO.File]::Exists('C:\npu-lab\indexed-pipe-active.txt');relay_count=@([Diagnostics.Process]::GetProcessesByName('nr_registered_indexed_stream_relay')).Count;owned_firewall_rules=$rules.Count;guest_token_exists=[IO.File]::Exists("$directory\private-token.bin")}|ConvertTo-Json
}}finally{Remove-PSSession $nrLab}
