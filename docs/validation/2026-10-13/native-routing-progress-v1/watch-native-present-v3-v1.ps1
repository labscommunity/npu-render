$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$report="$r\vm\native-present-v3-watch-v1.json";$end=[DateTime]::UtcNow.AddMinutes(60)
if(Test-Path $report){throw 'Presentation watcher evidence exists'}
$state=[ordered]@{started=[DateTime]::UtcNow.ToString('o');deadline=$end.ToString('o');controller_pid=$PID;completed=$false;probe_launched=$false;status='waiting-for-interactive-user-and-idle-experiments';full_desktop_offload=$false}
function Save-Watch {$state|ConvertTo-Json -Depth 5|Set-Content $report}
Save-Watch
try {
 while([DateTime]::UtcNow -lt $end){
  $build="$r\builds\native-present-v3";$manifest=Get-Content "$build\build-report.json" -Raw|ConvertFrom-Json
  if(!$manifest.clean_build -or (Get-FileHash "$build\Release\nr_frame_present_probe.exe").Hash -ne $manifest.binary_sha256){throw 'Session gate binary mismatch'}
  $gate=& "$build\Release\nr_frame_present_probe.exe" --inspect-session|ConvertFrom-Json;if($LASTEXITCODE -or !$gate.session_inspection){throw 'Session inspection failed'}
  $signedIn=[bool]$gate.ready_for_interactive_task
  $busy=@(Get-Process -Name 'nr_registered*','nr_native_service_probe' -ErrorAction SilentlyContinue).Count -gt 0
  $state.session_gate=$gate
  $state.last_checked=[DateTime]::UtcNow.ToString('o');$state.interactive_user_present=$signedIn;$state.experiments_busy=$busy;Save-Watch
  if($signedIn -and !$busy){
   $state.status='running-finite-presentation-probe';$state.probe_launched=$true;Save-Watch
   & ([scriptblock]::Create([IO.File]::ReadAllText("$r\vm\run-native-present-v3.ps1")))
   $q=Get-Content "$r\vm\native-present-v3-trial\report.json" -Raw|ConvertFrom-Json
   if(!$q.completed -or !$q.presentation_events_measured){throw 'Screen probe completion evidence absent'}
   $state.status='presentation-probe-completed';$state.presentation_modes=$q.presentation_modes
   break
  }
  Start-Sleep -Seconds 10
 }
 if(!$state.probe_launched){$state.status='expired-without-ready-interactive-desktop'}
}catch{$state.status='failed';$state.error=$_.Exception.Message;throw}
finally{$state.completed=$true;$state.finished=[DateTime]::UtcNow.ToString('o');Save-Watch}
$state|ConvertTo-Json -Depth 5
