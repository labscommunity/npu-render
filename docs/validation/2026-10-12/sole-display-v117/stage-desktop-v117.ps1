$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render"
. ([scriptblock]::Create([IO.File]::ReadAllText("$r\vm\open-guest-console-lite.ps1")))
try {
 $build=Get-Content "$r\builds\topology-duration-v31\build-report.json" -Raw|ConvertFrom-Json
 if(!$build.clean_build -or !$build.powershell_parser_passed){throw 'Topology build missing'}
 Invoke-Command -Session $nrLab -ScriptBlock {
  $ErrorActionPreference='Stop'
  . ([scriptblock]::Create([IO.File]::ReadAllText('C:\npu-lab\lab_device_identity.ps1')));Assert-NrLabGuest
  foreach($name in 'lab-v116-render-remove','lab-v9ax-readback-remove'){$a=Get-Content "C:\npu-lab\$name.json" -Raw|ConvertFrom-Json;if(!$a.completed -or !$a.passed){throw 'Previous cleanup incomplete'}}
  foreach($id in 'ROOT\NPURENDER_KMD_LAB','ROOT\NPURENDER_INDIRECT_LAB'){if(@(Get-NrLabDevice -HardwareId $id).Count){throw 'Custom device present'}}
  if(!(Test-Path C:\npu-lab\v116-after-cleanup.zip)){throw 'Archive missing'}
  foreach($package in 'driver-package-v116-compositor','driver-package-idd-readback-v9'){
   foreach($f in (Get-Content "C:\npu-lab\$package\sha256.json" -Raw|ConvertFrom-Json)){if((Get-FileHash "C:\npu-lab\$package\$($f.name)").Hash -ne $f.sha256){throw 'Package mismatch'}}
  }
  Move-Item C:\npu-lab\idd-trace C:\npu-lab\idd-trace-v9ax-preserved
  Move-Item C:\npu-lab\umd-trace C:\npu-lab\umd-trace-v116-preserved
  New-Item -ItemType Directory C:\npu-lab\umd-trace|Out-Null
  & icacls.exe C:\npu-lab\umd-trace /grant '*S-1-5-11:(OI)(CI)M' '*S-1-5-84-0-0-0-0-0:(OI)(CI)M' *> C:\npu-lab\umd-trace-v117-acl.log
  if($LASTEXITCODE){throw 'Trace ACL failed'}
 }
 Copy-Item "$r\builds\topology-duration-v31\Release\nr_indirect_topology_gui.exe" C:\npu-lab\nr_indirect_topology_gui_v31.exe -ToSession $nrLab
 if((Invoke-Command -Session $nrLab -ScriptBlock {(Get-FileHash C:\npu-lab\nr_indirect_topology_gui_v31.exe).Hash}) -ne $build.binary_sha256){throw 'Topology binary mismatch'}
 foreach($name in 'start_indirect_topology_task-v31.ps1','observe_registered_desktop-v31.ps1'){
  $source="$r\vm\topology-duration-v31\$name";Copy-Item $source "C:\npu-lab\$name" -ToSession $nrLab
  if((Get-FileHash $source).Hash -ne (Invoke-Command -Session $nrLab -ArgumentList $name -ScriptBlock {param($n)(Get-FileHash "C:\npu-lab\$n").Hash})){throw 'Harness mismatch'}
 }
 foreach($name in 'warm-indexed-v117.ps1'){
  Copy-Item "$r\vm\$name" "C:\npu-lab\$name" -ToSession $nrLab
  if((Get-FileHash "$r\vm\$name").Hash -ne (Invoke-Command -Session $nrLab -ArgumentList $name -ScriptBlock {param($n)(Get-FileHash "C:\npu-lab\$n").Hash})){throw 'Warmup mismatch'}
 }
 $cases=Invoke-Command -Session $nrLab -ScriptBlock {
  $ErrorActionPreference='Stop';if([Diagnostics.Process]::GetCurrentProcess().SessionId -ne 0){throw 'Parser qualification requires noninteractive management session'}
  $result=@()
  foreach($n in @(0,4,5,45,420,421,1500,1501)){
   $name="idd-topology-v31-parser-$n";$path="C:\npu-lab\$name.jsonl";if(Test-Path $path){throw 'Parser evidence exists'}
   $proc=Start-Process C:\npu-lab\nr_indirect_topology_gui_v31.exe -ArgumentList @('--report',$name,'--seconds',"$n") -PassThru
   if(!$proc.WaitForExit(10000)){$proc.Kill();throw 'Noninteractive parser test timed out'}
   $accepted=$n -ge 5 -and $n -le 1500;$created=Test-Path $path
   if($proc.ExitCode -ne 2 -or $created -ne $accepted){throw "Unexpected parser result for $n"}
   if($created -and (Get-Content $path -Raw) -notmatch '"event":"probe_started"'){throw 'Missing parser receipt'}
   $result+=[ordered]@{seconds=$n;duration_accepted=$accepted;report_created=$created;noninteractive_guard_exit_code=$proc.ExitCode;passed=$true};$proc.Dispose()
  }
  $result|ConvertTo-Json -Depth 4|Set-Content C:\npu-lab\topology-duration-v31-parser.json
  $result
 }
 Copy-Item C:\npu-lab\topology-duration-v31-parser.json "$r\vm\topology-duration-v31-parser.json" -FromSession $nrLab
 [ordered]@{trial='v117';staged=$true;package_reused_from='v116';package_version='0.0.103.0';topology_binary_sha256=$build.binary_sha256;parser_cases=$cases;installed=$false}|ConvertTo-Json -Depth 6|Set-Content "$r\vm\v117-staging.json"
 'v117 staged; parser boundary and noninteractive guard checks passed'
}finally{Remove-PSSession $nrLab}
