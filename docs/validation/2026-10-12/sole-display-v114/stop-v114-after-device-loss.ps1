$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$out="$r\vm\v114-early-stop.json"
if(Test-Path $out){throw 'Evidence exists'}
$s=Get-Content "$r\vm\indexed-dwm-stream-v114\report.json" -Raw|ConvertFrom-Json
if($s.worker_pid -ne 6720 -or $s.completed){throw 'Trial identity changed'}
$stream=[IO.File]::Open("$r\vm\indexed-dwm-stream-v114\worker.stderr.log",'Open','Read','ReadWrite')
try{$reader=[IO.StreamReader]::new($stream);$errorText=$reader.ReadToEnd()}finally{$reader.Dispose()}
if($errorText -notmatch 'ZE_RESULT_ERROR_DEVICE_LOST'){throw 'Expected recorded NPU device loss'}
$report=[ordered]@{started=[DateTime]::UtcNow.ToString('o');reason='recorded NPU device loss';physical_driver_installation=$false;physical_machine_restart=$false;worker_pid=6720}
$p=Get-Process -Id 6720 -ErrorAction SilentlyContinue
if($p -and !$p.HasExited){
 $actual=[IO.Path]::GetFullPath($p.Path);$expected=[IO.Path]::GetFullPath("$r\builds\present-umd-v223\Release\nr_registered_indexed_stream_worker.exe")
 if($actual -ne $expected -or $p.StartTime.ToUniversalTime() -lt [DateTime]::Parse($s.started).ToUniversalTime()){throw 'Worker identity changed'}
 $p.Kill();$report.worker_kill_requested=$true;$report.worker_exit_wait_completed=$p.WaitForExit(5000)
}else{$report.worker_already_exited=$true}
$report|ConvertTo-Json|Set-Content $out
. ([scriptblock]::Create([IO.File]::ReadAllText("$r\vm\open-guest-console-lite.ps1")))
try{
 $report.guest=Invoke-Command -Session $nrLab -ScriptBlock {
  $ErrorActionPreference='Stop';. C:\npu-lab\lab_device_identity.ps1;Assert-NrLabGuest
  $result=@()
  foreach($item in @(@('lab-v9av-readback','ROOT\NPURENDER_INDIRECT_LAB','0.0.9.0'),@('lab-v114-render','ROOT\NPURENDER_KMD_LAB','0.0.102.0'))){
   $saved=Get-Content "C:\npu-lab\$($item[0])-owned.json" -Raw|ConvertFrom-Json;$current=@(Get-NrLabDevice -HardwareId $item[1])
   if($current.Count -ne 1 -or $current[0].instance -ne $saved.instance -or $current[0].inf -ne $saved.inf -or $saved.version -ne $item[2]){throw 'Owned guest device changed'}
   & pnputil.exe /disable-device $saved.instance *> "C:\npu-lab\v114-early-disable-$($item[0]).log";$code=$LASTEXITCODE
   $result+=@{name=$item[0];instance=$saved.instance;exit_code=$code;after=@(Get-NrLabDevice -HardwareId $item[1])};$result|ConvertTo-Json -Depth 6|Set-Content C:\npu-lab\v114-early-disable.json
   if($code -ne 0){throw 'Owned guest device disable failed'}
  }
  return $result
 }
}finally{Remove-PSSession $nrLab;$report.finished=[DateTime]::UtcNow.ToString('o');$report|ConvertTo-Json -Depth 8|Set-Content $out}
$report|ConvertTo-Json -Depth 8
