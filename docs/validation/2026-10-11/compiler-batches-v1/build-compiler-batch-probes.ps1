$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$src='C:\Users\tatef\AppData\Local\Temp\npu-graphics-probe-0146a2388f25';$probe="$r\vm\compiler-batch-probe"
$stream=Get-Content "$r\vm\indexed-dwm-stream-v105\report.json" -Raw|ConvertFrom-Json
if(!$stream.completed -or !$stream.configuration_removed){throw 'Live NPU stream must finish first'}
$manifest=Get-Content "$r\builds\present-umd-v203\registered-build-report.json" -Raw|ConvertFrom-Json
foreach($f in $manifest.source_sha256.psobject.Properties){if((Get-FileHash "$src\$($f.Name)").Hash -ne $f.Value){throw 'Qualified source mismatch'}}
foreach($variant in '2026.1','2026.4'){
 $python=if($variant -eq '2026.1'){"$src\venv\Scripts\python.exe"}else{"$r\toolchains\openvino-2026.4.1\Scripts\python.exe"}
 $sdk=& $python -c 'import pathlib,openvino; print(pathlib.Path(openvino.__file__).parent)';if($LASTEXITCODE -ne 0){throw 'Cannot locate runtime'}
 $build="$r\builds\compiler-batch-$variant-v1";if(Test-Path $build){throw 'Compiler evidence exists'}
 $log="$r\vm\build-compiler-batch-$variant-v1.log";$ErrorActionPreference='Continue'
 & cmake -S $probe -B $build -A x64 "-DOpenVINO_DIR=$sdk\cmake" "-DNR_RENDER_SOURCE=$src\native\windows\registered_lab" *> $log
 $configured=$LASTEXITCODE
 if($configured -eq 0){& cmake --build $build --config Release --clean-first *>> $log;$built=$LASTEXITCODE}else{$built=$configured}
 $ErrorActionPreference='Stop';Get-Content $log -Tail 8
 if($built -ne 0){throw "Isolated probe build failed: $variant"}
 $sources=[ordered]@{};foreach($file in 'compositor_batch_probe.cpp','CMakeLists.txt'){$sources[$file]=(Get-FileHash "$probe\$file").Hash}
 $libraries=@(Get-ChildItem "$sdk\libs" -Filter '*.dll'|ForEach-Object{[ordered]@{name=$_.Name;bytes=$_.Length;sha256=(Get-FileHash $_.FullName).Hash;version=$_.VersionInfo.FileVersion}})
 [ordered]@{variant=$variant;python=$python;runtime_directory="$sdk\libs";source_commit='aca204c842fbda0b440236366df66b21c6dd788c';qualified_source_hashes=$manifest.source_sha256;probe_sources=$sources;binary_sha256=(Get-FileHash "$build\Release\nr_compositor_batch_probe.exe").Hash;runtime=$libraries;npu_tested=$false;shipping_batch_policy_changed=$false}|ConvertTo-Json -Depth 6|Set-Content "$build\probe-build-report.json"
}
