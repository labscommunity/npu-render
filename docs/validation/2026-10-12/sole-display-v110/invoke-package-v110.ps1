$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render"
$old=Get-ExecutionPolicy -Scope Process
try{Set-ExecutionPolicy -Scope Process Bypass -Force;& "$r\vm\package-present-v110.ps1" -SourceRevision 'd91aac36d37f1f5b6145f169f06d7c931c358246'}finally{Set-ExecutionPolicy -Scope Process $old -Force}
