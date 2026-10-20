$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\ptl-textured-batch-comparison-v2-qualification"
if(Test-Path $out){throw 'Diagnostic coordinator exists'};New-Item -ItemType Directory $out|Out-Null
. ([scriptblock]::Create([IO.File]::ReadAllText("$vm\atomic_lab_report_v1.ps1")))
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;desktop_changed=$false;command_batch_component_only=$true;tahoma_tested=$false;active_phase='verify-graphics-cleanup';steps=@()}
function Save-Report{Write-NrLabReport "$out/report.json" $report};Save-Report
function Invoke-Step($name,$exe,$arguments,$seconds){
 $report.active_phase=$name;Save-Report
 $p=[Diagnostics.Process]::new();$p.StartInfo.FileName=$exe;$p.StartInfo.Arguments=$arguments;$p.StartInfo.UseShellExecute=$false;$p.StartInfo.CreateNoWindow=$true;$p.StartInfo.RedirectStandardOutput=$true;$p.StartInfo.RedirectStandardError=$true
 try{if(!$p.Start()){throw 'Process did not start'};$stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync();if(!$p.WaitForExit($seconds*1000)){$p.Kill();$p.WaitForExit();throw "$name deadline"};$code=$p.ExitCode;[IO.File]::WriteAllText("$out/$name.stdout.log",$stdout.Result);[IO.File]::WriteAllText("$out/$name.stderr.log",$stderr.Result);$report.steps+=@{name=$name;exit_code=$code};Save-Report;if($code -ne 0){throw "$name failed with exit $code"}}finally{$p.Dispose()}
}
try{
 $report.active_phase='waiting-for-inventory-and-contract';Save-Report
 $deadline=[DateTime]::UtcNow.AddMinutes(90)
 foreach($name in @('ptl-textured-batch-v2-qualification')){
  while($true){$q=Get-Content "$vm/$name/report.json" -Raw|ConvertFrom-Json;if($q.completed){break};if([DateTime]::UtcNow -gt $deadline){throw 'Preceding experiment deadline'};Start-Sleep -Seconds 3}
 }
 if(!$q.passed){throw 'Prior renderer failed; inspect before further NPU work'}
 $preceding=if(Test-Path "$vm/native-desktop-admission-v74/report.json"){'native-desktop-admission-v74'}elseif(Test-Path "$vm/native-desktop-admission-v67/report.json"){'native-desktop-admission-v67'}else{'physical-gpu-desktop-v2'}
 $q=Get-Content "$vm/$preceding/report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.cleanup_passed -or !$q.memory_integrity_after){throw 'Preceding desktop cleanup incomplete'}
 if(@(Get-Process nr_registered*,nr_native*,nr_ptl*,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Another graphics experiment is active'}

 $sources=Get-Content "$vm/ptl-textured-batch-comparison-v2-source.json" -Raw|ConvertFrom-Json
 foreach($f in $sources.psobject.Properties){if((Get-FileHash "$vm/$($f.Name)").Hash.ToLower() -ne $f.Value){throw 'Source identity mismatch'}}
 $python="$r/toolchains/openvino-2026.4.1/Scripts/python.exe";$env:PYTHONPATH="$vm/ptl-reusable-harness-v1/src"
 Invoke-Step 'comparison' $python ('"'+$vm+'\compare-ptl-textured-batch-v2.py"') 360
 $q=Get-Content "$vm/ptl-textured-batch-comparison-v2/report.json" -Raw|ConvertFrom-Json
 if(!$q.completed -or !$q.passed -or @($q.child_result.cases).Count -ne 3){throw 'Comparison incomplete'}
 $report.exact_complete_images=42
 $report.passed=$true
}catch{$report.error=$_.Exception.Message}
finally{$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out/report.json" -Raw;if(!$report.passed){exit 1}
