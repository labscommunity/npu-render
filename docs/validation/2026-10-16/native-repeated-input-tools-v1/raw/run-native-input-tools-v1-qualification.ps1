$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\native-input-tools-v1-qualification"
if(Test-Path $out){throw 'Coordinator exists'};New-Item -ItemType Directory $out|Out-Null
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;desktop_changed=$false;npu_tested=$false;active_phase='waiting-for-provider-tests';phases=@()}
function Save-Report{$report|ConvertTo-Json -Depth 5|Set-Content "$out\report.json"};Save-Report
try{
 $deadline=[DateTime]::UtcNow.AddMinutes(45)
 while($true){$q=Get-Content "$vm\packet-provider-recovery-v2-qualification\report.json" -Raw|ConvertFrom-Json;if($q.completed){break};if([DateTime]::UtcNow -gt $deadline){throw 'Earlier tests deadline'};Start-Sleep -Seconds 3}
 $desktop=if(Test-Path "$vm\native-desktop-admission-v64\report.json"){'native-desktop-admission-v64'}else{'native-desktop-admission-v61'}
 $q=Get-Content "$vm\$desktop\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.cleanup_passed -or !$q.memory_integrity_after){throw 'Desktop cleanup incomplete'}
 if(@(Get-Process nr_registered*,nr_native*,nr_ptl*,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Graphics experiment active'}
 foreach($name in @('native-input-benchmark-v1','native-output-observer-v5')){
  foreach($action in @('build','qualify')){
   $report.active_phase="$action-$name";Save-Report
   & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$vm\$action-$name.ps1" > "$out\$action-$name.stdout.log" 2> "$out\$action-$name.stderr.log"
   if($LASTEXITCODE){throw "$action-$name failed"}
   $report.phases+=@{phase="$action-$name";passed=$true};Save-Report
  }
 }
 $report.passed=$true
}catch{$report.error=$_.Exception.Message}
finally{$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out\report.json" -Raw;if(!$report.passed){exit 1}
