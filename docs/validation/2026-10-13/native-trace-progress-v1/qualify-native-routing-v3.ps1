$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$trace="$env:ProgramData\T8-NpuRender-NativeTraceV1";$out="$r\vm\native-routing-v3-qualification"
if((Test-Path $trace) -or (Test-Path $out)){throw 'Diagnostic evidence exists'}
if(@(Get-Process nr_registered_local_indexed_worker,nr_registered_indexed_worker,nr_registered_indexed_stream_worker -ErrorAction SilentlyContinue).Count){throw 'Another renderer active'}
New-Item -ItemType Directory $trace|Out-Null
$acl=New-Object System.Security.AccessControl.DirectorySecurity;$acl.SetSecurityDescriptorSddlForm('O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)');Set-Acl $trace -AclObject $acl
try{
 & "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" "$r\vm\qualify-native-routing-v3.py" --worker "$r\builds\native-routing-v3\Release\nr_registered_local_indexed_worker.exe" --client "$r\builds\native-routing-v3\Release\nr_native_probe_ddi_test.exe" --runtime "$r\toolchains\openvino-2026.4.1\Lib\site-packages\openvino\libs" --cache "$r\vm\model-cache-2026-4-v209" --output $out --trace $trace
 if($LASTEXITCODE){throw 'Native diagnostic qualification failed'}
}finally{
 if(!(Test-Path $out)){New-Item -ItemType Directory $out|Out-Null}
 Copy-Item $trace "$out\native-trace" -Recurse
 $files=@(Get-ChildItem "$out\native-trace" -File|ForEach-Object {[ordered]@{name=$_.Name;bytes=$_.Length;sha256=(Get-FileHash $_.FullName).Hash}});$files|ConvertTo-Json -Depth 4|Set-Content "$out\trace-manifest.json"
 Remove-Item $trace -Recurse -Force
 [ordered]@{owned_directory_removed=!(Test-Path $trace);driver_installed=$false;desktop_changed=$false}|ConvertTo-Json|Set-Content "$out\cleanup.json"
}
