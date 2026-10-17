$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\physical-gpu-choice-v1-qualification"
if(Test-Path $out){throw 'Coordinator exists'};New-Item -ItemType Directory $out|Out-Null
. ([scriptblock]::Create([IO.File]::ReadAllText("$vm\atomic_lab_report_v1.ps1")))
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;desktop_changed=$false;npu_tested=$false;tahoma_tested=$false;active_phase='waiting-for-repeated-desktop';cases=@()}
function Save-Report {Write-NrLabReport "$out\report.json" $report};Save-Report
try{
 $deadline=[DateTime]::UtcNow.AddMinutes(65)
 while($true){$q=Get-Content "$vm\ptl-combined-desktop-v7-v2\report.json" -Raw|ConvertFrom-Json;if($q.completed){break};if([DateTime]::UtcNow -gt $deadline){throw 'Repeated desktop completion deadline'};Start-Sleep -Seconds 3}
 $desktop=if(Test-Path "$vm\native-desktop-admission-v65\report.json"){'native-desktop-admission-v65'}else{'native-desktop-admission-v64'}
 $q=Get-Content "$vm\$desktop\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.cleanup_passed -or !$q.memory_integrity_after){throw 'Desktop cleanup incomplete'}
 if(@(Get-Process nr_registered*,nr_native*,nr_ptl*,nr_frame_present_probe,nr_repeated_input,nr_output_observer -ErrorAction SilentlyContinue).Count){throw 'Graphics experiment active'}
 $report.active_phase='build';Save-Report
 & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$vm\build-physical-gpu-choice-v1.ps1" > "$out\build.stdout.log" 2> "$out\build.stderr.log";if($LASTEXITCODE){throw 'GPU selection probe build failed'}
 $exe="$r\builds\physical-gpu-choice-v1\Release\nr_physical_gpu_probe.exe";$proof=Get-Content "$r\builds\physical-gpu-choice-v1\build-report.json" -Raw|ConvertFrom-Json
 if(!$proof.clean_build -or (Get-FileHash $exe).Hash -ne $proof.binary_sha256){throw 'GPU probe identity mismatch'};$report.binary_sha256=$proof.binary_sha256
 foreach($case in @(@{name='contract';args=@('--contract');expected=0},@{name='inspect';args=@('--inspect','0x8086','0xb080');expected=0},@{name='wrong-device';args=@('--inspect','0x8086','0xffff');expected=1},@{name='clear';args=@('--clear','0x8086','0xb080');expected=0})){
  $report.active_phase=$case.name;Save-Report
  $child=Start-Process $exe -ArgumentList $case.args -PassThru -NoNewWindow -RedirectStandardOutput "$out\$($case.name).stdout.jsonl" -RedirectStandardError "$out\$($case.name).stderr.log"
  try{if(!$child.WaitForExit(30000)){$child.Kill();$child.WaitForExit();throw 'Owned GPU probe deadline'};$child.Refresh();if($child.ExitCode -ne $case.expected){throw "GPU control case failed: $($case.name)"}}finally{$child.Dispose()}
  if($case.expected -eq 0){$result=Get-Content "$out\$($case.name).stdout.jsonl" -Raw|ConvertFrom-Json;if(!$result.completed -or !$result.passed){throw 'GPU control case evidence incomplete'}
   if($case.name -eq 'contract' -and $result.selection_contract_checks -ne 11){throw 'Selection contract count differs'}
   if($case.name -eq 'clear' -and ($result.exact_clear_images -ne 4 -or $result.vendor -ne 32902 -or $result.device -ne 45184 -or $result.npu_commands -ne 0 -or $result.desktop_changed)){throw 'Explicit hardware GPU clear evidence incomplete'}
  }else{$result=@{expected_rejection=$true}}
  $report.cases+=@{case=$case.name;passed=$true;result=$result};Save-Report
 }
 $report.passed=$true
}catch{$report.error=$_.Exception.Message}
finally{$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out\report.json" -Raw;if(!$report.passed){exit 1}
