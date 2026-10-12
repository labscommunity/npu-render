$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render"
$stream=Get-Content "$r\vm\indexed-dwm-stream-v113\report.json" -Raw|ConvertFrom-Json
if(!$stream.completed -or !$stream.configuration_removed){throw 'Desktop stream active'}
$sdk="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino"
foreach($variant in 'baseline','candidate'){
 $name="vertex-capacity-$variant-v1";$source="$r\vm\$name";$b="$r\builds\$name";$log="$r\vm\build-$name.log"
 if(Test-Path $b){throw 'Diagnostic build already exists'}
 $expected=Get-Content "$r\vm\$name-source.json" -Raw|ConvertFrom-Json
 foreach($p in $expected.PSObject.Properties){if((Get-FileHash "$source\$($p.Name)").Hash.ToLowerInvariant() -ne $p.Value){throw 'Diagnostic source mismatch'}}
 $ErrorActionPreference='Continue'
 & cmake -S "$source\native\windows\registered_lab" -B $b -A x64 "-DOpenVINO_DIR=$sdk\cmake" "-DNR_WDK_INCLUDE_ROOT=$r\dependencies\wdk-26100.6584-headers" *> $log
 $code=$LASTEXITCODE
 if(!$code){& cmake --build $b --config Release --clean-first --target nr_registered_indexed_worker nr_registered_triangle_texture_draw_test nr_registered_indexed_exchange_test *>> $log;$code=$LASTEXITCODE}
 $ErrorActionPreference='Stop';Get-Content $log -Tail 5;if($code){throw 'Diagnostic build failed'}
 $env:PATH="$sdk\libs;$env:PATH"
 $sections=if($variant -eq 'candidate'){@('','--capacity')}else{@('')}
 foreach($section in $sections){
  $label=if($section){'capacity'}else{'exact'};$ErrorActionPreference='Continue'
  if($section){& "$b\Release\nr_registered_triangle_texture_draw_test.exe" CPU $section *> "$r\vm\$name-CPU-$label.log"}else{& "$b\Release\nr_registered_triangle_texture_draw_test.exe" CPU *> "$r\vm\$name-CPU-$label.log"}
  $code=$LASTEXITCODE;$ErrorActionPreference='Stop';if($code){throw 'Diagnostic CPU triangle test failed'}
 }
 $hashes=[ordered]@{};foreach($p in $expected.PSObject.Properties){$hashes[$p.Name]=(Get-FileHash "$source\$($p.Name)").Hash}
 [ordered]@{sources=$hashes;source_root=$source;runtime_directory="$sdk\libs";runtime=@(Get-ChildItem "$sdk\libs" -Filter '*.dll'|ForEach-Object{@{name=$_.Name;sha256=(Get-FileHash $_.FullName).Hash}});binaries=@(Get-ChildItem "$b\Release" -Filter '*.exe'|ForEach-Object{@{name=$_.Name;sha256=(Get-FileHash $_.FullName).Hash}});clean_build=$true;cpu_triangles_passed=$true;production_source_changed=$false;desktop_offload_tested=$false}|ConvertTo-Json -Depth 5|Set-Content "$b\probe-build-report.json"
}
