$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue'
$cases=@(
 @(@{phase='extended';path_count=1},@{indirect_display_active=$true}),
 @(@{phase='extended';path_count=1},@{indirect_display_active=$true},@{operation='restore';win32_status=31}),
 @(@{phase='extended';path_count=1},@{indirect_display_active=$true},@{passed=$false}),
 @(@{phase='extended';path_count=1},@{indirect_display_active=$true},@{error='helper failure'}),
 @(@{phase='extended';path_count=2},@{indirect_display_active=$true})
)
for($i=0;$i -lt $cases.Count;$i++) {
 $text=($cases[$i]|ForEach-Object {$_|ConvertTo-Json -Compress}) -join "`n"
 $state=@{sole_indirect=$false}
     $rows=@($text -split "`n" |
      Where-Object {$_.Trim()} | ForEach-Object {$_|ConvertFrom-Json})
     $last=$rows|Where-Object {$null -ne $_.path_count}|Select-Object -Last 1
     $admitted=@($rows|Where-Object {$_.indirect_display_active -eq $true}).Count -gt 0
     $finished=@($rows|Where-Object {
      $_.operation -eq 'restore' -or $null -ne $_.passed -or $null -ne $_.error
     }).Count -gt 0
     $state.sole_indirect=$admitted -and !$finished -and
      $last.phase -eq 'extended' -and $last.path_count -eq 1
 if($state.sole_indirect -ne ($i -eq 0)){throw "Gate case $i failed"}
}
'{"kind":"collector-powershell-topology-gate","cases":5,"restore_failure_disables_input":true,"passed":true}'
