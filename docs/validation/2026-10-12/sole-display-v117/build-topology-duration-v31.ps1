$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$s="$r\vm\topology-duration-v31";$b="$r\builds\topology-duration-v31";$log="$r\vm\build-topology-duration-v31.log"
$stream=Get-Content "$r\vm\indexed-dwm-stream-v116\report.json" -Raw|ConvertFrom-Json;if(!$stream.completed -or !$stream.configuration_removed){throw 'Desktop stream must finish before building'}
if(Test-Path $b){throw 'Build already exists'}
$manifest=Get-Content "$r\vm\topology-duration-v31-source.json" -Raw|ConvertFrom-Json
foreach($f in $manifest.psobject.Properties){if((Get-FileHash "$s\$($f.Name)").Hash -ne $f.Value){throw 'Source mismatch'}}
foreach($name in 'start_indirect_topology_task-v31.ps1','observe_registered_desktop-v31.ps1'){
$tokens=$null;$errors=$null;[void][Management.Automation.Language.Parser]::ParseFile("$s\$name",[ref]$tokens,[ref]$errors);if($errors.Count){throw 'PowerShell parser rejected experiment'}
}
$ErrorActionPreference='Continue'
& cmake -S $s -B $b -A x64 *> $log;$code=$LASTEXITCODE
if(!$code){& cmake --build $b --config Release --clean-first *>> $log;$code=$LASTEXITCODE}
$ErrorActionPreference='Stop';Get-Content $log -Tail 6;if($code){throw 'Topology build failed'}
foreach($f in $manifest.psobject.Properties){if((Get-FileHash "$s\$($f.Name)").Hash -ne $f.Value){throw 'Source changed during build'}}
$binary="$b\Release\nr_indirect_topology_gui.exe";$hash=(Get-FileHash $binary).Hash
[ordered]@{kind='finite-topology-duration-experiment';base_source_commit='04069b560a44c278b6150da68f678767b8b2b41e';source_sha256=$manifest;clean_build=$true;binary_sha256=$hash;maximum_observation_seconds=1500;default_observation_seconds=45;task_maximum_seconds=1560;guest_guards_preserved=$true;restore_behavior_changed=$false;powershell_parser_passed=$true;desktop_offload_tested=$false}|ConvertTo-Json -Depth 5|Set-Content "$b\build-report.json"
'Topology duration experiment built'
