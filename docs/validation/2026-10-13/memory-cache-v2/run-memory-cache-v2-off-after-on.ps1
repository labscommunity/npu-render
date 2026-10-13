$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$until=[DateTime]::UtcNow.AddMinutes(15);$ready=$false
while([DateTime]::UtcNow -lt $until -and !$ready){
 $path="$r\vm\memory-cache-v2-chain.json";$stream=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete));try{$reader=[IO.StreamReader]::new($stream);$content=$reader.ReadToEnd();try{$m=$content|ConvertFrom-Json}catch{$m=$null}}finally{$stream.Dispose()}
 if($m -and $m.completed){if(!$m.passed){throw 'Enabled qualification failed'};$ready=$true;break};Start-Sleep -Seconds 5
}
if(!$ready){throw 'Enabled qualification not completed before deadline'}
& ([scriptblock]::Create([IO.File]::ReadAllText("$r\vm\qualify-ordered-memory-cache-v2-off.ps1")))
