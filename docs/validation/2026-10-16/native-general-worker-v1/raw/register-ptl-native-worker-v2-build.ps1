$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$b="$r\builds\ptl-native-worker-v2"
$p=Get-Content "$b\build-report.json" -Raw|ConvertFrom-Json
$exe="$b\Release\nr_registered_local_indexed_worker.exe"
if(!$p.clean_build -or (Get-FileHash $exe).Hash -ne $p.binary_sha256 -or $p.binary_sha256 -ne '6F49045CCBB56F8D27EAE6B363EEA7D44EB1EB4BA564F99A2C822A9ECF1FF115'){throw 'Pinned build receipt mismatch'}
if(Test-Path "$b\registered-build-report.json"){throw 'Receipt exists'}
[ordered]@{clean_build=$true;binary_sha256=@{'nr_registered_local_indexed_worker.exe'=$p.binary_sha256};rendering_implementation=$p.rendering_implementation;source_sha256=$p.source_sha256;linked_libraries=$p.linked_libraries;derived_from='build-report.json';build_report_sha256=(Get-FileHash "$b\build-report.json").Hash}|ConvertTo-Json -Depth 6|Set-Content "$b\registered-build-report.json"
