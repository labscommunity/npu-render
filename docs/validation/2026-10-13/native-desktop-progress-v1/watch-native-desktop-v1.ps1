param([Parameter(Mandatory=$true)][string]$Workspace,[Parameter(Mandatory=$true)][string]$ExpectedSid,[Parameter(Mandatory=$true)][string]$ManifestSha256)
$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r=$Workspace;$task='T8-NpuRender-NativeDesktopWatch-v1';$out="$r\vm\native-desktop-watch-v1";$probe="$r\builds\native-present-v3\Release\nr_frame_present_probe.exe"
if([Security.Principal.WindowsIdentity]::GetCurrent().User.Value -ne $ExpectedSid){throw 'Watcher must run as the authorized controller user'}
if($r -ne "$env:LOCALAPPDATA\npu-render"){throw 'Watcher profile differs from the qualified workspace'}
if($ManifestSha256 -notmatch '^[a-fA-F0-9]{64}$' -or (Get-FileHash "$r\vm\native-desktop-v1-source.json").Hash -ne $ManifestSha256){throw 'Controller manifest changed'}
if(Test-Path $out){throw 'Watcher evidence already exists'}
New-Item -ItemType Directory $out|Out-Null
& icacls.exe $out /inheritance:r /grant:r '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' > "$out\directory-acl.log";if($LASTEXITCODE){throw 'Watcher evidence protection failed'}
$deadline=[DateTime]::UtcNow.AddHours(12)
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');deadline=$deadline.ToString('o');controller_pid=$PID;controller_identity_verified=$true;completed=$false;probe_launched=$false;admission_launched=$false;status='waiting-for-signed-in-unlocked-desktop';full_desktop_offload=$false}
function Save-Report{$report|ConvertTo-Json -Depth 8|Set-Content "$out\report.tmp";Move-Item "$out\report.tmp" "$out\report.json" -Force}
function Run-Trial([string]$file,[string[]]$arguments,[string]$label,[int]$seconds){
 $command=@('-NoProfile','-NonInteractive','-ExecutionPolicy','RemoteSigned','-File',$file)+$arguments
 $quoted=@($command|ForEach-Object {'"'+$_.Replace('"','\"')+'"'})
 $child=Start-Process "$env:windir\System32\WindowsPowerShell\v1.0\powershell.exe" -ArgumentList $quoted -PassThru -NoNewWindow -RedirectStandardOutput "$out\$label.stdout.log" -RedirectStandardError "$out\$label.stderr.log"
 [void]$child.Handle
 try{if(!$child.WaitForExit($seconds*1000)){$child.Kill();$child.WaitForExit();throw "Owned $label controller timed out"};$child.Refresh();return $child.ExitCode}finally{$child.Dispose()}
}
Save-Report
try{
 $proof=Get-Content "$r\builds\native-present-v3\build-report.json" -Raw|ConvertFrom-Json
 if(!$proof.clean_build -or (Get-FileHash $probe).Hash -ne $proof.binary_sha256){throw 'Session probe identity changed'}
 while([DateTime]::UtcNow -lt $deadline){
  if((Get-FileHash "$r\vm\native-desktop-v1-source.json").Hash -ne $ManifestSha256){throw 'Controller manifest changed during wait'}
  $source=Get-Content "$r\vm\native-desktop-v1-source.json" -Raw|ConvertFrom-Json
  foreach($f in $source.psobject.Properties){if((Get-FileHash "$r\vm\$($f.Name)").Hash -ne $f.Value){throw 'Controller source changed during wait'}}
  $gate=(& $probe --inspect-session)|ConvertFrom-Json;if($LASTEXITCODE){throw 'Session inspection failed'}
  $busy=@(Get-Process nr_registered*,nr_native_service_probe,nr_native_local_session,nr_frame_present_probe -ErrorAction SilentlyContinue).Count -gt 0
  $report.last_checked=[DateTime]::UtcNow.ToString('o');$report.session_gate=$gate;$report.experiments_busy=$busy;Save-Report
  if($gate.ready_for_interactive_task -and !$busy){
   if(!(Test-Path "$r\vm\native-present-v3-trial")){
    $report.probe_launched=$true;$report.status='running-finite-presentation-probe';Save-Report
    $report.presentation_probe_exit=Run-Trial "$r\vm\run-native-present-v3.ps1" @() 'present' 100;Save-Report
   }
   if(Test-Path "$r\vm\native-desktop-admission-v1"){throw 'A native admission trial already exists'}
   $gate=(& $probe --inspect-session)|ConvertFrom-Json;if($LASTEXITCODE -or !$gate.ready_for_interactive_task){throw 'Console is no longer ready after presentation probe'}
   $report.admission_launched=$true;$report.status='running-bounded-native-admission';Save-Report
   $report.admission_exit=Run-Trial "$r\vm\run-native-desktop-v1.ps1" @('-Mode','admit') 'admission' 430
   if(Test-Path "$r\vm\native-desktop-admission-v1\report.json"){
    $trial=Get-Content "$r\vm\native-desktop-admission-v1\report.json" -Raw|ConvertFrom-Json
    $report.native_trial_completed=$trial.completed;$report.native_trial_passed=$trial.passed;$report.cleanup_passed=$trial.cleanup_passed
   }
   $report.status='bounded-trials-finished';break
  }
  Start-Sleep -Seconds 10
 }
 if(!$report.admission_launched){$report.status='expired-without-ready-desktop'}
}catch{$report.error=$_.Exception.Message;$report.status='stopped-with-error'}
finally{$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report;Unregister-ScheduledTask -TaskName $task -Confirm:$false -ErrorAction SilentlyContinue}
