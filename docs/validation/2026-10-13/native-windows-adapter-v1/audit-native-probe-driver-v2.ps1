$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$b="$r\builds\native-probe-driver-v2"
$dump='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\dumpbin.exe'
foreach($name in 'nr_native_probe_umd.dll','nr_native_probe_ddi_test.exe'){
 & $dump /dependents "$b\Release\$name" > "$r\vm\native-probe-driver-v2-$name-dependents.log"
 if($LASTEXITCODE){throw 'PE dependency audit failed'}
 $log=Get-Content "$r\vm\native-probe-driver-v2-$name-dependents.log" -Raw
 $deps=@([regex]::Matches($log,'(?im)^\s+([a-z0-9_.-]+\.dll)\s*$')|ForEach-Object {$_.Groups[1].Value})
 [ordered]@{binary=$name;dependencies=$deps;openvino_imported=([bool]($deps -match 'openvino'));sha256=(Get-FileHash "$b\Release\$name").Hash}|ConvertTo-Json -Depth 4
 if($deps -match 'openvino'){throw 'Native consumer still imports OpenVINO'}
}
& ([scriptblock]::Create([IO.File]::ReadAllText("$r\vm\qualify-native-probe-driver-v2.ps1")))
& ([scriptblock]::Create([IO.File]::ReadAllText("$r\vm\qualify-native-loaded-v3.ps1")))
