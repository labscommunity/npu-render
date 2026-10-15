param(
    [Parameter(Mandatory=$true)][string]$OutputPath,
    [int[]]$ProcessIds = @(),
    [ValidateRange(1,86400)][int]$Seconds = 300,
    [ValidateRange(1,60)][int]$SampleIntervalSeconds = 1,
    [string]$StopFile = ''
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$started = (Get-Date).ToUniversalTime().ToString('o')
$sampleIndex = 0
$counterErrors = @()
$fatalError = $null
$stoppedByRequest = $false
$writer = [System.IO.StreamWriter]::new($OutputPath, $false)
try {
    # Get-Counter expands wildcard instances once per invocation. Reopen for
    # each sample so applications launched during the capture are discovered.
    # Each invocation takes its own initial reading before the timed sample.
    for ($attempt = 0; $attempt -lt $Seconds; $attempt++) {
        if ($StopFile -and (Test-Path -LiteralPath $StopFile)) {
            $stoppedByRequest = $true
            break
        }
        $queryStarted = (Get-Date).ToUniversalTime().ToString('o')
        Get-Counter '\GPU Engine(*)\Utilization Percentage' -SampleInterval $SampleIntervalSeconds -MaxSamples 1 -ErrorAction SilentlyContinue -ErrorVariable +counterErrors |
        ForEach-Object {
            $sample = $_
            $sampleIndex += 1
            $engines = @($sample.CounterSamples | ForEach-Object {
                if ($_.InstanceName -match '^pid_(\d+)_(.+)_engtype_(.*)$') {
                    $samplePid = [int]$Matches[1]
                    if ($ProcessIds.Count -eq 0 -or $ProcessIds -contains $samplePid) {
                        $value = $_.CookedValue
                        if ([double]::IsNaN($value) -or [double]::IsInfinity($value)) {$value = $null}
                        @{pid=$samplePid; adapter_engine=$Matches[2]; engine_type=$Matches[3];
                          utilization_percent=$value; counter_status=$_.Status;
                          raw_value=$_.RawValue; second_value=$_.SecondValue}
                    }
                }
            })
            $sampleIds = @($engines | ForEach-Object {$_.pid} | Sort-Object -Unique)
            $processes = @()
            if ($sampleIds.Count -gt 0) {
                $processes = @(Get-Process -Id $sampleIds -ErrorAction SilentlyContinue | ForEach-Object {
                    $startUtc = $null
                    try {$startUtc = $_.StartTime.ToUniversalTime().ToString('o')} catch {}
                    @{pid=$_.Id; name=$_.ProcessName; start_utc=$startUtc;
                      session_id=$_.SessionId; cpu_seconds=$_.CPU; working_set_bytes=$_.WorkingSet64}
                })
            }
            $writer.WriteLine((@{schema_version=2; sample_index=$sampleIndex;
                at_utc=$sample.Timestamp.ToUniversalTime().ToString('o');
                query_started_utc=$queryStarted; measurement_interval_seconds=$SampleIntervalSeconds;
                total_counter_instances=$sample.CounterSamples.Count;
                engines=$engines; processes=$processes} | ConvertTo-Json -Depth 6 -Compress))
            $writer.Flush()
        }
    }
    if (-not $stoppedByRequest -and $sampleIndex -ne $Seconds) {throw "Expected $Seconds samples; captured $sampleIndex"}
} catch {
    $fatalError = $_.Exception.Message
    throw
} finally {
    $writer.Dispose()
    @{schema_version=2; started_utc=$started; ended_utc=(Get-Date).ToUniversalTime().ToString('o');
      requested_samples=$Seconds; sample_interval_seconds=$SampleIntervalSeconds;
      samples=$sampleIndex; stopped_by_request=$stoppedByRequest; process_filter=$ProcessIds; fatal_error=$fatalError;
      counter_errors=@($counterErrors | ForEach-Object {$_.Exception.Message});
      discovery='Wildcard instances are refreshed before every independently primed sample. Short-lived processes and gaps between queries can be missed.';
      scope='GPU Engine counters and process names only; no window contents or command lines';
      note='Seconds is the sample count for backward compatibility; elapsed time may be longer. Counter statuses 0 and 1 are valid; other values are retained for rejection.'
    } | ConvertTo-Json -Depth 5 | Set-Content ($OutputPath + '.metadata.json') -Encoding UTF8
}
