$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$src="$r\vm\model-reuse-diagnostic-v2";$build="$r\builds\model-reuse-diagnostic-v2"
$stream=Get-Content "$r\vm\indexed-dwm-stream-v107\report.json" -Raw|ConvertFrom-Json
if(!$stream.completed -or !$stream.configuration_removed){throw 'Desktop trial must finish before building'}
if((Test-Path $src) -or (Test-Path $build)){throw 'Diagnostic source or build exists'}
Expand-Archive "$src.zip" $src
foreach($entry in (Get-Content "$src\source-sha256.json" -Raw|ConvertFrom-Json)){
 $file=Join-Path $src $entry.path
 if((Get-Item $file).Length -ne $entry.bytes -or (Get-FileHash $file).Hash -ne $entry.sha256){throw 'Diagnostic source mismatch'}
}
Set-Location $src;$python="$r\toolchains\openvino-2026.4.1\Scripts\python.exe"
$ErrorActionPreference='Continue'
& $python tools\build_registered_lab.py --build-dir $build --wdk-include-root "$r\dependencies\wdk-26100.6584-headers" *> "$r\vm\build-model-reuse-diagnostic-v2.log"
$code=$LASTEXITCODE;$ErrorActionPreference='Stop'
Get-Content "$r\vm\build-model-reuse-diagnostic-v2.log" -Tail 28|Where-Object{$_ -notmatch '^\{"schema"'}
if($code){throw 'Diagnostic build failed'}
'Isolated model reuse diagnostic built and CPU contracts passed; production source unchanged'
