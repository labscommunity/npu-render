$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$b="$r\builds\input-reuse-v1";$out="$r\vm\input-reuse-v1-loop-correction"
if(Test-Path $out){throw 'Correction evidence exists'}
if(@(Get-Process nr_registered_indexed_stream_worker,nr_registered_indexed_worker -ErrorAction SilentlyContinue).Count){throw 'Another renderer is active'}
$m=Get-Content "$b\registered-build-report.json" -Raw|ConvertFrom-Json
if((Get-FileHash "$b\Release\nr_registered_loop_test.exe").Hash -ne $m.binary_sha256.'nr_registered_loop_test.exe'){throw 'Test binary mismatch'}
New-Item -ItemType Directory $out|Out-Null;$oldPath=$env:PATH;$oldCache=$env:NR_RENDER_NPU_CACHE;$oldShared=$env:NR_RENDER_SHARED_QUEUE;$oldPolicy=$env:NR_RENDER_PIPELINE_CACHE
try{
 $env:PATH="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs;$oldPath";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-2026-4-v209";$env:NR_RENDER_SHARED_QUEUE=$null;$env:NR_RENDER_PIPELINE_CACHE=$null
 $watch=[Diagnostics.Stopwatch]::StartNew();$p=Start-Process "$b\Release\nr_registered_loop_test.exe" -ArgumentList @('NPU','all') -RedirectStandardOutput "$out\stdout.jsonl" -RedirectStandardError "$out\stderr.log" -PassThru
 [void]$p.Handle;try{if(!$p.WaitForExit(600000)){$p.Kill();throw 'Loop test timed out'};$p.Refresh();if($p.ExitCode){throw 'Correct loop invocation failed'}}finally{$p.Dispose()}
 $rows=@(Get-Content "$out\stdout.jsonl"|Where-Object {$_ -match '^\{'}|ForEach-Object {$_|ConvertFrom-Json})
 if(@($rows|Where-Object {$_.bounded_loop_components -eq 128}).Count -ne 1 -or @($rows|Where-Object {$_.exact_components -eq 256}).Count -ne 3 -or @($rows|Where-Object {$_.captured_shader_words -eq 1850 -and $_.verified_components -eq 16}).Count -ne 72 -or @($rows|Where-Object {$_.captured_loop_raster_pixels -eq 1024 -and $_.covered_samples -eq 572}).Count -ne 1){throw 'Expected loop test cases did not all execute'}
 if(@($rows|Where-Object {$_.execution_device -ne 'NPU'}).Count){throw 'Loop result attribution mismatch'}
 [ordered]@{completed=$true;passed=$true;build='input-reuse-v1';backend='NPU';invocation='NPU all';test_result_rows=$rows.Count;expected_test_result_rows=77;elapsed_ms=$watch.ElapsedMilliseconds;original_incorrect_invocation='--backend NPU';original_invocation_ran_no_cases=$true;original_empty_stdout_preserved=$true;original_exit_zero_was_not_test_evidence=$true;all_77_device_receipts_required=$true;correction='The loop test accepts positional backend and section arguments, unlike texture/prepared-input tests. The earlier controller only checked its zero exit code. This run uses positional arguments and validates every expected device receipt.'}|ConvertTo-Json -Depth 4|Set-Content "$out\report.json"
 'All 77 NPU loop regression receipts verified; original no-op invocation preserved as a harness failure'
}finally{$env:PATH=$oldPath;$env:NR_RENDER_NPU_CACHE=$oldCache;$env:NR_RENDER_SHARED_QUEUE=$oldShared;$env:NR_RENDER_PIPELINE_CACHE=$oldPolicy}
