$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render"
$worker=Get-Process -Id 8284 -ErrorAction SilentlyContinue;if($worker -and !$worker.HasExited){throw 'Owned trial worker still active'}
$zip="$r\vm\v113-after-cleanup.zip";Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive=[IO.Compression.ZipFile]::OpenRead($zip)
try{$entry=$archive.Entries|Where-Object{$_.FullName -eq 'lab-v113-render-observation.json'};if(!$entry){throw 'Missing observation'};$reader=[IO.StreamReader]::new($entry.Open());try{$observation=$reader.ReadToEnd()|ConvertFrom-Json}finally{$reader.Dispose()};if(!$observation.completed -or !$observation.cleanup_passed){throw 'Guest cleanup incomplete'}}finally{$archive.Dispose()}
$script=[IO.File]::ReadAllText("$r\vm\replay-v113-failures.ps1")
$guard='$stream=Get-Content "$r\vm\indexed-dwm-stream-v113\report.json" -Raw|ConvertFrom-Json'+"`n"+'if(!$stream.completed -or !$stream.configuration_removed){throw ''Desktop stream active''}'
if(!$script.Contains($guard)){throw 'Unexpected replay script'}
& ([scriptblock]::Create($script.Replace($guard,'')))
