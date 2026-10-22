# Samples GPU 3D engine utilisation, GPU memory, CPU (total, dwm, NPU worker processes) and
# NPU engine utilisation beside a trial, one JSON line per sample. Only counters that exist on
# the host are read; a missing counter is recorded once and skipped. This is a measurement of
# where the host's engines are busy, never proof of NPU execution (that comes from the worker's
# provider receipts).
param([string]$Output, [int]$Seconds = 900, [int]$Interval = 2)
$cores=[Environment]::ProcessorCount
$paths=@('\GPU Engine(*engtype_3D)\Utilization Percentage','\GPU Adapter Memory(*)\Total Committed','\Processor(_Total)\% Processor Time','\Process(dwm)\% Processor Time','\Process(nr_registered_local_indexed_worker*)\% Processor Time','\Process(WUDFHost*)\% Processor Time','\NPU Engine(*)\Utilization Percentage')
$available=@(); $missing=@()
foreach($p in $paths){ try { $null=Get-Counter $p -MaxSamples 1 -ErrorAction Stop; $available+=$p } catch { $missing+=$p } }
Add-Content $Output ("{""t"":""$([DateTime]::UtcNow.ToString('o'))"",""sampler"":""engine_sampler"",""cores"":$cores,""interval_s"":$Interval,""missing_counters"":[" + (($missing | ForEach-Object { '"' + ($_ -replace '\\','/') + '"' }) -join ',') + "]}")
$end=[DateTime]::UtcNow.AddSeconds($Seconds)
while([DateTime]::UtcNow -lt $end){
  try {
    $s=Get-Counter $available -SampleInterval $Interval -MaxSamples 1 -ErrorAction Stop
    $c=$s.CounterSamples
    $gpu=($c | Where-Object { $_.Path -like '*engtype_3D*' } | Measure-Object CookedValue -Sum).Sum
    $mem=($c | Where-Object { $_.Path -like '*Total Committed*' } | Measure-Object CookedValue -Sum).Sum
    $cpu=($c | Where-Object { $_.Path -like '*processor(_total)*' } | Measure-Object CookedValue -Sum).Sum
    $dwm=($c | Where-Object { $_.Path -like '*process(dwm)*' } | Measure-Object CookedValue -Sum).Sum
    $worker=($c | Where-Object { $_.Path -like '*nr_registered_local_indexed_worker*' } | Measure-Object CookedValue -Sum).Sum
    $host_=($c | Where-Object { $_.Path -like '*process(wudfhost*' } | Measure-Object CookedValue -Sum).Sum
    $npu=($c | Where-Object { $_.Path -like '*npu engine*' } | Measure-Object CookedValue -Sum).Sum
    $top=($c | Where-Object { $_.Path -like '*engtype_3D*' -and $_.CookedValue -gt 1 } | Sort-Object CookedValue -Descending | Select-Object -First 3 | ForEach-Object { "$(($_.InstanceName -split '_')[1])=$([math]::Round($_.CookedValue,1))" }) -join ','
    # Process counters sum over cores (0..100*cores); they are reported as a share of all cores.
    Add-Content $Output ("{""t"":""$([DateTime]::UtcNow.ToString('o'))"",""gpu_3d_percent_sum"":$([math]::Round($gpu,2)),""gpu_committed_bytes"":$([int64]$mem),""cpu_total_percent"":$([math]::Round($cpu,2)),""dwm_cpu_percent_of_all_cores"":$([math]::Round($dwm/$cores,2)),""worker_cpu_percent_of_all_cores"":$([math]::Round($worker/$cores,2)),""indirect_host_cpu_percent_of_all_cores"":$([math]::Round($host_/$cores,2)),""npu_percent_sum"":$([math]::Round($npu,2)),""top_pids"":""$top""}")
  } catch { Add-Content $Output ("{""t"":""$([DateTime]::UtcNow.ToString('o'))"",""error"":""$($_.Exception.Message -replace '"','''')""}"); Start-Sleep -Seconds $Interval }
}
