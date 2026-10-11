$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$src='C:\Users\tatef\AppData\Local\Temp\npu-graphics-probe-0146a2388f25';$build="$r\builds\present-umd-v207";$out="$r\vm\runtime-2026-4-v207-remaining"
if(Test-Path $out){throw 'Evidence exists'}
$manifest=Get-Content "$build\registered-build-report.json" -Raw|ConvertFrom-Json
if(!$manifest.clean_build){throw 'Clean build required'}
foreach($f in $manifest.binary_sha256.psobject.Properties){if((Get-FileHash "$build\Release\$($f.Name)").Hash -ne $f.Value){throw 'Binary hash mismatch'}}
foreach($f in $manifest.source_sha256.psobject.Properties){if((Get-FileHash "$src\$($f.Name)").Hash -ne $f.Value){throw 'Source hash mismatch'}}
$stream=Get-Content "$r\vm\indexed-dwm-stream-v105\report.json" -Raw|ConvertFrom-Json;if(!$stream.completed -or !$stream.configuration_removed){throw 'Live stream still active'}
New-Item -ItemType Directory $out|Out-Null;$oldPath=$env:PATH;$oldCache=$env:NR_RENDER_NPU_CACHE
$results=[Collections.Generic.List[object]]::new()
function Run([string]$name,[string]$binary,[string]$arguments,[int]$limit=240000){
 $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=if([IO.Path]::IsPathRooted($binary)){$binary}else{"$build\Release\$binary"};$info.Arguments=$arguments;$info.UseShellExecute=$false;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 $process=[Diagnostics.Process]::new();$process.StartInfo=$info
 if(!$process.Start()){throw 'Cannot start qualification'}
 $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync();$watch=[Diagnostics.Stopwatch]::StartNew();$timedOut=$false
 if(!$process.WaitForExit($limit)){$timedOut=$true;$process.Kill();$process.WaitForExit()}
 $code=$process.ExitCode;[IO.File]::WriteAllText("$out\$name.stdout.jsonl",$stdout.GetAwaiter().GetResult());[IO.File]::WriteAllText("$out\$name.stderr.log",$stderr.GetAwaiter().GetResult())
 $entry=[ordered]@{name=$name;exit_code=$code;timed_out=$timedOut;elapsed_ms=$watch.ElapsedMilliseconds;passed=(!$timedOut -and $code -eq 0)};$results.Add($entry);$results|ConvertTo-Json|Set-Content "$out\runs.json"
 Get-Content "$out\$name.stdout.jsonl" -Tail 3;Get-Content "$out\$name.stderr.log" -Tail 4;$process.Dispose()

}
try{
 $env:PATH="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs;$env:PATH";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-2026-4-v205"
 foreach($test in @(@('crop','nr_registered_texture_crop_test.exe'),@('point','nr_registered_point_texture_draw_test.exe'),@('linear','nr_registered_linear_texture_draw_test.exe'),@('color-clear','nr_registered_color_clear_test.exe'))){Run ($test[0]+'-NPU') $test[1] 'NPU' 600000}
 Run 'modulated-point-NPU' 'nr_registered_linear_texture_draw_test.exe' 'NPU --point' 600000
 Run 'opaque-point-NPU' 'nr_registered_linear_texture_draw_test.exe' 'NPU --point-opaque' 600000
 foreach($test in @(@('triangles','nr_registered_triangle_texture_draw_test.exe'),@('derivatives','nr_registered_derivative_test.exe'),@('compositor','nr_registered_compositor_test.exe'),@('typed-load','nr_registered_typed_load_test.exe'),@('loops','nr_registered_loop_test.exe'),@('masks','nr_registered_mask_test.exe'),@('mirror','nr_registered_mirror_test.exe'))){Run ($test[0]+'-NPU') $test[1] 'NPU' $(if($test[0] -eq 'compositor'){600000}else{240000})}
 $failures=@($results|Where-Object {!$_.passed})
 [ordered]@{build='present-umd-v207';component_survey_only=$true;failed_tests=$failures.name;completed_tests=$results.Count;desktop_offload_tested=$false;deployable=$false}|ConvertTo-Json|Tee-Object "$out\report.json"
}finally{$env:PATH=$oldPath;$env:NR_RENDER_NPU_CACHE=$oldCache}
