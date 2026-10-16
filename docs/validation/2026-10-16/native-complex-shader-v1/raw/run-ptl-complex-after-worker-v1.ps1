$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$out="$vm\ptl-complex-after-worker-v1"
if(Test-Path $out){throw 'Coordinator exists'};New-Item -ItemType Directory $out|Out-Null
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');completed=$false;passed=$false;desktop_changed=$false;tahoma_tested=$false;active_phase='waiting-for-worker'}
function Save-Report{$report|ConvertTo-Json -Depth 5|Set-Content "$out\report.json"};Save-Report
try{
 $deadline=[DateTime]::UtcNow.AddMinutes(35)
 while($true){$q=Get-Content "$vm\ptl-worker-v11-qualification-v2\report.json" -Raw|ConvertFrom-Json;if($q.completed){break};if([DateTime]::UtcNow -ge $deadline){throw 'Worker wait exceeded'};Start-Sleep -Seconds 2}
 if(!$q.passed){throw 'Worker qualification failed'}
 if(@(Get-Process nr_registered*,nr_native*,nr_ptl*,nr_frame_present_probe -ErrorAction SilentlyContinue).Count){throw 'Another graphics experiment active'}
 $report.active_phase='image-comparisons';Save-Report
 & "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" "$vm\run-ptl-complex-shader-capture-v2.py" --model-sha256 47f69b5861732a03014b58160116ebbb928fd6e818a11b7f553e423a4cf3c43a --executable-sha256 44add80f829c52577bb113cbc03d03facedc3223196566d3efbbb20e35607d38 > "$out\comparison.stdout.log" 2> "$out\comparison.stderr.log"
 if($LASTEXITCODE){throw 'Complete image comparison failed'}
 $q=Get-Content "$vm\ptl-complex-shader-capture-v2\report.json" -Raw|ConvertFrom-Json;if(!$q.completed -or !$q.passed){throw 'Comparison proof incomplete'}
 $report.passed=$true;$report.cases=@($q.cases).Count
}catch{$report.error=$_.Exception.Message}
finally{$report.completed=$true;$report.finished=[DateTime]::UtcNow.ToString('o');Save-Report}
Get-Content "$out\report.json" -Raw;if(!$report.passed){exit 1}
