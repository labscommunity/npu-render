$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render"
$old=Get-ExecutionPolicy -Scope Process
try{Set-ExecutionPolicy -Scope Process Bypass -Force;& "$r\vm\package-present-v111.ps1" -SourceRevision 'b0784b7071360f2765ac61c7e93f4cbb3ecab389'}finally{Set-ExecutionPolicy -Scope Process $old -Force}
