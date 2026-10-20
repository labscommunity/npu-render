$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render"
foreach($trial in @('ptl-worker-v27-qualification')){
 $q=Get-Content "$r\vm\$trial\report.json" -Raw|ConvertFrom-Json;if(!$q.completed){throw 'Cannot archive active experiment'}
}
function Item([string]$name,[string]$path,[string]$remote){
 if(!(Test-Path $path)){throw "Missing evidence: $name"}
 $stream=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
 try{$h=(Get-FileHash -InputStream $stream).Hash}finally{$stream.Dispose()}
 [ordered]@{source=$name;remote_source=$remote;bytes=(Get-Item $path).Length;sha256=$h}
}
$items=@(
foreach($name in @('ptl-worker-v27-qualification','ptl-native-worker-v27-selected','ptl-native-worker-v27-full','program-vector-fill-comparison-v1')){
 if(!(Test-Path "$r\vm\$name")){continue}
 Get-ChildItem "$r\vm\$name" -File -Recurse|Where-Object {
  ($_.Extension -in @('.json','.jsonl','.log','.txt','.inf','.ps1','.py','.c','.cpp','.hpp')) -and
  ($_.Name -notlike 'client-*' -or ($_.Name -like '*.stderr.log' -and $_.Length -gt 0))
 }|ForEach-Object {
  if($_.Length -gt 30000000){throw 'Unexpected artifact size'}
  $n=$_.FullName.Substring(("$r\vm\").Length).Replace('\','/');Item $n $_.FullName ("vm/"+$n)
 }
}
foreach($name in @('build-ptl-native-worker-v27.ps1','build-ptl-native-worker-v27.log','qualify-ptl-native-worker-v27.py','qualify-program-vector-fill-comparison-v1.py','run-ptl-worker-v27-qualification.ps1','ptl-native-worker-v27-source.json','ptl-worker-v27-tools-source.json','ptl-native-worker-v27/ptl_long_triangle_draw.cpp','ptl-native-worker-v27/ptl_solid.hpp','ptl-native-worker-v27/packet_provider_process.hpp','ptl-native-worker-v27/ptl_program_ir.hpp','ptl-native-worker-v27/CMakeLists.txt','ptl-native-worker-v27/ptl_four_tap.hpp','ptl-native-worker-v27/ptl_masked_triangle_draw.cpp','ptl-native-worker-v27/ptl_triangle_texture_draw.cpp','ptl-native-worker-v27/ptl_resident_clip.hpp','ptl-native-worker-v27/local_indexed_worker.cpp','ptl-native-worker-v27/ptl_complex_shader.hpp','ptl-native-worker-v27/ptl_geometry_triangle_draw.cpp','ptl-native-worker-v27/ptl_long_shaders.hpp','ptl-native-worker-v27/ptl_resident_clip_triangle_draw.hpp','ptl-native-worker-v27/ptl_four_tap_shaders.hpp','ptl-native-worker-v27/ptl_masked.hpp','ptl-native-worker-v27/ptl_program_triangle_draw.hpp','ptl-native-worker-v27/ptl_complex_triangle_draw.cpp','ptl-native-worker-v27/indexed_pipeline.hpp','ptl-native-worker-v27/ptl_long_shader.hpp','ptl-native-worker-v27/ptl_triangle_texture_draw.hpp','ptl-native-worker-v27/ptl_adaptive_triangle.hpp','ptl-native-worker-v27/ptl_geometry.hpp','ptl-native-worker-v27/ptl_solid_shaders.hpp','ptl-native-worker-v27/ptl_geometry_triangle_draw.hpp','ptl-native-worker-v27/ptl_masked_shaders.hpp','ptl-native-worker-v27/ptl_resident_clip_triangle_draw.cpp','ptl-native-worker-v27/local_indexed_validated_client.hpp','ptl-native-worker-v27/ptl_program_triangle_draw.cpp','ptl-native-worker-v27/ptl_complex_triangle_draw.hpp','ptl-native-worker-v27/npu_turbo_import.hpp','ptl-native-worker-v27/ptl_quad_batch.hpp','ptl-native-worker-v27/ptl_long_triangle_draw.hpp','ptl-native-worker-v27/ptl_point_rectangle.hpp','ptl-native-worker-v27/ptl_program.hpp','ptl-native-worker-v27/ptl_texture_shader.hpp','ptl-native-worker-v27/ptl_masked_triangle_draw.hpp','ptl-native-worker-v27/response_envelope_v1.hpp','ptl-native-worker-v27/ptl_complex_shaders.hpp','ptl-native-worker-v27/ptl_texture_shaders.hpp')){
 Item $name "$r\vm\$name" ("vm/"+$name)
}
foreach($version in @(27)){foreach($file in @('build-report.json','registered-build-report.json')){
 $name="ptl-native-worker-v$version"
 if(Test-Path "$r\builds\$name\$file"){Item "build-$name/$file" "$r\builds\$name\$file" "builds/$name/$file"}
}}
);$items|ConvertTo-Json -Depth 5
