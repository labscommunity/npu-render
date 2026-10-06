param(
    [Parameter(Mandatory=$true)][string]$OutputPath,
    [Parameter(Mandatory=$true)][int[]]$ProcessIds,
    [int]$Seconds = 300
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$writer = [System.IO.StreamWriter]::new($OutputPath, $false)
try {
    Get-Counter '\GPU Engine(*)\Utilization Percentage' -SampleInterval 1 -MaxSamples $Seconds |
        ForEach-Object {
            $sample = $_
            $engines = @($sample.CounterSamples | ForEach-Object {
                if ($_.InstanceName -match '^pid_(\d+)_(.+)_engtype_(.+)$') {
                    $samplePid = [int]$Matches[1]
                    if ($ProcessIds -contains $samplePid) {
                        @{pid=$samplePid; adapter_engine=$Matches[2]; engine_type=$Matches[3];
                          utilization_percent=$_.CookedValue; counter_status=$_.Status}
                    }
                }
            })
            $processes = @(Get-Process -Id $ProcessIds -ErrorAction SilentlyContinue |
                ForEach-Object { @{pid=$_.Id; cpu_seconds=$_.CPU; working_set_bytes=$_.WorkingSet64} })
            $writer.WriteLine((@{at_utc=$sample.Timestamp.ToUniversalTime().ToString('o');
                engines=$engines; processes=$processes} | ConvertTo-Json -Depth 5 -Compress))
            $writer.Flush()
        }
} finally {
    $writer.Dispose()
}
