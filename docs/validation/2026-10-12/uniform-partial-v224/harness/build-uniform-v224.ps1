try {
$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$src='C:\Users\tatef\AppData\Local\Temp\npu-graphics-probe-0146a2388f25'
$stream=Get-Content "$r\vm\indexed-dwm-stream-v115\report.json" -Raw|ConvertFrom-Json;if(!$stream.completed -or !$stream.configuration_removed){throw 'Live stream active'}
Set-Location $src
foreach($variant in 'candidate','legacy'){
 $tag=if($variant -eq 'candidate'){'v224'}else{'v224-legacy'};$b="$r\builds\present-umd-$tag";if(Test-Path $b){throw 'Build evidence exists'}
 $python=if($variant -eq 'candidate'){"$r\toolchains\openvino-2026.4.1\Scripts\python.exe"}else{"$src\venv\Scripts\python.exe"}
 $version=& $python -c 'import openvino; print(openvino.get_version())';if($LASTEXITCODE -ne 0){throw 'Runtime version unavailable'}
 "Building $tag using $version";$ErrorActionPreference='Continue'
 & $python tools\build_registered_lab.py --build-dir $b --wdk-include-root "$r\dependencies\wdk-26100.6584-headers" *> "$r\vm\build-present-$tag.log"
 $code=$LASTEXITCODE;$ErrorActionPreference='Stop';Get-Content "$r\vm\build-present-$tag.log" -Tail 28|Where-Object{$_ -notmatch '^\{"schema"'}
 if($code -ne 0){throw "Clean build failed: $tag"}
 $sdk=& $python -c 'import pathlib,openvino; print(pathlib.Path(openvino.__file__).parent)'
 [ordered]@{runtime_version=$version;runtime_directory="$sdk\libs";python=$python;runtime=@(Get-ChildItem "$sdk\libs" -Filter '*.dll'|ForEach-Object {[ordered]@{name=$_.Name;sha256=(Get-FileHash $_.FullName).Hash;bytes=$_.Length;version=$_.VersionInfo.FileVersion}})}|ConvertTo-Json -Depth 5|Set-Content "$b\runtime-build-provenance.json"
}

} catch { [IO.File]::WriteAllText("$env:LOCALAPPDATA\npu-render\vm\build-v224-failed.txt",[string]$_); throw }
