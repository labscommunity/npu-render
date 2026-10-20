$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\combined-desktop-ready-v1"
if(Test-Path $out){throw 'Readiness coordinator already exists'}
New-Item -ItemType Directory $out|Out-Null
. ([scriptblock]::Create([IO.File]::ReadAllText("$vm/atomic_lab_report_v1.ps1")))
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;desktop_trial_started=$false;active_phase='waiting-for-serialized-hardware-tests';full_desktop_offload=$false;tahoma_tested=$false}
function Save-Report{Write-NrLabReport "$out/report.json" $report};Save-Report
function Verify-Sources{
 $sources=Get-Content "$vm/ptl-combined-fill-turbo-live-v1-source.json" -Raw|ConvertFrom-Json
 foreach($f in $sources.psobject.Properties){if((Get-FileHash "$vm/$($f.Name)").Hash.ToLower() -ne $f.Value){throw 'Prepared desktop source changed'}}
}
try{
 Verify-Sources
 $deadline=[DateTime]::UtcNow.AddHours(2)
 while($true){
  if(Test-Path "$out/stop"){throw 'Stopped before desktop trial'}
  if([DateTime]::UtcNow -gt $deadline){throw 'Readiness deadline; no desktop trial started'}
  $q=$null;if(Test-Path "$vm/ptl-textured-windows-v2-qualification/report.json"){$q=Get-Content "$vm/ptl-textured-windows-v2-qualification/report.json" -Raw|ConvertFrom-Json}
  if($q -and $q.completed){if(!$q.passed){throw 'Inspect preceding hardware failure before desktop work'};break}
  Start-Sleep -Seconds 5
 }
 $q=Get-Content "$vm/combined-fill-turbo-comparison-v1/report.json" -Raw|ConvertFrom-Json
 if(!$q.completed -or !$q.passed -or $q.summary.new_parent_ms -ge .95*$q.summary.old_parent_ms){throw 'Desktop performance gate failed'}
 $q=Get-Content "$vm/combined-fill-turbo-provider-recovery-v1/report.json" -Raw|ConvertFrom-Json
 if(!$q.completed -or !$q.passed -or @($q.phases).Count -ne 2){throw 'Provider recovery gate failed'}
 $report.active_phase='waiting-for-interactive-sign-in';Save-Report
 while($true){
  if(Test-Path "$out/stop"){throw 'Stopped before desktop trial'}
  if([DateTime]::UtcNow -gt $deadline){throw 'Readiness deadline; no desktop trial started'}
  $shells=@(Get-Process explorer -ErrorAction SilentlyContinue|Where-Object {$_.SessionId -gt 0})
  $locked=@(Get-Process LogonUI -ErrorAction SilentlyContinue)
  $ready=@($shells|Where-Object {$s=$_;!@($locked|Where-Object {$_.SessionId -eq $s.SessionId}).Count})
  $report.last_observed_utc=[DateTime]::UtcNow.ToString('o');$report.interactive_shells=$shells.Count;Save-Report
  if($ready.Count){break}
  Start-Sleep -Seconds 5
 }
 # Shell/process detection only permits the existing stronger interactive
 # preflight to run. Its input-desktop/topology checks precede driver changes.
 Verify-Sources
 if(@(Get-Process nr_registered*,nr_native*,nr_ptl*,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Another graphics experiment is active'}
 $report.active_phase='physical-desktop-controller';$report.desktop_trial_started=$true;Save-Report
 $p=[Diagnostics.Process]::new()
 $p.StartInfo.FileName='powershell.exe'
 $p.StartInfo.Arguments='-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "'+$vm+'\run-ptl-combined-desktop-v17.ps1"'
 $p.StartInfo.UseShellExecute=$false;$p.StartInfo.CreateNoWindow=$true
 $p.StartInfo.RedirectStandardOutput=$true;$p.StartInfo.RedirectStandardError=$true
 try{
  if(!$p.Start()){throw 'Desktop controller did not start'}
  $stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync()
  if(!$p.WaitForExit(2400000)){$p.Kill();$p.WaitForExit();throw 'Desktop controller deadline; inspect its independent recovery report'}
  [IO.File]::WriteAllText("$out/controller.stdout.log",$stdout.Result);[IO.File]::WriteAllText("$out/controller.stderr.log",$stderr.Result)
  $report.controller_exit_code=$p.ExitCode
  $q=Get-Content "$vm/ptl-combined-desktop-v17/report.json" -Raw|ConvertFrom-Json
  if($p.ExitCode -ne 0 -or !$q.completed -or !$q.passed){throw 'Desktop controller did not pass'}
  $report.passed=$true
 }finally{$p.Dispose()}
}catch{$report.error=$_.Exception.Message}
finally{$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
if(!$report.passed){exit 1}
