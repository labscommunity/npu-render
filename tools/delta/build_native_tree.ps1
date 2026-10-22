param([Parameter(Mandatory=$true)][string]$Name,[string[]]$Targets=@('nr_registered_local_indexed_worker','nr_native_probe_umd','nr_native_probe_ddi_test','nr_native_probe_lease_test','nr_native_trace_lifecycle_test','nr_native_registered_probe'))
# Builds the repository's native tree (uploaded as $vm\<Name>.tar.gz containing native/) with the pinned OpenVINO SDK and WDK headers.
$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$s="$r\vm\$Name";$b="$r\builds\$Name";$sdk="$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino";$log="$r\vm\build-$Name.log"
if(Test-Path $s){Remove-Item $s -Recurse -Force};if(Test-Path $b){Remove-Item $b -Recurse -Force}
New-Item -ItemType Directory $s|Out-Null
& tar -xzf "$r\vm\$Name.tar.gz" -C $s;if($LASTEXITCODE){throw 'Extraction failed'}
$env:CMAKE_BUILD_PARALLEL_LEVEL='8';$ErrorActionPreference='Continue'
& cmake -S "$s\native\windows\registered_lab" -B $b -A x64 "-DOpenVINO_DIR=$sdk\cmake" "-DNR_WDK_INCLUDE_ROOT=$r\dependencies\wdk-26100.6584-headers" *> $log;$code=$LASTEXITCODE
if(!$code){& cmake --build $b --config Release --target @Targets *>> $log;$code=$LASTEXITCODE}
$ErrorActionPreference='Stop'
$errors=@(Select-String -Path $log -Pattern 'error [A-Z]+[0-9]+|fatal error|CMake Error' | Select-Object -First 25 | ForEach-Object { $_.Line.Trim() })
$hashes=[ordered]@{};foreach($t in $Targets){ $exe=Get-ChildItem "$b\Release" -Filter "$t.*" -ErrorAction SilentlyContinue | Where-Object { $_.Extension -in '.exe','.dll' } | Select-Object -First 1; if($exe){ $hashes[$exe.Name]=(Get-FileHash $exe.FullName).Hash } }
[ordered]@{name=$Name;clean_build=($code -eq 0);exit_code=$code;binary_sha256=$hashes;errors=$errors;finished=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json -Depth 4|Set-Content "$b\build-report.json"
Get-Content "$b\build-report.json" -Raw
if($code){exit 1}
