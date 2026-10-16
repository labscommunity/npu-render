$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm";$py="$r\toolchains\openvino-2026.4.1\Scripts\python.exe"
& $py "$vm\run-ptl-program-compare-v4.py" selected *> "$vm\run-ptl-program-comparison-v4-selected.log"
if($LASTEXITCODE){Get-Content "$vm\run-ptl-program-comparison-v4-selected.log" -Tail 2;throw 'Selected comparison failed'}
Get-Content "$vm\ptl-program-comparison-v4-selected\report.json" -Raw | ConvertFrom-Json | Select-Object completed,passed,planned_cases,independent_reference_cases,original_graph_different_cases | ConvertTo-Json
& $py "$vm\run-ptl-program-compare-v4.py" full *> "$vm\run-ptl-program-comparison-v4-full.log"
if($LASTEXITCODE){Get-Content "$vm\run-ptl-program-comparison-v4-full.log" -Tail 2;throw 'Full comparison failed'}
Get-Content "$vm\ptl-program-comparison-v4-full\report.json" -Raw | ConvertFrom-Json | Select-Object completed,passed,planned_cases,independent_reference_cases,original_graph_different_cases | ConvertTo-Json
