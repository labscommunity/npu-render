# Summarises an engine_sampler.ps1 output file: percentiles of each engine share and the top samples.
param([Parameter(Mandatory=$true)][string]$Samples,[string]$Output='')
$g=@(Get-Content $Samples | ForEach-Object { try { $_ | ConvertFrom-Json } catch {} } | Where-Object { $null -ne $_.gpu_3d_percent_sum })
function Pct($values,$q){ $s=@($values | Sort-Object); if(!$s.Count){return $null}; return $s[[Math]::Min($s.Count-1,[int]($s.Count*$q))] }
$summary=[ordered]@{samples=$g.Count;first=$(if($g.Count){$g[0].t});last=$(if($g.Count){$g[-1].t})}
foreach($k in @('gpu_3d_percent_sum','cpu_total_percent','dwm_cpu_percent_of_all_cores','worker_cpu_percent_of_all_cores','indirect_host_cpu_percent_of_all_cores','npu_percent_sum')){ $v=@($g | ForEach-Object { $_.$k }); $summary[$k]=[ordered]@{avg=[math]::Round(($v|Measure-Object -Average).Average,2);p50=(Pct $v 0.5);p90=(Pct $v 0.9);p99=(Pct $v 0.99);max=($v|Measure-Object -Maximum).Maximum} }
$summary.top_gpu=@($g | Sort-Object gpu_3d_percent_sum -Descending | Select-Object -First 3 | ForEach-Object { "$($_.t) $($_.gpu_3d_percent_sum) $($_.top_pids)" })
$json=$summary|ConvertTo-Json -Depth 4;if($Output){Set-Content $Output $json};[Console]::Out.WriteLine($json)
