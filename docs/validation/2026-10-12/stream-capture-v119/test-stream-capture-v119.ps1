$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render\vm\stream-capture-v119-test"
if(Test-Path $r){throw 'Evidence exists'};New-Item -ItemType Directory $r|Out-Null
$results=@()
foreach($mode in 'normal','terminated'){
 $code="[Console]::Out.WriteLine('stdout-$mode');[Console]::Out.Flush();[Console]::Error.WriteLine('stderr-$mode');[Console]::Error.Flush();Start-Sleep -Seconds 10"
 $p=[Diagnostics.Process]::new();$p.StartInfo.FileName="$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe";$p.StartInfo.Arguments='-NoProfile -NonInteractive -EncodedCommand '+[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($code));$p.StartInfo.UseShellExecute=$false;$p.StartInfo.RedirectStandardOutput=$true;$p.StartInfo.RedirectStandardError=$true
 $of=[IO.FileStream]::new("$r\$mode.stdout.log",[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::ReadWrite,1,$true);$ef=[IO.FileStream]::new("$r\$mode.stderr.log",[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::ReadWrite,1,$true)
 try{
  [void]$p.Start();$ot=$p.StandardOutput.BaseStream.CopyToAsync($of,4096);$et=$p.StandardError.BaseStream.CopyToAsync($ef,4096)
  $watch=[Diagnostics.Stopwatch]::StartNew();$live=$false
  while($watch.ElapsedMilliseconds -lt 5000){
   if((Get-Content "$r\$mode.stdout.log" -Raw) -match "stdout-$mode" -and (Get-Content "$r\$mode.stderr.log" -Raw) -match "stderr-$mode"){$live=!$p.HasExited;break};Start-Sleep -Milliseconds 100
  }
  if(!$live){throw 'Output not visible before child exit'}
  if($mode -eq 'terminated'){$p.Kill()}
  if(!$p.WaitForExit(15000) -or !$ot.Wait(2000) -or !$et.Wait(2000)){throw 'Bounded completion failed'}
  $results+=@{mode=$mode;output_visible_while_child_alive=$live;output_complete=$true;exit_code=$p.ExitCode}
 }finally{$of.Dispose();$ef.Dispose();$p.Dispose()}
}
$tokens=$null;$errors=$null;[void][Management.Automation.Language.Parser]::ParseFile("$env:LOCALAPPDATA\npu-render\vm\stream-controller-v119.ps1",[ref]$tokens,[ref]$errors)
if($errors.Count){throw ($errors|Out-String)}
@{all_checks_passed=$true;checks=$results;device_loss_simulated=$false}|ConvertTo-Json -Depth 5|Tee-Object "$r\report.json"
