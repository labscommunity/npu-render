$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$record="$r\vm\memory-cache-v2-chain.json";$stage='waiting-for-abba';$at=[DateTime]::UtcNow.ToString('o')
if(Test-Path $record){throw 'Chain evidence already exists'}
function Save-State([bool]$done,[bool]$passed,[string]$failure){[ordered]@{started=$at;updated=[DateTime]::UtcNow.ToString('o');controller_pid=$PID;stage=$stage;completed=$done;passed=$passed;error=$failure}|ConvertTo-Json -Depth 4|Set-Content $record}
try{
 Save-State $false $false ''
 $until=[DateTime]::UtcNow.AddMinutes(40);$ready=$false
 while([DateTime]::UtcNow -lt $until -and !$ready){
  $path="$r\vm\compact-abba-v1\summary.json";$m=$null
  if(Test-Path $path){$stream=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete));try{$reader=[IO.StreamReader]::new($stream);$content=$reader.ReadToEnd();try{$m=$content|ConvertFrom-Json}catch{$m=$null}}finally{$stream.Dispose()}}
  if($m -and $m.completed -and $m.cases.Count -eq 4 -and @($m.cases|Where-Object {!$_.passed}).Count -eq 0){$ready=$true;break}
  Start-Sleep -Seconds 5
 }
 if(!$ready){throw 'Completed four-case comparison was not available before deadline'}
 if(@(Get-Process nr_registered_indexed_worker,nr_registered_indexed_stream_worker -ErrorAction SilentlyContinue).Count){throw 'Another renderer still active after comparison'}
 $stage='source-admission';Save-State $false $false ''
 if(Test-Path "$r\vm\memory-cache-v2"){throw 'Experiment source already exists'}
 & tar -xzf "$r\vm\memory-cache-v2-source.tar.gz" -C "$r\vm";if($LASTEXITCODE){throw 'Source extraction failed'}
 foreach($script in @('build-memory-cache-v2.ps1','qualify-memory-cache-v2.ps1','qualify-ordered-memory-cache-v2.ps1')){
  $stage=$script;Save-State $false $false ''
  $errors=$null;$tokens=$null;[void][Management.Automation.Language.Parser]::ParseFile("$r\vm\$script",[ref]$tokens,[ref]$errors);if($errors){throw ($errors|Out-String)}
  & ([scriptblock]::Create([IO.File]::ReadAllText("$r\vm\$script")))
 }
 $stage='completed';Save-State $true $true ''
 'Memory cache build, CPU/NPU contracts and full trace replay completed'
}catch{Save-State $true $false $_.Exception.Message;throw}
