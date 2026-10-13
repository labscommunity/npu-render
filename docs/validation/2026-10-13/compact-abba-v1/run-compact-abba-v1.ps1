$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$b="$r\builds\compact-runtime-v1";$root="$r\vm\compact-abba-v1"
if(Test-Path $root){throw 'ABBA results already exist'}
if(@(Get-Process nr_registered_indexed_stream_worker,nr_registered_indexed_worker -ErrorAction SilentlyContinue).Count){throw 'Another renderer active'}
$q=Get-Content "$r\vm\ordered-compact-runtime-v1-summary.json" -Raw|ConvertFrom-Json;if(!$q.passed -or !$q.completed){throw 'Full correctness qualification required'}
New-Item -ItemType Directory $root|Out-Null
$py="$r\toolchains\openvino-2026.4.1\Scripts\python.exe";$runtime="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs";$reference="$r\vm\ordered-point-reentry-v1-expanded\report.json";$cases=@('A1','B1','B2','A2');$rows=@()
foreach($name in $cases){
  $at=[DateTime]::UtcNow.ToString('o');$enabled=$name.StartsWith('B');$out="$root\$name"
  $args=@("$r\vm\replay-ordered-trace-v8-compact.py",'--trace',"$r\vm\indexed-dwm-stream-v120",'--trace-manifest',"$r\vm\v120-trace-sha256.json",'--worker',"$b\Release\nr_registered_indexed_worker.exe",'--runtime',$runtime,'--cache',"$r\vm\model-cache-2026-4-v209",'--output',$out,'--seconds','1800','--limit','1438','--shared-queue','1','--pipeline-cache','expanded','--reference',$reference)
  if($enabled){$args+='--compact-coverage'}
  & $py @args
  if($LASTEXITCODE){throw "Matched trial $name failed"}
  $m=Get-Content "$out\report.json" -Raw|ConvertFrom-Json
  $s=@($m.requests|ForEach-Object {$_.coverage_telemetry})
  if(!$m.passed -or !$m.completed -or $s.Count -ne 1438){throw 'Incomplete matched replay'}
  $rows += [ordered]@{name=$name;compact=$enabled;started=$at;finished=$m.finished;elapsed_ms=$m.elapsed_ms;draw_ms=($s|Measure-Object draw_ms -Sum).Sum;compile_and_draw_ms=($s|Measure-Object compile_and_draw_ms -Sum).Sum;compile_calls=($s|Measure-Object compile_calls -Sum).Sum;cache_hits=($s|Measure-Object cache_hits -Sum).Sum;compact_calls=($s|Measure-Object compact_coverage_calls -Sum).Sum;compact_samples=($s|Measure-Object compact_coverage_samples -Sum).Sum;wide_calls=($s|Measure-Object wide_coverage_calls -Sum).Sum;passed=$true}
  [ordered]@{completed=($rows.Count -eq 4);cases=$rows;worker_unchanged=$true;order='off,on,on,off';physical_desktop_tested=$false;tahoma_performance_tested=$false}|ConvertTo-Json -Depth 6|Set-Content "$root\summary.json"
}
'ABBA replay completed'
