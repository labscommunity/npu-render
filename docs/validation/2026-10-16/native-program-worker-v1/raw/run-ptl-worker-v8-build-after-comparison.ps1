$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$vm="$env:LOCALAPPDATA\npu-render\vm";$deadline=[DateTime]::UtcNow.AddMinutes(12)
do {
 $proof=$null;try{$proof=Get-Content "$vm\ptl-program-comparison-v4-full\report.json" -Raw|ConvertFrom-Json}catch{}
 if($proof.completed){break}
 if([DateTime]::UtcNow -gt $deadline){throw 'Comparison wait deadline'}
 Start-Sleep -Seconds 2
}while($true)
if(!$proof.passed){throw 'Full program comparison failed'}
& ([scriptblock]::Create([IO.File]::ReadAllText("$vm\build-ptl-native-worker-v8.ps1")))
