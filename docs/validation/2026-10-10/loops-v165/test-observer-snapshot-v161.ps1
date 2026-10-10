$ErrorActionPreference='Stop';$r="$env:LOCALAPPDATA\npu-render\vm";$tokens=$null;$errors=$null;$source="$r\observe_registered_desktop.ps1";$hash=(Get-FileHash $source).Hash
$ast=[Management.Automation.Language.Parser]::ParseFile($source,[ref]$tokens,[ref]$errors);if($errors.Count){throw 'Observer parse failed'}
$function=$ast.Find({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Read-NrLabLog'},$true);. ([scriptblock]::Create($function.Extent.Text))
$d="$r\observer-snapshot-v161";if(Test-Path $d){throw 'Evidence exists'};New-Item -ItemType Directory $d|Out-Null
Copy-Item $source "$d\observer.ps1";$p="$d\growing.log";[IO.File]::WriteAllText($p,'seed')
$job=Start-Job -ArgumentList $p -ScriptBlock {param($path)$s=[IO.File]::Open($path,'Append','Write','ReadWrite');try{[IO.File]::WriteAllText($path+'.ready','ready');$row=[Text.Encoding]::UTF8.GetBytes('{"growing":true}'+"`n");for($n=0;$n -lt 10000;$n++){$s.Write($row,0,$row.Length);$s.Flush();Start-Sleep -Milliseconds 1}}finally{$s.Dispose()}}
try{
 $ready=[Diagnostics.Stopwatch]::StartNew();while(!(Test-Path ($p+'.ready'))){if($ready.ElapsedMilliseconds -gt 10000){throw 'Writer not ready'};Start-Sleep -Milliseconds 10}
 $before=(Get-Item $p).Length;$w=[Diagnostics.Stopwatch]::StartNew()
 for($n=0;$n -lt 100;$n++){[void](Read-NrLabLog $p);Start-Sleep -Milliseconds 5};$elapsed=$w.ElapsedMilliseconds;$after=(Get-Item $p).Length
 if($elapsed -gt 10000 -or $after -le $before){throw 'Concurrent snapshot check failed'}
}finally{Stop-Job $job;Remove-Job $job}
$large="$d\oversize.log";$s=[IO.File]::Create($large);$s.SetLength(16777217);$s.Dispose();$rejected=$false;try{[void](Read-NrLabLog $large)}catch{$rejected=$_.Exception.Message -match 'snapshot limit'};if(!$rejected){throw 'Oversize log not rejected'}
if((Get-FileHash $source).Hash -ne $hash){throw 'Observer source changed during test'}
[ordered]@{snapshots=100;elapsed_ms=$elapsed;writer_bytes_before=$before;writer_bytes_after=$after;oversize_rejected=$rejected;observer_sha256=$hash;passed=$true}|ConvertTo-Json|Tee-Object "$d\report.json"
