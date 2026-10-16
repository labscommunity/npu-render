$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\native-response-envelope-v1-qualification"
$q=Get-Content "$out\report.json" -Raw|ConvertFrom-Json
if($q.completed -or $q.active_phase -ne 'cpu-component-build'){throw 'Controller state changed'}
$p=Get-CimInstance Win32_Process -Filter 'ProcessId=10656'
if(!$p -or $p.Name -ne 'powershell.exe' -or $p.CommandLine -notlike '*-File *run-response-envelope-v1-qualification.ps1'){throw 'Owned controller identity changed'}
if(@(Get-CimInstance Win32_Process|Where-Object ParentProcessId -eq $p.ProcessId).Count){throw 'Build child is still active'}
if(@(Get-Process nr_registered*,nr_native*,nr_ptl*,nr_response* -ErrorAction SilentlyContinue).Count){throw 'Graphics or comparison active'}
$b=Get-Content "$r\builds\native-response-envelope-v1\build-report.json" -Raw|ConvertFrom-Json
if(!$b.clean_build -or (Get-FileHash "$r\builds\native-response-envelope-v1\Release\nr_response_envelope_compare.exe").Hash -ne $b.binary_sha256){throw 'Completed build identity missing'}
[ordered]@{controller_pid=$p.ProcessId;created=$p.CreationDate;command=$p.CommandLine;observed_phase=$q.active_phase;build_completed=$true;build_sha256=$b.binary_sha256;direct_children=0;graphics_active=$false;reason='Parent remains in build wait after child exited; bounded coordinator replaces this wait';at=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json -Depth 4|Set-Content "$out\controller-wait-recovery.json"
Stop-Process -Id $p.ProcessId -ErrorAction Stop
$q.completed=$true;$q.passed=$false;$q|Add-Member -NotePropertyName error -NotePropertyValue 'Owned controller terminated after completed build and exited child; comparison did not run'
$q|Add-Member -NotePropertyName finished -NotePropertyValue ([DateTime]::UtcNow.ToString('o'))
$q|ConvertTo-Json -Depth 6|Set-Content "$out\report.json"
Get-Content "$out\controller-wait-recovery.json" -Raw
