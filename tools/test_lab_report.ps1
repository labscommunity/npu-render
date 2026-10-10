param([Parameter(Mandatory=$true)][string]$Helper)
$ErrorActionPreference='Stop'
. $Helper
$directory=Join-Path ([IO.Path]::GetTempPath()) ('nr-report-test-'+[Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($directory)|Out-Null
$path=Join-Path $directory 'report.json';$reader=$null
try {
 Write-NrLabReport $path @{phase='old'}
 # Match a reader that denies replacement, as happened during live recovery.
 $reader=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
 $cleaned=$false
 try {throw 'simulated trial failure'}catch {}finally {
  Write-NrLabReport $path @{phase='cleanup'} -BestEffort
  $cleaned=$true
 }
 if(!$cleaned -or ((Get-Content $path -Raw|ConvertFrom-Json).phase -ne 'old')){throw 'Cleanup or previous evidence lost'}
 $reader.Dispose();$reader=$null
 Write-NrLabReport $path @{phase='completed'}
 if((Get-Content $path -Raw|ConvertFrom-Json).phase -ne 'completed'){throw 'Status did not recover'}
 if(@(Get-ChildItem $directory -Filter '*.tmp').Count){throw 'Temporary files leaked'}
 '{"sharing_failure_reproduced":true,"cleanup_completed":true,"status_recovered":true}'
} finally {if($reader){$reader.Dispose()};[IO.Directory]::Delete($directory,$true)}
