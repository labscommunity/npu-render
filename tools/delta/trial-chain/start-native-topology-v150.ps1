$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$out="$r\vm\native-topology-v150";$source="$env:LOCALAPPDATA\Temp\npu-graphics-probe-0146a2388f25";$task='T8-NpuRender-InspectTopology-V150'
if(Test-Path $out){throw 'Observation exists'};New-Item -ItemType Directory $out|Out-Null
@{command=@("$source\venv\Scripts\python.exe","$r\vm\observe_native_topology_v8.py",$out,"$r\builds\native-present-v6\Release\nr_frame_present_probe.exe","$r\builds\native-present-v6\build-report.json");log="$out\runner.log"}|ConvertTo-Json -Depth 4|Set-Content -Encoding UTF8 "$out\job.json"
$action=New-ScheduledTaskAction -Execute "$source\venv\Scripts\pythonw.exe" -Argument ('"'+$source+'\native_runner.py" "'+$out+'\job.json"')
$principal=New-ScheduledTaskPrincipal -UserId ([Security.Principal.WindowsIdentity]::GetCurrent().User.Value) -LogonType Interactive -RunLevel Limited
$settings=New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 42) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
Register-ScheduledTask -TaskName $task -Action $action -Principal $principal -Settings $settings|Out-Null;Start-ScheduledTask -TaskName $task
$deadline=[DateTime]::UtcNow.AddSeconds(15)
while(!(Test-Path "$out\samples.jsonl") -and !(Test-Path "$out\report.json") -and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 200}
if(!(Test-Path "$out\samples.jsonl")){if(Test-Path "$out\report.json"){Get-Content "$out\report.json"};throw 'Observer did not start'}
$first=Get-Content "$out\samples.jsonl" -First 1|ConvertFrom-Json;if($first.exit_code){throw 'Observer failed'};$first|ConvertTo-Json -Depth 5
