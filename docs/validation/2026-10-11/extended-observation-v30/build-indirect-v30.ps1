$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$src='C:\Users\tatef\AppData\Local\Temp\npu-graphics-probe-0146a2388f25'
$comparison=Get-Content "$r\vm\compiler-batch-comparison-v2\report.json" -Raw|ConvertFrom-Json;if(!$comparison.completed){throw 'Compiler matrix must finish first'}
$b="$r\builds\indirect-lab-v30";if(Test-Path $b){throw 'Build evidence exists'}
Set-Location $src;$ErrorActionPreference='Continue'
& "$src\venv\Scripts\python.exe" tools\build_indirect_lab.py --build-dir $b --wdk-root "$r\dependencies\wdk-umdf-26100.6584" *> "$r\vm\build-indirect-v30.log"
$code=$LASTEXITCODE;Get-Content "$r\vm\build-indirect-v30.log" -Tail 18|Where-Object{$_ -notmatch '^\{"schema"'};exit $code
