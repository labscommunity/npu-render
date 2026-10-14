$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$out="$r\vm\native-extended-v2";$native=$null
if(Test-Path $out){throw 'Extended trial evidence exists'};New-Item -ItemType Directory $out|Out-Null
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;native_trial='native-desktop-admission-v11';topology_trial='native-topology-extension-v3';desktop_offloaded=$false}
try{
 & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy RemoteSigned -File "$r\vm\start-native-topology-v10.ps1" > "$out\observer-start.log"
 if($LASTEXITCODE){throw 'Topology observer failed'}
 $native=Start-Process "$env:windir\System32\WindowsPowerShell\v1.0\powershell.exe" -ArgumentList @('-NoProfile','-NonInteractive','-ExecutionPolicy','RemoteSigned','-File',"$r\vm\run-native-desktop-v11.ps1",'-Mode','admit') -PassThru -NoNewWindow -RedirectStandardOutput "$out\native.stdout.log" -RedirectStandardError "$out\native.stderr.log";[void]$native.Handle;$report.controller_pid=$native.Id
 $deadline=[DateTime]::UtcNow.AddSeconds(75);$ready=$false
 while([DateTime]::UtcNow -lt $deadline){
  if($native.HasExited){throw 'Native admission exited before topology control'}
  if((Test-Path "$r\vm\native-desktop-admission-v11\bootstrap\report.json") -and (Test-Path "$r\vm\native-topology-v10\samples.jsonl")){
   try{$trial=Get-Content "$r\vm\native-desktop-admission-v11\bootstrap\report.json" -Raw|ConvertFrom-Json;$sample=Get-Content "$r\vm\native-topology-v10\samples.jsonl" -Tail 1|ConvertFrom-Json;$ready=$trial.admission_published -and !$trial.completed -and @($sample.entries|Where-Object active_path).Count -eq 2}catch{}
  };if($ready){break};Start-Sleep -Milliseconds 250
 }
 if(!$ready){throw 'Native indirect topology did not become active'}
 if(!(Get-ScheduledTask -TaskName T8-NpuRender-NativeDesktopRecovery-v11 -ErrorAction SilentlyContinue)){throw 'Independent recovery not armed'}
 & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy RemoteSigned -File "$r\vm\run-native-topology-helper-v3.ps1" -Mode extend -Trial native-topology-extension-v3 -Seconds 90 > "$out\topology.stdout.log"
 $report.topology_exit=$LASTEXITCODE
 if(!$native.WaitForExit(450000)){throw 'Native controller exceeded observation window'};$native.Refresh();$report.native_exit=$native.ExitCode
 $trial=Get-Content "$r\vm\native-desktop-admission-v11\report.json" -Raw|ConvertFrom-Json;$bootstrap=Get-Content "$r\vm\native-desktop-admission-v11\bootstrap\report.json" -Raw|ConvertFrom-Json
 $report.passed=($report.topology_exit -eq 0 -and $report.native_exit -eq 0 -and $trial.passed -and $trial.cleanup_passed -and $bootstrap.passed -and !$bootstrap.error)
 $report.cleanup_passed=$trial.cleanup_passed;$report.native_frame_produced=$bootstrap.native_indirect_frame_produced;$report.observed_frames=$bootstrap.observed_frame_count
}catch{$report.error=$_.Exception.Message}
finally{
 if($native -and !$native.HasExited){[void]$native.WaitForExit(60000)}
 if(Test-Path "$r\vm\native-topology-v10"){New-Item -ItemType File "$r\vm\native-topology-v10\stop" -Force|Out-Null}
 $report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');$report|ConvertTo-Json -Depth 5|Set-Content "$out\report.json";if($native){$native.Dispose()}
}
$report|ConvertTo-Json -Depth 5;if(!$report.passed){exit 1}
