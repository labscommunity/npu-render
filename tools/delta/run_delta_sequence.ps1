# Runs a sequence of steps on Delta and keeps one status file current after every step, so the
# controlling host polls a single JSON document instead of chaining remote commands. Each step is
# a script path (PowerShell .ps1 or Python .py run with the trial's interpreter) with arguments,
# an optional regex a passing step must print, and an optional directory a passing step must
# leave a report.json with "passed": true in. A failed step stops the sequence unless the plan
# marks it "continue". The status file carries the plan's run id so a stale file from an earlier
# run can never be mistaken for this one.
param([Parameter(Mandatory=$true)][string]$PlanPath)
$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue'
$spec=Get-Content $PlanPath -Raw | ConvertFrom-Json
$r="$env:LOCALAPPDATA\npu-render";$py="$r\toolchains\openvino-2026.4.1\Scripts\python.exe"
$script:statusPath=[string]$spec.status
$script:status=[ordered]@{run_id=$spec.run_id;plan=$PlanPath;started=[DateTime]::UtcNow.ToString('o');done=$false;passed=$false;steps=@()}
function Save{ $tmp="$($script:statusPath).tmp";$script:status|ConvertTo-Json -Depth 6|Set-Content $tmp -Encoding UTF8;Move-Item $tmp $script:statusPath -Force }
Save
$ok=$true
foreach($step in $spec.steps){
  $entry=[ordered]@{name=$step.name;started=[DateTime]::UtcNow.ToString('o');ok=$false;exit_code=$null;matched=@();error=$null;log="$($spec.log_dir)\$($step.name).log"}
  $script:status.steps+=$entry;Save
  New-Item -ItemType Directory $spec.log_dir -Force|Out-Null
  $sw=[Diagnostics.Stopwatch]::StartNew()
  try{
    if(-not $ok -and -not $step.always){ $entry.error='skipped: earlier step failed'; $entry.finished=[DateTime]::UtcNow.ToString('o'); Save; continue }
    # Positional arguments travel as a list; a PowerShell script's named parameters travel as an
    # object (array splatting is positional, so '-Name value' pairs in a list would misbind).
    $stepArgs=@($step.args | ForEach-Object { [string]$_ });$named=@{}
    if($step.named){ foreach($prop in $step.named.psobject.Properties){ $named[$prop.Name]=[string]$prop.Value } }
    $global:LASTEXITCODE=0
    $output=if($step.script -like '*.py'){ & $py $step.script @stepArgs 2>&1 } else { & $step.script @stepArgs @named 2>&1 }
    # A script that never calls exit leaves no exit code; its errors surface as exceptions above.
    $code=if($null -eq $LASTEXITCODE){0}else{$LASTEXITCODE}
    $text=($output | ForEach-Object { "$_" }) -join "`n"
    Set-Content $entry.log $text -Encoding UTF8
    $entry.exit_code=$code
    if($step.match){ $m=[regex]::Matches($text,$step.match) | ForEach-Object { $_.Value }; $entry.matched=@($m | Select-Object -First 20) }
    $reportOk=$true
    if($step.report){ $j=Get-Content $step.report -Raw|ConvertFrom-Json; $reportOk=[bool]$j.passed; $entry.report_passed=$reportOk; if($j.error){ $entry.report_error="$($j.error)" } }
    $entry.ok=($code -eq 0 -or $step.ignore_exit) -and $reportOk -and (-not $step.match -or $entry.matched.Count -gt 0)
    $entry.tail=($text -split "`n" | Select-Object -Last 6) -join "`n"
  }catch{ $entry.error=$_.Exception.Message; $entry.ok=$false }
  $entry.seconds=[int]$sw.Elapsed.TotalSeconds;$entry.finished=[DateTime]::UtcNow.ToString('o')
  if(-not $entry.ok -and -not $step.continue){ $ok=$false }
  Save
}
$script:status.passed=$ok;$script:status.done=$true;$script:status.finished=[DateTime]::UtcNow.ToString('o');Save
