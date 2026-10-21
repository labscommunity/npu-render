$r="$env:LOCALAPPDATA\npu-render";$vm="$r\vm"
Set-ExecutionPolicy -Scope Process Bypass -Force
function Step($name,$block){ $sw=[Diagnostics.Stopwatch]::StartNew(); [Console]::Out.WriteLine("=== $name"); try { & $block } catch { [Console]::Out.WriteLine("STEP FAILED ($name) after $([int]$sw.Elapsed.TotalSeconds)s: $($_.Exception.Message)"); throw }; [Console]::Out.WriteLine("--- $name ok in $([int]$sw.Elapsed.TotalSeconds)s") }
function Tail($text,$n){ $t=($text|Out-String -Width 300).Trim(); if($t.Length -gt $n){ $t=$t.Substring($t.Length-$n) }; [Console]::Out.WriteLine($t) }
try {
Step "stop saver" { try { & "$vm\stop_screen_saver.ps1" *>&1 | Out-String -Width 300 | ForEach-Object { [Console]::Out.WriteLine($_.Trim()) } } catch { [Console]::Out.WriteLine("saver stop: $($_.Exception.Message)") } }
Step "control v15" { Tail (& "$vm\build-native-topology-control-v15.ps1" *>&1) 1200; $j=Get-Content "$r\builds\native-topology-control-v15\build-report.json" -Raw|ConvertFrom-Json; if(!$j.clean_build){ throw 'control v15 not clean' } }
Step "present v24" { Tail (& "$vm\build-native-present-v24.ps1" *>&1) 1200; $j=Get-Content "$r\builds\native-present-v24\build-report.json" -Raw|ConvertFrom-Json; if(!$j.clean_build){ throw 'present v24 not clean' } }
Step "observer v7" { Tail (& "$vm\build-native-output-observer-v7.ps1" *>&1) 1200; $j=Get-Content "$r\builds\native-output-observer-v7\build-report.json" -Raw|ConvertFrom-Json; if(!$j.clean_build){ throw 'observer v7 not clean' } }
Step "frame reader v15" { Tail (& "$r\toolchains\openvino-2026.4.1\Scripts\python.exe" "$vm\qualify-live-frame-reader-v1.py" --exe "$r\builds\native-present-v24\Release\nr_frame_present_probe.exe" --output "$vm\live-frame-reader-v15" 2>&1) 600; $j=Get-Content "$vm\live-frame-reader-v15\report.json" -Raw|ConvertFrom-Json; if(!$j.passed){ throw 'reader v15 failed' } }
Step "present v24 input qualification" { Tail (& "$vm\qualify-native-input-v24.ps1" *>&1) 1500; $j=Get-Content "$vm\native-present-v24-qualification\report.json" -Raw|ConvertFrom-Json; if(!$j.passed){ throw 'input qualification v24 failed' } }
Step "observer v7 qualification" { Tail (& "$vm\qualify-native-output-observer-v7.ps1" *>&1) 1500; $j=Get-Content "$vm\native-output-observer-v7-qualification\report.json" -Raw|ConvertFrom-Json; if(!$j.passed){ throw 'observer qualification v7 failed' } }
Step "cursor guard v7" { Tail (& "$vm\qualify-native-cursor-v7.ps1" *>&1) 2000; $j=Get-Content "$vm\native-cursor-guard-v7-qualification\report.json" -Raw|ConvertFrom-Json; if(!$j.passed -or !$j.foreground_input_passed -or !$j.foreground_release_reacquired){ throw 'cursor guard v7 failed' } }
[Console]::Out.WriteLine("CHAIN COMPLETE")
} catch { [Console]::Out.WriteLine("CHAIN STOPPED: $($_.Exception.Message)") }
