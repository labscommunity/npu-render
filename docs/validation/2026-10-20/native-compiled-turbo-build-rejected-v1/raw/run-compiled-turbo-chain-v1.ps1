$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\compiled-turbo-chain-v1"
if(Test-Path $out){throw 'Chain already exists'};New-Item -ItemType Directory $out|Out-Null
. ([scriptblock]::Create([IO.File]::ReadAllText("$vm\atomic_lab_report_v1.ps1")))
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;desktop_changed=$false;active_phase='qualification';steps=@()}
function Save-Report{Write-NrLabReport "$out/report.json" $report};Save-Report
try{
 foreach($name in @('ptl-worker-v30-qualification','ptl-worker-v30-recovery-qualification')){
  $report.active_phase=$name;Save-Report
  $p=[Diagnostics.Process]::new();$p.StartInfo.FileName='powershell.exe';$p.StartInfo.Arguments='-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "'+$vm+'\run-'+$name+'.ps1"';$p.StartInfo.UseShellExecute=$false;$p.StartInfo.CreateNoWindow=$true;$p.StartInfo.RedirectStandardOutput=$true;$p.StartInfo.RedirectStandardError=$true
  try{
   if(!$p.Start()){throw 'Could not start coordinator'}
   $stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync()
   if(!$p.WaitForExit(10800000)){$p.Kill();$p.WaitForExit();throw 'Coordinator deadline'}
   $code=$p.ExitCode;[IO.File]::WriteAllText("$out/$name.stdout.log",$stdout.Result);[IO.File]::WriteAllText("$out/$name.stderr.log",$stderr.Result)
   $report.steps+=@{name=$name;exit_code=$code};Save-Report
   if($code -ne 0){throw "$name failed with exit $code"}
   $q=Get-Content "$vm/$name/report.json" -Raw|ConvertFrom-Json
   if(!$q.completed -or !$q.passed){throw 'Coordinator result incomplete'}
  }finally{$p.Dispose()}
 }
 $report.passed=$true
}catch{$report.error=$_.Exception.Message}
finally{$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
if(!$report.passed){exit 1}
