$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render"
foreach($trial in @('ptl-worker-v31-qualification')){
 $q=Get-Content "$r\vm\$trial\report.json" -Raw|ConvertFrom-Json;if(!$q.completed){throw 'Cannot archive active experiment'}
}
function Item([string]$name,[string]$path,[string]$remote){
 if(!(Test-Path $path)){throw "Missing evidence: $name"}
 $stream=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
 try{$h=(Get-FileHash -InputStream $stream).Hash}finally{$stream.Dispose()}
 [ordered]@{source=$name;remote_source=$remote;bytes=(Get-Item $path).Length;sha256=$h}
}
$items=@(
foreach($name in @('ptl-worker-v31-qualification','ptl-native-worker-v31-selected','ptl-native-worker-v31-full','combined-fill-turbo-comparison-v1','combined-fill-turbo-provider-recovery-v1')){
 if(!(Test-Path "$r\vm\$name")){continue}
 Get-ChildItem "$r\vm\$name" -File -Recurse|Where-Object {
  ($_.Extension -in @('.json','.jsonl','.log','.txt','.inf','.ps1','.py','.c','.cpp','.hpp')) -and
  ($_.Name -notlike 'client-*' -or ($_.Name -like '*.stderr.log' -and $_.Length -gt 0))
 }|ForEach-Object {
  if($_.Length -gt 30000000){throw 'Unexpected artifact size'}
  $n=$_.FullName.Substring(("$r\vm\").Length).Replace('\','/');Item $n $_.FullName ("vm/"+$n)
 }
}
foreach($name in @('ptl-native-worker-v31-source.json','ptl-worker-v31-tools-source.json','build-ptl-native-worker-v31.ps1','build-ptl-native-worker-v31.log','qualify-ptl-native-worker-v31.py','qualify-combined-fill-turbo-comparison-v1.py','qualify-combined-fill-turbo-provider-recovery-v1.py','run-ptl-worker-v31-qualification.ps1','start-combined-fill-turbo-v1.ps1','combined-fill-turbo-v1-task.xml','combined-fill-turbo-v1-task-start.json','ptl-native-worker-v31/ptl_long_triangle_draw.cpp','ptl-native-worker-v31/ptl_solid.hpp','ptl-native-worker-v31/packet_provider_process.hpp','ptl-native-worker-v31/ptl_program_ir.hpp','ptl-native-worker-v31/CMakeLists.txt','ptl-native-worker-v31/ptl_four_tap.hpp','ptl-native-worker-v31/ptl_masked_triangle_draw.cpp','ptl-native-worker-v31/ptl_triangle_texture_draw.cpp','ptl-native-worker-v31/ptl_resident_clip.hpp','ptl-native-worker-v31/local_indexed_worker.cpp','ptl-native-worker-v31/ptl_complex_shader.hpp','ptl-native-worker-v31/ptl_geometry_triangle_draw.cpp','ptl-native-worker-v31/ptl_long_shaders.hpp','ptl-native-worker-v31/ptl_resident_clip_triangle_draw.hpp','ptl-native-worker-v31/ptl_four_tap_shaders.hpp','ptl-native-worker-v31/ptl_masked.hpp','ptl-native-worker-v31/ptl_program_triangle_draw.hpp','ptl-native-worker-v31/ptl_complex_triangle_draw.cpp','ptl-native-worker-v31/indexed_pipeline.hpp','ptl-native-worker-v31/ptl_long_shader.hpp','ptl-native-worker-v31/ptl_triangle_texture_draw.hpp','ptl-native-worker-v31/ptl_adaptive_triangle.hpp','ptl-native-worker-v31/ptl_geometry.hpp','ptl-native-worker-v31/ptl_solid_shaders.hpp','ptl-native-worker-v31/ptl_geometry_triangle_draw.hpp','ptl-native-worker-v31/ptl_masked_shaders.hpp','ptl-native-worker-v31/ptl_resident_clip_triangle_draw.cpp','ptl-native-worker-v31/local_indexed_validated_client.hpp','ptl-native-worker-v31/ptl_program_triangle_draw.cpp','ptl-native-worker-v31/ptl_complex_triangle_draw.hpp','ptl-native-worker-v31/npu_turbo_import.hpp','ptl-native-worker-v31/ptl_quad_batch.hpp','ptl-native-worker-v31/ptl_long_triangle_draw.hpp','ptl-native-worker-v31/ptl_point_rectangle.hpp','ptl-native-worker-v31/ptl_program.hpp','ptl-native-worker-v31/ptl_texture_shader.hpp','ptl-native-worker-v31/ptl_masked_triangle_draw.hpp','ptl-native-worker-v31/response_envelope_v1.hpp','ptl-native-worker-v31/ptl_complex_shaders.hpp','ptl-native-worker-v31/ptl_texture_shaders.hpp')){
 Item $name "$r\vm\$name" ("vm/"+$name)
}
foreach($version in @(31)){foreach($file in @('build-report.json','registered-build-report.json')){
 $name="ptl-native-worker-v$version"
 if(Test-Path "$r\builds\$name\$file"){Item "build-$name/$file" "$r\builds\$name\$file" "builds/$name/$file"}
}}
Item "build-ptl-compiled-turbo-v1/build-report-v2.json" "$r/builds/ptl-compiled-turbo-v1/build-report-v2.json" "builds/ptl-compiled-turbo-v1/build-report-v2.json"
);$items|ConvertTo-Json -Depth 5
