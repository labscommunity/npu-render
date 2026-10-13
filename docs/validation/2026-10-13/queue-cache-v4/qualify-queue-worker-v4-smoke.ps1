$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$b="$r\builds\queue-worker-v4";$out="$r\vm\queue-worker-v4-smoke"
$prior=Get-Content "$r\vm\indexed-dwm-stream-v120\report.json" -Raw|ConvertFrom-Json
if(!$prior.completed -or !$prior.configuration_removed -or (Get-Process -Id $prior.worker_pid -ErrorAction SilentlyContinue)){throw 'Desktop cleanup required'}
if(@(Get-Process nr_registered_indexed_stream_worker,nr_registered_indexed_worker -ErrorAction SilentlyContinue).Count){throw 'Another NPU renderer is running'}
$m=Get-Content "$b\registered-build-report.json" -Raw|ConvertFrom-Json
if(!$m.clean_build -or !$m.relevant_cpu_contracts_passed -or !$m.expanded_pipeline_capacity_opt_in){throw 'Qualified build required'}
foreach($f in $m.binary_sha256.psobject.Properties){if((Get-FileHash "$b\Release\$($f.Name)").Hash -ne $f.Value){throw 'Binary mismatch'}}
$probe=Get-Content "$r\vm\queue-worker-v4-probe\report.json" -Raw|ConvertFrom-Json;if(!$probe.all_checks_passed){throw 'Queue property check required'}
$component=Get-Content "$r\vm\component-coverage-v229\packets\report.json" -Raw|ConvertFrom-Json;if(!$component.all_pixels_exact){throw 'Component oracle required'}
if(Test-Path $out){throw 'Evidence exists'};New-Item -ItemType Directory $out|Out-Null
$py="$r\toolchains\openvino-2026.4.1\Scripts\python.exe";$runtime="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs"
$oldPath=$env:PATH;$oldCache=$env:NR_RENDER_NPU_CACHE;$oldShared=$env:NR_RENDER_SHARED_QUEUE;$oldPolicy=$env:NR_RENDER_PIPELINE_CACHE
function Run([string]$name,[string]$directory,[string]$count,[int]$expected=0){
 $p=Start-Process "$b\Release\nr_registered_indexed_worker.exe" -ArgumentList @(('"'+$directory+'"'),$count) -RedirectStandardOutput "$out\$name.stdout.jsonl" -RedirectStandardError "$out\$name.stderr.log" -PassThru
 [void]$p.Handle
 try{if(!$p.WaitForExit(90000)){$p.Kill();throw 'Worker smoke timed out'};$p.Refresh();if($p.ExitCode -ne $expected){throw "Unexpected worker exit: $name $($p.ExitCode)"}}finally{$p.Dispose()}
}
function Check-Policy([string]$path,[bool]$expanded,[bool]$shared){
 $rows=@(Get-Content $path|Where-Object {$_ -match '^\{'}|ForEach-Object {$_|ConvertFrom-Json})
 $policy=@($rows|Where-Object {$_.kind -eq 'pipeline_cache_policy'});$uniform=if($expanded){16}else{8};$triangle=if($expanded){48}else{24}
 if($policy.Count -ne 1 -or $policy[0].uniform_capacity -ne $uniform -or $policy[0].triangle_capacity -ne $triangle -or $policy[0].clear_capacity -ne 4 -or $policy[0].replacement -ne 'FIFO'){throw 'Cache policy readback mismatch'}
 $queue=@($rows|Where-Object {$_.kind -eq 'private_shared_queue_experiment'})
 if($shared){if($queue.Count -ne 1 -or !$queue[0].enabled -or !$queue[0].property_readback_verified){throw 'Shared queue readback mismatch'}}elseif($queue.Count){throw 'Default mode unexpectedly enabled private queue override'}
}
try{
 $env:PATH="$runtime;$oldPath";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-2026-4-v209";$env:NR_RENDER_SHARED_QUEUE=$null;$env:NR_RENDER_PIPELINE_CACHE='invalid'
 New-Item -ItemType Directory "$out\invalid"|Out-Null;Run 'invalid-policy' "$out\invalid" '1' 1
 if((Get-Content "$out\invalid-policy.stderr.log" -Raw) -notmatch 'pipeline cache experiment requires expanded'){throw 'Invalid policy not rejected explicitly'}
 $env:NR_RENDER_PIPELINE_CACHE=$null
 $manifest=@(Get-ChildItem "$r\vm\trace-worker-v5-smoke\trace-request-*.bin"|ForEach-Object {[ordered]@{name=$_.Name;bytes=$_.Length;sha256=(Get-FileHash $_.FullName).Hash}})
 if($manifest.Count -ne 3){throw 'Trace smoke fixture missing'};$manifest|ConvertTo-Json|Set-Content "$out\trace-sha256.json"
 foreach($variant in 'baseline','shared','expanded'){
  $nrReplayArgs=@("$r\vm\replay-ordered-trace-v3.py",'--trace',"$r\vm\trace-worker-v5-smoke",'--trace-manifest',"$out\trace-sha256.json",'--worker',"$b\Release\nr_registered_indexed_worker.exe",'--runtime',$runtime,'--cache',"$r\vm\model-cache-2026-4-v209",'--output',"$out\$variant-ordered",'--seconds','90','--limit','3')
  if($variant -ne 'baseline'){$nrReplayArgs+=@('--shared-queue','1','--reference',"$out\baseline-ordered\report.json")}
  if($variant -eq 'expanded'){$nrReplayArgs+=@('--pipeline-cache','expanded')}
  & $py @nrReplayArgs;if($LASTEXITCODE){throw "Ordered smoke failed: $variant"}
  Check-Policy "$out\$variant-ordered\worker.stdout.jsonl" ($variant -eq 'expanded') ($variant -ne 'baseline')
  $directory="$out\$variant-components";New-Item -ItemType Directory $directory|Out-Null
  foreach($case in $component.cases){$path="$r\vm\component-coverage-v229\packets\$($case.packet)";if((Get-FileHash $path).Hash.ToLowerInvariant() -ne $case.sha256){throw 'Component packet changed'};Copy-Item $path $directory}
  $env:NR_RENDER_SHARED_QUEUE=if($variant -eq 'baseline'){$null}else{'1'};$env:NR_RENDER_PIPELINE_CACHE=if($variant -eq 'expanded'){'expanded'}else{$null}
  Run "$variant-components" $directory '3'
  Check-Policy "$out\$variant-components.stdout.jsonl" ($variant -eq 'expanded') ($variant -ne 'baseline')
  foreach($case in $component.cases){
   $request=[IO.File]::ReadAllBytes("$directory\$($case.packet)");$reply=[IO.File]::ReadAllBytes("$directory\$($case.packet.Replace('request-','reply-'))")
   if($reply.Length -ne 16512 -or [BitConverter]::ToUInt32($reply,68) -ne 2 -or [BitConverter]::ToUInt32($reply,72) -ne 3 -or [BitConverter]::ToUInt64($reply,104) -ne 3132){throw 'Component NPU reply mismatch'}
   foreach($offset in @(0,8,16)){if([BitConverter]::ToUInt64($request,$offset) -ne [BitConverter]::ToUInt64($reply,$offset)){throw 'Component identity mismatch'}}
   foreach($offset in @(24,28,32,36,40,44,48,52,56,64,76)){if([BitConverter]::ToUInt32($request,$offset) -ne [BitConverter]::ToUInt32($reply,$offset)){throw 'Component shape mismatch'}}
   $sha=[Security.Cryptography.SHA256]::Create();try{$hash=[BitConverter]::ToString($sha.ComputeHash($reply,128,16384)).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
   if($hash -ne $case.expected_sha256){throw 'Component independent oracle mismatch'}
  }
  $env:NR_RENDER_SHARED_QUEUE=$null;$env:NR_RENDER_PIPELINE_CACHE=$null
 }
 [ordered]@{all_checks_passed=$true;build='queue-worker-v4';default_shared_expanded_exact=$true;ordered_trace_cases_per_variant=3;independent_component_cases_per_variant=3;invalid_cache_policy_rejected=$true;cache_capacity_readback_verified=$true;shared_queue_readback_verified=$true;hardware_queue_count_measured=$false;performance_improvement_measured=$false;desktop_use_qualified=$false}|ConvertTo-Json|Tee-Object "$r\vm\queue-worker-v4-smoke-qualified.json"
}finally{$env:PATH=$oldPath;$env:NR_RENDER_NPU_CACHE=$oldCache;$env:NR_RENDER_SHARED_QUEUE=$oldShared;$env:NR_RENDER_PIPELINE_CACHE=$oldPolicy}
