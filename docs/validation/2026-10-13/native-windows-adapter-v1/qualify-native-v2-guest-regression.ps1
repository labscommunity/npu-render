$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$b="$r\builds\native-probe-driver-v2";$log="$r\vm\native-v2-guest-regression-build.log"
$ErrorActionPreference='Continue'; & cmake --build $b --config Release --target nr_fill_umd nr_registered_lab_test *> $log;$code=$LASTEXITCODE;$ErrorActionPreference='Stop';Get-Content $log -Tail 8;if($code){throw 'Default guest variant build failed'}
$env:PATH="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs;$env:PATH"
$ErrorActionPreference='Continue'; & "$b\Release\nr_registered_lab_test.exe" CPU 1> "$r\vm\native-v2-guest-regression.stdout.jsonl" 2> "$r\vm\native-v2-guest-regression.stderr.log";$code=$LASTEXITCODE;$ErrorActionPreference='Stop'
[ordered]@{passed=($code -eq 0);exit_code=$code;default_guest_variant_built=$true;cpu_contract_only=$true;npu_tested=$false;finished=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json|Set-Content "$r\vm\native-v2-guest-regression-report.json"
Get-Content "$r\vm\native-v2-guest-regression-report.json";if($code){throw 'Default guest CPU contracts failed'}
