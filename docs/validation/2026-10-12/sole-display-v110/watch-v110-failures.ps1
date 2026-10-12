$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render"
. ([scriptblock]::Create([IO.File]::ReadAllText("$r\vm\open-guest-console-lite.ps1")))
try{
 Invoke-Command -Session $nrLab -ScriptBlock {
  $ErrorActionPreference='Stop';$prefix='C:\npu-lab\v110-early';if(Test-Path "$prefix-records.json"){throw 'Evidence exists'}
  $seen=[Collections.Generic.HashSet[string]]::new();$saved=[Collections.Generic.List[object]]::new();$watch=[Diagnostics.Stopwatch]::StartNew()
  while($watch.ElapsedMilliseconds -lt 650000 -and $saved.Count -lt 8){
   foreach($path in [IO.Directory]::GetFiles('C:\npu-lab\umd-trace','draw-*.bin')){
    $stream=$null
    try{
     # Deny concurrent writers while copying a complete failed capture.
     $stream=[IO.File]::Open($path,'Open','Read','Read');$length=$stream.Length
     if($length -lt 48 -or $length -gt 33554432){continue}
     $stream.Position=$length-40;$footer=New-Object byte[] 40
     if($stream.Read($footer,0,40) -ne 40 -or [Text.Encoding]::ASCII.GetString($footer,0,8) -ne 'complete' -or [BitConverter]::ToUInt64($footer,32) -ne 0){continue}
     $stream.Position=0;$bytes=New-Object byte[] ([int]$length);$offset=0
     while($offset -lt $length){$n=$stream.Read($bytes,$offset,[int]$length-$offset);if(!$n){throw 'Capture truncated'};$offset+=$n}
     $sha=[Security.Cryptography.SHA256]::Create();try{$hash=[BitConverter]::ToString($sha.ComputeHash($bytes)).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
     if($seen.Add($hash)){
      $name='v110-early-'+[IO.Path]::GetFileNameWithoutExtension($path)+'-'+($saved.Count+1)+'.bin';[IO.File]::WriteAllBytes(('C:\npu-lab\'+$name),$bytes)
      $saved.Add([ordered]@{name=$name;bytes=$length;sha256=$hash;captured_at=[DateTime]::UtcNow.ToString('o')});$saved|ConvertTo-Json|Set-Content "$prefix-records.json"
     }
    }catch [IO.IOException]{}finally{if($stream){$stream.Dispose()}}
   }
   if(Test-Path C:\npu-lab\lab-v110-render-remove.json){break};Start-Sleep -Milliseconds 500
  }
  if(!$saved.Count){'[]'|Set-Content "$prefix-records.json"}
 }
 $records=Invoke-Command -Session $nrLab -ScriptBlock {Get-Content C:\npu-lab\v110-early-records.json -Raw|ConvertFrom-Json}
 foreach($record in $records){Copy-Item ('C:\npu-lab\'+$record.name) "$r\vm\$($record.name)" -FromSession $nrLab}
 Copy-Item C:\npu-lab\v110-early-records.json "$r\vm\v110-early-records.json" -FromSession $nrLab
 'Finite capture watcher finished'
}finally{Remove-PSSession $nrLab}
