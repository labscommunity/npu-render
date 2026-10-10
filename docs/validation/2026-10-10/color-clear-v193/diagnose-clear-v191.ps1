$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\npu-render";$src='C:\Users\tatef\AppData\Local\Temp\npu-graphics-probe-0146a2388f25';$out="$r\vm\clear-diagnostic-v191"
$s=Get-Content "$r\vm\indexed-dwm-stream-v98\report.json" -Raw|ConvertFrom-Json;if(!$s.completed -or !$s.configuration_removed){throw 'Stream active'}
if(Test-Path $out){throw 'Evidence exists'};New-Item -ItemType Directory $out|Out-Null
$exe="$r\builds\present-umd-v191\Release\nr_registered_color_clear_test.exe"
$old=$env:PATH;$env:PATH="$src\venv\Lib\site-packages\openvino\libs;$env:PATH"
try{
 $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$exe;$info.Arguments='NPU';$info.UseShellExecute=$false;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 $p=[Diagnostics.Process]::new();$p.StartInfo=$info;if(!$p.Start()){throw 'Launch failed'}
 $stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync();$timeout=$false
 if(!$p.WaitForExit(120000)){$timeout=$true;$p.Kill();$p.WaitForExit()}
 [IO.File]::WriteAllText("$out\stdout.jsonl",$stdout.GetAwaiter().GetResult());[IO.File]::WriteAllText("$out\stderr.log",$stderr.GetAwaiter().GetResult())
 [ordered]@{binary_sha256=(Get-FileHash $exe).Hash;exit_code=$p.ExitCode;timed_out=$timeout;full_build_passed=$false}|ConvertTo-Json|Set-Content "$out\report.json"
 Get-Content "$out\stdout.jsonl";Get-Content "$out\stderr.log";Get-Content "$out\report.json"
}finally{$env:PATH=$old}
