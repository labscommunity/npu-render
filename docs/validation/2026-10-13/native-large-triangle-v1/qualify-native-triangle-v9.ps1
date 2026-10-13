$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$b="$r\builds\native-routing-v9";$out="$r\vm\native-triangle-v9-regression"
if(Test-Path $out){throw 'Regression evidence exists'};New-Item -ItemType Directory $out|Out-Null
$ErrorActionPreference='Continue'; & cmake --build $b --config Release --target nr_registered_triangle_texture_draw_test *> "$out\build.log";$code=$LASTEXITCODE;$ErrorActionPreference='Stop';if($code){throw 'Linear regression build failed'}
$env:PATH="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs;$env:PATH";$env:NR_RENDER_NPU_CACHE="$r\vm\model-cache-2026-4-v209";$env:NR_RENDER_SHARED_QUEUE='1';$env:NR_RENDER_MEMORY_CACHE='1'
$results=@()
foreach($backend in @('CPU','NPU')){foreach($mode in @('default','capacity')){
 $arguments=@($backend);if($mode -ne 'default'){$arguments+=('--'+$mode)};$name=$backend+'-'+$mode
 $p=Start-Process "$b\Release\nr_registered_triangle_texture_draw_test.exe" -ArgumentList $arguments -PassThru -NoNewWindow -RedirectStandardOutput "$out\$name.stdout.jsonl" -RedirectStandardError "$out\$name.stderr.log";[void]$p.Handle
 if(!$p.WaitForExit(120000)){$p.Kill();$p.WaitForExit();throw 'Owned regression timed out'};$p.Refresh();$code=$p.ExitCode;$p.Dispose();$results+=@{backend=$backend;mode=$mode;exit_code=$code};$results|ConvertTo-Json|Set-Content "$out\cases.json"
 if($code){Get-Content "$out\$name.stderr.log";throw 'Linear regression failed'}
}}
@{passed=$true;cases=$results;binary_sha256=(Get-FileHash "$b\Release\nr_registered_triangle_texture_draw_test.exe").Hash;desktop_changed=$false}|ConvertTo-Json -Depth 5|Set-Content "$out\report.json";Get-Content "$out\report.json"
