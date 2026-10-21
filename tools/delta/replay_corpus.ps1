# Replays every recorded desktop capture on Delta through one worker build and writes a summary:
# the per-build correctness gate that runs in minutes, before any live trial. A capture is a
# bootstrap "requests" directory with an index.jsonl; a request without a recorded response (the
# worker failed on it) is still replayed, because that is exactly the case worth catching.
param([Parameter(Mandatory=$true)][string]$Build,[string[]]$Captures=@(),[string]$Output='',[string]$FrameBlobs='frame-blobs-v18\deploy',[int]$MaxDiff=2,[double]$MaxFraction=0.01)
$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue'
$r="$env:LOCALAPPDATA\npu-render";$b="$r\builds\$Build\Release";$py="$r\toolchains\openvino-2026.4.1\Scripts\python.exe"
if(!$Output){$Output="$r\vm\replay-corpus-$Build-$(Get-Date -Format 'MMdd-HHmmss')"}
New-Item -ItemType Directory $Output|Out-Null
if(!$Captures.Count){ $Captures=@(Get-ChildItem "$r\vm" -Directory | Where-Object { $_.Name -match '^native-desktop-admission-v\d+$' -and (Test-Path "$($_.FullName)\bootstrap\requests\index.jsonl") } | ForEach-Object { "$($_.FullName)\bootstrap\requests" }) }
$envs=@("NR_RENDER_PTL_COMPLEX_BLOB=$r\vm\ptl-six-span-complex-shader-v1\patched.blob","NR_RENDER_PTL_RECTANGLE_BLOB=$r\vm\ptl-six-span-point-rectangle-v2\patched.blob","NR_RENDER_PTL_LONG_BLOB=$r\vm\ptl-six-span-long-shader-v2\patched.blob","NR_RENDER_PTL_PROGRAM_BLOB=$r\vm\ptl-six-span-program-compact-v1\patched.blob","NR_RENDER_PTL_MASKED_BLOB=$r\vm\ptl-six-span-masked-v1\patched.blob","NR_RENDER_PTL_GEOMETRY_BLOB=$r\vm\ptl-six-span-geometry-v2\patched.blob","NR_RENDER_PTL_TEXTURE_BLOB=$r\vm\ptl-six-span-three-texture-v1\patched.blob","NR_RENDER_PTL_SOLID_BLOB=$r\vm\ptl-six-span-solid-v3\patched.blob","NR_RENDER_PTL_FOUR_TAP_BLOB=$r\vm\ptl-six-span-general-v1\patched.blob","NR_RENDER_PTL_RESIDENT_CLIP_BLOB=$r\vm\ptl-six-span-resident-clip-v3\patched.blob","NR_RENDER_PTL_FRAME_BLOB_DIR=$r\vm\$FrameBlobs")
$envArgs=@();foreach($e in $envs){$envArgs+='--env';$envArgs+=$e}
$summary=[ordered]@{build=$Build;frame_blobs=$FrameBlobs;started=[DateTime]::UtcNow.ToString('o');captures=@();passed=$false}
$all=$true
foreach($cap in $Captures){
  $name=($cap -replace '.*\\vm\\','') -replace '\\','_';$out="$Output\$name";New-Item -ItemType Directory $out|Out-Null
  # A request the worker never answered has no provider_response_recorded line; add a synthetic one so the converter keeps it.
  $index=@(Get-Content "$cap\index.jsonl");$tickets=@($index|Where-Object {$_ -match '"request_recorded":true'}|ForEach-Object {($_|ConvertFrom-Json).ticket});$answered=@($index|Where-Object {$_ -match '"provider_response_recorded":true'}|ForEach-Object {($_|ConvertFrom-Json).ticket})
  $unanswered=@($tickets|Where-Object {$answered -notcontains $_})
  $src=$cap
  if($unanswered.Count){ $src="$out\requests";New-Item -ItemType Directory $src|Out-Null;Get-ChildItem $cap -Filter 'request-*.bin'|ForEach-Object { New-Item -ItemType HardLink -Path "$src\$($_.Name)" -Target $_.FullName|Out-Null };$lines=$index;foreach($t in $unanswered){$lines+="{""provider_response_recorded"":true,""ticket"":$t,""pixel_bytes"":0,""pixels_sha256"":""$('0'*64)"",""delivery_acknowledged"":false,""synthetic_for_replay"":true}"};[IO.File]::WriteAllLines("$src\index.jsonl",$lines) }
  $entry=[ordered]@{capture=$cap;requests=$tickets.Count;unanswered=$unanswered.Count;passed=$false}
  $sw=[Diagnostics.Stopwatch]::StartNew()
  try{
    & "$b\nr_frame_batch_replay_convert.exe" $src "$out\per-draw" per-draw 2>&1 | Out-Null
    & $py "$r\vm\replay_frame_batches.py" --worker "$b\nr_registered_local_indexed_worker.exe" --client "$b\nr_registered_local_indexed_client_probe.exe" --runtime "$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs" --cache "$r\vm\model-cache-2026-4-v209" --batches "$out\per-draw" --capture $src --reference --output "$out\replay" --max-diff $MaxDiff --max-fraction $MaxFraction @envArgs 2>&1 | Out-Null
    $j=Get-Content "$out\replay\report.json" -Raw|ConvertFrom-Json
    $wl=@(Get-Content "$out\replay\worker.stdout.jsonl" -ErrorAction SilentlyContinue)
    $entry.passed=[bool]$j.passed;$entry.within_tolerance=$j.within_tolerance;$entry.beyond_tolerance=@($j.beyond_tolerance).Count;$entry.recorded=$j.recorded_replayed;$entry.commands=$j.commands;$entry.fallback=$j.commands_fallback;$entry.error="$($j.error)"
    $entry.fallback_routes=$j.fallback_routes;$entry.route_disagreements=@($wl|Where-Object {$_ -match 'route_disagreement'}).Count;$entry.retired_blobs=@($wl|Where-Object {$_ -match 'frame_kernel_self_test' -and $_ -notmatch '"mismatches":0'}).Count
    $entry.invalid_packets=@(Get-Content "$out\replay\worker.stderr.log" -ErrorAction SilentlyContinue|Where-Object {$_ -match 'invalid indexed'}).Count
    $entry.passed=$entry.passed -and $entry.invalid_packets -eq 0 -and $entry.retired_blobs -eq 0
  }catch{$entry.error=$_.Exception.Message}
  $entry.seconds=[int]$sw.Elapsed.TotalSeconds;$summary.captures+=$entry;if(!$entry.passed){$all=$false}
  $summary|ConvertTo-Json -Depth 6|Set-Content "$Output\summary.json"
}
$summary.passed=$all;$summary.finished=[DateTime]::UtcNow.ToString('o');$summary|ConvertTo-Json -Depth 6|Set-Content "$Output\summary.json"
Get-Content "$Output\summary.json"
if(!$all){exit 1}
