$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$out="$r\vm\native-saver-dismiss-v33"
if(Test-Path $out){throw 'Dismissal evidence exists'};New-Item -ItemType Directory $out|Out-Null
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;settings_changed=$false;driver_changed=$false}
try{
 $prior=Get-Content "$r\vm\native-topology-v112\report.json" -Raw|ConvertFrom-Json
 if($prior.screen_saver_settings.secure -ne $false -or $prior.after_screen_saver_close.input_name -ne 'Screen-saver' -or !($prior.screen_saver_close_requests|Where-Object posted)){throw 'Ordinary dismissal prerequisites absent'}
 if(([DateTime]::UtcNow-([DateTimeOffset]::Parse($prior.finished)).UtcDateTime).TotalMinutes -gt 10){throw 'Screen-saver evidence stale'}
 $expected="$env:windir\ASUS\ScreenSaver\OLED Care Screensaver.scr"
 $matches=@(Get-CimInstance Win32_Process | Where-Object {$_.ExecutablePath -eq $expected -and $_.SessionId -eq $prior.after_screen_saver_close.session -and $_.CommandLine -eq ('"'+$expected+'" /s')})
 if($matches.Count -ne 1){throw 'Screen-saver identity ambiguous'}
 $candidate=$matches[0];$p=[Diagnostics.Process]::GetProcessById($candidate.ProcessId);[void]$p.Handle
 try{
  if($p.MainModule.FileName -ne $expected -or $p.SessionId -ne $candidate.SessionId -or [Math]::Abs(($p.StartTime.ToUniversalTime()-$candidate.CreationDate.ToUniversalTime()).TotalSeconds) -gt 1){throw 'Screen-saver identity changed'}
  $report.identity=@{pid=$p.Id;created=$p.StartTime.ToUniversalTime().ToString('o');session=$p.SessionId;file_sha256=(Get-FileHash $expected).Hash;image='Windows/ASUS/ScreenSaver/OLED Care Screensaver.scr'}
  $p.Kill();$report.terminated=$p.WaitForExit(5000);$report.passed=$report.terminated
 }finally{$p.Dispose()}
}catch{$report.error=$_.Exception.Message}
finally{$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');$report|ConvertTo-Json -Depth 5|Set-Content "$out\report.json"}
Get-Content "$out\report.json";if(!$report.passed){exit 1}
