$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$out="$r\vm\native-live-v104";$source="$env:LOCALAPPDATA\Temp\npu-graphics-probe-0146a2388f25";$native=$null;$topology=$null;$relay=$null;$tasks=@();$cursorStarted=$false;$cursorCued=$false
if(Test-Path $out){throw 'Live trial exists'}
$proof=Get-Content "$r\builds\native-present-v24\build-report.json" -Raw|ConvertFrom-Json;$present="$r\builds\native-present-v24\Release\nr_frame_present_probe.exe"
$reader=Get-Content "$r\vm\live-frame-reader-v15\report.json" -Raw|ConvertFrom-Json
if(!$proof.clean_build -or !$reader.passed -or (Get-FileHash $present).Hash -ne $proof.binary_sha256 -or $reader.binary_sha256 -ne $proof.binary_sha256){throw 'Live presenter identity not qualified'}
$prior=Get-Content "$r\vm\native-extended-v2\report.json" -Raw|ConvertFrom-Json;if(!$prior.passed -or !$prior.cleanup_passed){throw 'Native extension prerequisite failed'}
$screenProof=Get-Content "$r\builds\native-output-observer-v7\build-report.json" -Raw|ConvertFrom-Json
$screenQualification=Get-Content "$r\vm\native-output-observer-v7-qualification\report.json" -Raw|ConvertFrom-Json
$screenBinary="$r\builds\native-output-observer-v7\Release\nr_output_observer.exe"
if(!$screenProof.clean_build -or !$screenQualification.completed -or !$screenQualification.passed -or $screenQualification.binary_sha256 -ne $screenProof.binary_sha256 -or (Get-FileHash $screenBinary).Hash -ne $screenProof.binary_sha256){throw 'Physical output observer is not qualified'}
$cursorProof=Get-Content "$r\builds\native-topology-control-v15\build-report.json" -Raw|ConvertFrom-Json
$cursorQualification=Get-Content "$r\vm\native-cursor-guard-v7-qualification\report.json" -Raw|ConvertFrom-Json
if(!$cursorProof.clean_build -or !$cursorQualification.passed -or !$cursorQualification.completed -or @($cursorQualification.cases).Count -ne 2 -or @($cursorQualification.cases|Where-Object {$_.case -eq 'owner-terminated' -and $_.recovery.recovery_released_owned_clip -and $_.passed}).Count -ne 1){throw 'Pointer qualification failed'}
$cursorQualifiedBuild=Get-Content "$r\vm\native-cursor-guard-v7-qualification\build-report.json" -Raw|ConvertFrom-Json
if($cursorQualifiedBuild.binary_sha256 -ne $cursorProof.binary_sha256 -or !$cursorQualification.foreground_input_passed -or !$cursorQualification.foreground_release_reacquired){throw 'Cursor foreground transition is not qualified'}
$adapterProof=Get-Content "$r\builds\native-adapter-choice-v1\build-report.json" -Raw|ConvertFrom-Json
$adapterBaseline=Get-Content "$r\vm\native-adapter-choice-v1-baseline\report.json" -Raw|ConvertFrom-Json
$adapterBinary="$r\builds\native-adapter-choice-v1\Release\nr_default_adapter_probe.exe"
if(!$adapterProof.clean_build -or !$adapterBaseline.completed -or !$adapterBaseline.passed -or $adapterBaseline.binary_sha256 -ne $adapterProof.binary_sha256 -or (Get-FileHash $adapterBinary).Hash -ne $adapterProof.binary_sha256){throw 'Ordinary adapter baseline not qualified'}
$lastNative=Get-Content "$r\vm\native-desktop-admission-v35\report.json" -Raw|ConvertFrom-Json
$failedReplay=Get-Content "$r\vm\native-ordered-replay-v28-pointer-rollback\report.json" -Raw|ConvertFrom-Json
if(!$lastNative.completed -or !$lastNative.cleanup_passed -or !$failedReplay.completed -or !$failedReplay.passed){throw 'Previous rollback cleanup and isolated replay are required'}
$pointerQualified=Get-Content "$r\vm\native-live-v23\report.json" -Raw|ConvertFrom-Json
if(!$pointerQualified.completed -or !$pointerQualified.passed -or !$pointerQualified.pointer_confined_and_restored -or !$pointerQualified.cleanup_passed){throw 'Native pointer baseline is not qualified'}
foreach($path in @('native-quad-abba-v1','quad-pipeline-v1-qualification','native-ordered-replay-v29-quad-rollback','native-registration-v92','native-provider-process-v2','native-ordered-replay-v39-provider-soak')){$q=Get-Content "$r\vm\$path\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.passed){throw 'Quad runtime prerequisites failed'}}
$baselineMs=0.;$candidateMs=0.
foreach($phase in @('A1','B1','B2','A2')){
 $draws=@(Get-Content "$r\vm\native-quad-abba-v1-$phase\worker.stdout.jsonl"|Where-Object {$_ -match '^\{.*\}$'}|ForEach-Object {$_|ConvertFrom-Json}|Where-Object {$_.PSObject.Properties.Name -contains 'compile_and_draw_ms'})
 if($draws.Count -ne 13){throw 'Matched worker timing evidence incomplete'}
 $sum=($draws|Measure-Object -Property compile_and_draw_ms -Sum).Sum
 if($phase.StartsWith('A')){$baselineMs+=$sum}else{$candidateMs+=$sum}
}
if($candidateMs -ge $baselineMs/2){throw 'Candidate did not meet the measured component improvement gate'}
New-Item -ItemType Directory $out|Out-Null
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;native_trial='native-desktop-admission-v114';topology_trial='native-topology-primary-v90';desktop_offloaded=$false;input_forwarding_tested=$false;tahoma_tested=$false;no_stimulus=$true}
. ([scriptblock]::Create([IO.File]::ReadAllText("$r\vm\atomic_lab_report_v1.ps1")))
function Save-Report{Write-NrLabReport "$out/report.json" $report}
function Start-Owned([string]$exe,[string[]]$arguments,[string]$name){$quoted=@($arguments|ForEach-Object {'"'+$_.Replace('"','\"')+'"'});$process=Start-Process $exe -ArgumentList $quoted -PassThru -NoNewWindow -RedirectStandardOutput "$out\$name.stdout.log" -RedirectStandardError "$out\$name.stderr.log";[void]$process.Handle;return $process}
function Task-State([string]$n){for($i=0;$i -lt 5;$i++){try{return (Get-ScheduledTask -TaskName $n -ErrorAction Stop).State}catch{if($i -eq 4){throw};Start-Sleep -Milliseconds 300}}}
function Start-Interactive([string]$name,[string[]]$command){
 $dir="$out\$name";New-Item -ItemType Directory $dir|Out-Null;$task="T8-NpuRender-NativeLiveV104-$name"
 @{command=$command;log="$dir\probe.log"}|ConvertTo-Json -Depth 4|Set-Content -Encoding UTF8 "$dir\job.json"
 $action=New-ScheduledTaskAction -Execute "$source\venv\Scripts\pythonw.exe" -Argument ('"'+$source+'\native_runner.py" "'+$dir+'\job.json"')
 $principal=New-ScheduledTaskPrincipal -UserId ([Security.Principal.WindowsIdentity]::GetCurrent().User.Value) -LogonType Interactive -RunLevel Limited
 $settings=New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes $(if($name -eq 'cursor'){236}else{233})) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
 Register-ScheduledTask -TaskName $task -Action $action -Principal $principal -Settings $settings|Out-Null;$script:tasks+=$task;Start-ScheduledTask -TaskName $task
}
function Read-Rows([string]$path){if(!(Test-Path $path)){return @()};return @(Get-Content $path|Where-Object {$_ -match '^\{.*\}$'}|ForEach-Object {$_|ConvertFrom-Json})}
Save-Report
try{
 $observerStart=Start-Process powershell.exe -ArgumentList @('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',('"'+$r+'\vm\start-native-topology-v211.ps1"')) -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$out\observer-start.log" -RedirectStandardError "$out\observer-start.stderr.log"
 try{$report.observer_start_exit=$observerStart.ExitCode;if($observerStart.ExitCode){throw 'Observer failed before native admission'}}finally{$observerStart.Dispose()}
 $native=Start-Owned "$env:windir\System32\WindowsPowerShell\v1.0\powershell.exe" @('-NoProfile','-NonInteractive','-ExecutionPolicy','RemoteSigned','-File',"$r\vm\run-native-desktop-v114.ps1",'-Mode','admit') 'native';$report.controller_pid=$native.Id;Save-Report
 $deadline=[DateTime]::UtcNow.AddSeconds(75);$ready=$false
 while([DateTime]::UtcNow -lt $deadline){
  if($native.HasExited){throw 'Native admission exited early'}
  try{$b=Get-Content "$r\vm\native-desktop-admission-v114\bootstrap\report.json" -Raw|ConvertFrom-Json;$sample=Get-Content "$r\vm\native-topology-v211\samples.jsonl" -Tail 1|ConvertFrom-Json;$ready=$b.admission_published -and !$b.completed -and @($sample.entries|Where-Object active_path).Count -eq 2}catch{}
  if($ready){break};Start-Sleep -Milliseconds 250
 };if(!$ready){throw 'Indirect topology not ready'}
 if(!(Get-ScheduledTask -TaskName T8-NpuRender-NativeDesktopRecovery-v114 -ErrorAction SilentlyContinue)){throw 'Independent recovery not armed'}
 $topology=Start-Owned "$env:windir\System32\WindowsPowerShell\v1.0\powershell.exe" @('-NoProfile','-NonInteractive','-ExecutionPolicy','RemoteSigned','-File',"$r\vm\run-native-topology-helper-v15.ps1",'-Mode','primary-cursor','-Trial','native-topology-primary-v90','-Seconds','13940','-CursorCue',"$out\cues\cursor") 'topology'
 $topologyLog="$r\vm\native-topology-primary-v90\probe.log";$deadline=[DateTime]::UtcNow.AddSeconds(45);$extended=$false
 while([DateTime]::UtcNow -lt $deadline){if($topology.HasExited -or $native.HasExited){throw 'Trial ended before extension'};$extended=@(Read-Rows $topologyLog|Where-Object extension_verified).Count -eq 1;if($extended){break};Start-Sleep -Milliseconds 250}
 if(!$extended){throw 'Independent desktop extension not verified'};$extensionObserved=[DateTime]::UtcNow
 New-Item -ItemType Directory "$out\cues"|Out-Null
 Start-Interactive 'cursor' @("$source\venv\Scripts\python.exe","$r\vm\cursor_recovery_watchdog_v4.py",'--log',$topologyLog,'--binary-sha256',$cursorProof.binary_sha256,'--output',"$out\cursor\recovery",'--seconds','14040');$cursorStarted=$true
 $cursorDeadline=[DateTime]::UtcNow.AddSeconds(15);while(!(Test-Path "$out\cursor\recovery\ready.json") -and [DateTime]::UtcNow -lt $cursorDeadline){Start-Sleep -Milliseconds 100}
 if(!(Test-Path "$out\cursor\recovery\ready.json")){throw 'Independent cursor watchdog did not arm'}

 Start-Interactive 'adapter' @($adapterBinary)
 $relay=Start-Owned "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" @("$r\vm\relay_native_frames_v9.py",'--transaction',"$r\vm\native-desktop-admission-v114",'--topology',$topologyLog,'--output',"$out\relay",'--seconds','13930') 'relay'
 $deadline=[DateTime]::UtcNow.AddSeconds(150);$frame=$null
 while([DateTime]::UtcNow -lt $deadline){if($relay.HasExited -or $native.HasExited -or $topology.HasExited){throw 'Trial ended before relay frame'}
  try{$rr=Get-Content "$out\relay\report.json" -Raw|ConvertFrom-Json;if($rr.published -gt 0 -and $rr.producer_and_compositor_verified){$frame=$rr.latest;break}}catch{}
  Start-Sleep -Milliseconds 250
 };if(!$frame){throw 'No verified live relay frame within 150 seconds'}
 $remaining=13920-([DateTime]::UtcNow-$extensionObserved).TotalSeconds;$duration=[Math]::Min(13890,[Math]::Floor($remaining));if($duration -lt 20){throw 'Insufficient separated-desktop time for live test'}
 Start-Interactive 'present' @($present,'--live',"$out\relay\latest.bin",([string]$duration),([string]$frame.producer_pid));$report.first_relay=$frame;$report.presentation_seconds=$duration;Save-Report
 $deadline=[DateTime]::UtcNow.AddSeconds($duration+15)
 $screenStarted=$false
 do{
  $report.active_phase='presentation-health';
  if($relay.HasExited){throw 'Frame relay exited during presentation'}
  if($native.HasExited -or $topology.HasExited){throw 'Native renderer or topology ended during presentation'}
  Start-Sleep -Milliseconds 250;$presentState=(Task-State 'T8-NpuRender-NativeLiveV104-present');$presentRows=Read-Rows "$out\present\probe.log"
  $presentReady=@($presentRows|Where-Object probe_ready);$physicalRoute=@($presentRows|Where-Object physical_route_selected)
  if(!$screenStarted -and $presentReady.Count -eq 1 -and $physicalRoute.Count -eq 1){
   if(!$physicalRoute[0].indirect_primary -or $physicalRoute[0].physical_x -ne 1024 -or $physicalRoute[0].physical_y -ne 0){throw 'Primary presentation route mismatch'}
   Start-Interactive 'screen' @($screenBinary,'--observe',([string]$presentReady[0].pid),([string]([Math]::Max(1,$duration-15))),"$out\screen\stop");$screenStarted=$true
  }
  $screenRows=Read-Rows "$out\screen\probe.log"
  if(!$cursorCued -and $screenStarted -and @($screenRows|Where-Object {@($_.points|Where-Object owned_presenter_at_point).Count -gt 0}).Count -gt 0){
   $cursorReady=Get-Content "$out\cursor\recovery\ready.json" -Raw|ConvertFrom-Json;if(!$cursorReady.armed){throw 'Cursor recovery no longer armed'}
   New-Item -ItemType File "$out\cues\cursor"|Out-Null;$cursorCued=$true;$report.cursor_cued_at=[DateTime]::UtcNow.ToString('o');Save-Report
  }
  # Keep the independent physical observer running for the complete presentation interval.
 }while(($presentState -eq 'Running' -or !$presentRows.Count) -and [DateTime]::UtcNow -lt $deadline)
 if($screenStarted){New-Item -ItemType File "$out\screen\stop" -Force|Out-Null;$screenDeadline=[DateTime]::UtcNow.AddSeconds(40);while((Task-State 'T8-NpuRender-NativeLiveV104-screen') -eq 'Running' -and [DateTime]::UtcNow -lt $screenDeadline){Start-Sleep -Milliseconds 250}}

 if($presentState -eq 'Running'){throw 'Presenter exceeded finite duration'}
 New-Item -ItemType File "$out\relay\stop" -Force|Out-Null
 if(!$relay.WaitForExit(5000)){throw 'Relay failed to stop after presentation'}
 $relay.Refresh();$relayResult=Get-Content "$out\relay\report.json" -Raw|ConvertFrom-Json
 $report.relay_exit=$relay.ExitCode;$report.relay_completed=$relayResult.completed;$report.relay_passed=$relayResult.passed
 if($relay.ExitCode -ne 0 -or !$relayResult.completed -or !$relayResult.passed -or $relayResult.error){throw 'Relay final evidence failed'}
 $report.active_phase='finite-topology-completion';Save-Report
 $presentInfo=Get-ScheduledTaskInfo -TaskName T8-NpuRender-NativeLiveV104-present;$report.presenter_task_result=$presentInfo.LastTaskResult
 if(!$topology.WaitForExit(13980000)){throw 'Topology trial timed out'};$topology.Refresh();$report.topology_exit=$topology.ExitCode
 if(!$native.WaitForExit(14120000)){throw 'Native trial timed out'};$native.Refresh();$report.native_exit=$native.ExitCode
 $nr=Get-Content "$r\vm\native-desktop-admission-v114\report.json" -Raw|ConvertFrom-Json;$report.cleanup_passed=$nr.cleanup_passed
 $adapterRows=Read-Rows "$out\adapter\probe.log";$adapterDone=@($adapterRows|Where-Object adapter_probe_completed);$adapterExit=@($adapterRows|Where-Object {$_.PSObject.Properties.Name -contains 'exit_code'})
 if($adapterDone.Count -ne 1 -or !$adapterDone[0].passed -or $adapterDone[0].graphics_draws_submitted -ne 0 -or $adapterExit.Count -ne 1 -or $adapterExit[0].exit_code -ne 0){throw 'Ordinary adapter observation incomplete'}
 $report.application_adapter_observation=@($adapterRows|Where-Object {$_.adapter_observation -or $_.default_device_request -or $_.adapter_probe_started})
 $report.application_default_device_created=@($adapterRows|Where-Object {$_.default_device_request -and $_.device_created}).Count -gt 0
 $report.application_adapter_baseline=$adapterBaseline.adapters;$report.application_graphics_offload_qualified=$false
 $frames=@($presentRows|Where-Object live_frame_presented);$done=@($presentRows|Where-Object live_bridge_tested);$published=Read-Rows "$out\relay\frames.jsonl"
 $report.physically_submitted_frames=$frames.Count;$report.matching_source_frames=@($frames|Where-Object {$f=$_;@($published|Where-Object {$_.sequence -eq $f.source_sequence -and $_.qpc -eq $f.source_qpc -and $_.producer_pid -eq $f.producer_pid}).Count -eq 1}).Count
 $report.backbuffer_copies_exact=@($frames|Where-Object {!$_.backbuffer_exact}).Count -eq 0
 $report.distinct_source_pixel_hashes=@($published|Select-Object -ExpandProperty pixel_sha256 -Unique).Count
 $presentExit=@($presentRows|Where-Object {$_.PSObject.Properties.Name -contains 'exit_code'})
 $report.presenter_child_success=($presentExit.Count -eq 1 -and $presentExit[0].exit_code -eq 0)
 $screenFinalRows=Read-Rows "$out\screen\probe.log";$screenDone=@($screenFinalRows|Where-Object {$_.completed -and $_.physical_output_observer});$screenExit=@($screenFinalRows|Where-Object {$_.PSObject.Properties.Name -contains 'exit_code'})
 $report.physical_blue_green_red_observed=($screenDone.Count -eq 1 -and $screenDone[0].passed -and $screenExit.Count -eq 1 -and $screenExit[0].exit_code -eq 0)
 $report.screen_observer='DXGI physical-output duplication; two-pixel GPU readback; ownership before and after'
 if($screenDone.Count -eq 1){$report.screen_observer_result=$screenDone[0]}
 $report.physical_presenter_observed=($screenDone.Count -eq 1 -and @($screenFinalRows|Where-Object {$_.ownership_verified_before_and_after -and @($_.points).Count -eq 2 -and @($_.points|Where-Object owned_presenter_at_point).Count -eq 2}).Count -gt 0)
 $topologyResult=Get-Content "$r\vm\native-topology-primary-v90\report.json" -Raw|ConvertFrom-Json
 $recoveryRows=Read-Rows $topologyLog;$placements=@($recoveryRows|Where-Object window_placement_recovery)
 $report.primary_topology_restored=$topologyResult.passed -and @($recoveryRows|Where-Object {$_.completed -and $_.passed -and $_.topology_restored_exactly}).Count -eq 1
 $report.ordinary_windows_restored=$placements.Count -eq 1 -and $placements[0].failed -eq 0 -and $placements[0].cursor_restored -and $placements[0].foreground_restored
 $cursorResult=Get-Content "$out\cursor\recovery\report.json" -Raw|ConvertFrom-Json
 $report.pointer_confined_and_restored=($cursorCued -and $cursorResult.completed -and $cursorResult.passed -and $cursorResult.owned_confinement_observed -and $cursorResult.original_unconfined_restored)
 $report.pointer_reacquisitions=@($recoveryRows|Where-Object cursor_reacquired_after_release).Count
 $bootstrap=Get-Content "$r\vm\native-desktop-admission-v114\bootstrap\report.json" -Raw|ConvertFrom-Json;$report.texture_scope=$bootstrap.texture_scope;$report.memory_cache=$bootstrap.memory_cache;if($bootstrap.texture_scope -ne '1' -or $bootstrap.memory_cache -ne '1'){throw 'Native provider did not use qualified scoped settings'};$report.isolated_provider=$bootstrap.provider_isolated;$report.provider_generations=$bootstrap.provider_generations;$report.provider_faults=$bootstrap.provider_faults;$report.provider_retries=$bootstrap.provider_retries;$report.hardware_child_receipts=$bootstrap.hardware_child_receipts
 & "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" "$r\vm\native_provider_generation_audit_v2.py" --bootstrap "$r\vm\native-desktop-admission-v114\bootstrap" --output "$out\provider-generation-audit" *> "$out\provider-generation-audit.log"
 if($LASTEXITCODE){throw 'Per-generation provider verification failed'}
 $generationProof=Get-Content "$out\provider-generation-audit\report.json" -Raw|ConvertFrom-Json
 if(!$generationProof.completed -or !$generationProof.passed){throw 'Per-generation provider evidence incomplete'}
 $report.provider_generation_audit_passed=$true

 $turboGenerations=@()
 foreach($log in @(Get-ChildItem "$r\vm\native-desktop-admission-v114\bootstrap\provider-processes\generation-*\worker.stdout.jsonl" -File)){
  $events=@(Read-Rows $log.FullName|Where-Object ptl_turbo_import_verified)
  if(!$events.Count -or @($events|Where-Object {!$_.requested -or !$_.readback -or $_.global_power_policy_changed}).Count){throw 'Turbo property readback absent or invalid'}
  $routes=@($events|Select-Object -ExpandProperty route)
  if(@($routes|Where-Object {$_ -notin @('program','solid','geometry','four_tap','point_rectangle','complex_shader','resident_clip','texture_shader','long_shader','masked','frame')}).Count){throw 'Unknown turbo import route'}
  $turboGenerations+=@{generation=$log.Directory.Name;imports=$events.Count;routes=$routes}
 }
 if(!$turboGenerations.Count){throw 'No turbo provider generation'}
 $report.turbo_import_audit_passed=$true;$report.turbo_generations=$turboGenerations

 $compiledGenerations=@()
 foreach($log in @(Get-ChildItem "$r\vm\native-desktop-admission-v114\bootstrap\provider-processes\generation-*\worker.stdout.jsonl" -File)){
  $events=@(Read-Rows $log.FullName|Where-Object {$_.ptl_turbo_import_verified -and $_.route -eq 'frame'})
  $blobCount=@(Get-Content "$r\vm\frame-blobs-v18\deploy\manifest.txt"|Where-Object {$_ -match '^\d+ [0-9a-f]{64} '}).Count
  if($events.Count -ne $blobCount -or @($events|Where-Object {!$_.requested -or !$_.readback -or $_.global_power_policy_changed}).Count){throw 'Frame kernel blob import readback absent or invalid'}
  $compiledGenerations+=@{generation=$log.Directory.Name;frame_blob_imports=$events.Count}
 }
 if(!$compiledGenerations.Count){throw 'No frame kernel provider generation'}
 $report.compiled_turbo_import_audit_passed=$true;$report.compiled_turbo_generations=$compiledGenerations
 $batchRows=@(Get-ChildItem "$r\vm\native-desktop-admission-v114\bootstrap\provider-processes\generation-*\worker.stdout.jsonl" -File|ForEach-Object {Read-Rows $_.FullName}|Where-Object frame_batch)
 $report.frame_batches=$batchRows.Count;$report.frame_batch_commands=($batchRows|Measure-Object -Property commands -Sum).Sum;$report.frame_batch_fallback_commands=($batchRows|Measure-Object -Property fallback -Sum).Sum
 $report.frame_batch_kernel_requests=($batchRows|Measure-Object -Property kernel_requests -Sum).Sum;$report.frame_batch_kernel_ms=($batchRows|Measure-Object -Property kernel_ms -Sum).Sum;$report.frame_batch_host_ms=($batchRows|Measure-Object -Property host_ms -Sum).Sum
 if($report.frame_batches -lt 1){throw 'No frame batch executed on the NPU during the native workload'}
 $umdRows=@(@("$env:ProgramData\T8-NpuRender-NativeTraceV1","$r\vm\native-desktop-admission-v114\T8-NpuRender-NativeTraceV1")|ForEach-Object {Get-ChildItem $_ -Filter 'process-*.jsonl' -File -ErrorAction SilentlyContinue}|ForEach-Object {Read-Rows $_.FullName})
 $report.umd_batched_draws=@($umdRows|Where-Object {$_.event -in @('draw_recorded','indexed_draw_recorded')}).Count;$report.umd_unbatched_draws=@($umdRows|Where-Object {$_.event -eq 'draw_not_batched'}).Count
 $report.umd_unbatched_reasons=@($umdRows|Where-Object {$_.event -eq 'draw_not_batched'}|Group-Object reason|ForEach-Object {@{reason=$_.Name;count=$_.Count}})
 $report.umd_flushes=@($umdRows|Where-Object {$_.event -eq 'frame_batch_flushed'}).Count
 $report.active_phase='freshness-validation';Save-Report
 & "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" "$r\vm\validate-native-delivery-no-stimulus-v1.py" --trial $out --output "$out\delivery-validation.json" > "$out\delivery-validation.stdout.log" 2> "$out\delivery-validation.stderr.log"
 if($LASTEXITCODE){throw 'Continued delivery and fresh physical observation not qualified'}
 $report.continuous_delivery_gate_passed=$true
 $report.passed=($report.pointer_confined_and_restored -and $report.physical_presenter_observed -and $report.primary_topology_restored -and $report.ordinary_windows_restored -and $report.presenter_child_success -and $report.presenter_task_result -eq 0 -and $report.topology_exit -eq 0 -and $report.native_exit -eq 0 -and $nr.passed -and $nr.cleanup_passed -and $frames.Count -ge 2 -and $report.matching_source_frames -eq $frames.Count -and $report.backbuffer_copies_exact -and $done.Count -eq 1 -and $report.distinct_source_pixel_hashes -ge 2)
}catch{$report.error=$_.Exception.Message;$report.error_position=$_.InvocationInfo.PositionMessage;$report.error_stack=$_.ScriptStackTrace;$report.error_type=$_.Exception.GetType().FullName;Save-Report}
finally{
 if(Test-Path "$out\screen"){New-Item -ItemType File "$out\screen\stop" -Force|Out-Null}
 if(Test-Path "$out\relay"){New-Item -ItemType File "$out\relay\stop" -Force|Out-Null}
 if($relay -and !$relay.HasExited){if(!$relay.WaitForExit(5000)){$relay.Kill();$relay.WaitForExit()}}
 foreach($task in @($tasks|Where-Object {$_ -notlike '*-cursor'})){Stop-ScheduledTask -TaskName $task -ErrorAction SilentlyContinue;Unregister-ScheduledTask -TaskName $task -Confirm:$false -ErrorAction SilentlyContinue}
 if($report.error -and (Test-Path "$out\cues")){New-Item -ItemType File "$out\cues\cursor.stop" -Force|Out-Null}
 if($topology -and !$topology.HasExited){[void]$topology.WaitForExit($(if($report.error){15000}else{13980000}))}
 if($report.error -and (Test-Path "$r\vm\native-desktop-admission-v114")){New-Item -ItemType File "$r\vm\native-desktop-admission-v114\stop" -Force|Out-Null}

 if($cursorStarted){
  $cursorDone=$null;if(Test-Path "$out\cursor\recovery"){New-Item -ItemType File "$out\cursor\recovery\stop" -Force|Out-Null}
  $cursorDeadline=[DateTime]::UtcNow.AddSeconds(15);do{try{$cursorDone=Get-Content "$out\cursor\recovery\report.json" -Raw|ConvertFrom-Json;if($cursorDone.completed){break}}catch{};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow -lt $cursorDeadline)
  if($cursorDone.completed -and $cursorDone.passed -and $cursorDone.original_unconfined_restored){Stop-ScheduledTask -TaskName T8-NpuRender-NativeLiveV104-cursor -ErrorAction SilentlyContinue;Unregister-ScheduledTask -TaskName T8-NpuRender-NativeLiveV104-cursor -Confirm:$false -ErrorAction SilentlyContinue;$report.cursor_cleanup_passed=$true}
  else{$report.cursor_cleanup_passed=$false;$report.passed=$false;$report.cursor_recovery_task_retained=$true}
 }
 if($native -and !$native.HasExited){[void]$native.WaitForExit($(if($report.error){90000}else{14120000}))}
 if(Test-Path "$r\vm\native-topology-v211"){New-Item -ItemType File "$r\vm\native-topology-v211\stop" -Force|Out-Null}
 $observerTask=Get-ScheduledTask -TaskName T8-NpuRender-InspectTopology-V123 -ErrorAction SilentlyContinue
 if($observerTask){
  $expectedObserverArgument='"'+$source+'\native_runner.py" "'+$r+'\vm\native-topology-v211\job.json"'
  if(@($observerTask.Actions).Count -eq 1 -and $observerTask.Actions[0].Execute -eq "$source\venv\Scripts\pythonw.exe" -and $observerTask.Actions[0].Arguments -eq $expectedObserverArgument){
   $observerDeadline=[DateTime]::UtcNow.AddSeconds(15);while((Get-ScheduledTask -TaskName $observerTask.TaskName).State -eq 'Running' -and [DateTime]::UtcNow -lt $observerDeadline){Start-Sleep -Milliseconds 200}
   if((Get-ScheduledTask -TaskName $observerTask.TaskName).State -ne 'Running'){Unregister-ScheduledTask -TaskName $observerTask.TaskName -Confirm:$false;$report.observer_cleanup_passed=$true}else{$report.observer_cleanup_passed=$false;$report.passed=$false}
  }else{$report.observer_cleanup_passed=$false;$report.passed=$false}
 }
 if($native -and $native.HasExited){$native.Refresh();$report.native_exit=$native.ExitCode}
 if($topology -and $topology.HasExited){$topology.Refresh();$report.topology_exit=$topology.ExitCode}
 try{$finalNative=Get-Content "$r\vm\native-desktop-admission-v114\report.json" -Raw|ConvertFrom-Json;$report.cleanup_passed=[bool]$finalNative.cleanup_passed}catch{$report.cleanup_passed=$false;$report.cleanup_read_error=$_.Exception.Message}
 foreach($process in @($relay,$topology,$native)){if($process){$process.Dispose()}}
 $report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report
}
$report|ConvertTo-Json -Depth 6;if(!$report.passed){exit 1}
