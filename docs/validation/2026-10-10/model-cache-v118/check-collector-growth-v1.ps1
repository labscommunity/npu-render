$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue'
$r="$env:LOCALAPPDATA\npu-render";$out="$r\vm\collector-growth-v1";if(Test-Path $out){throw 'Evidence exists'};New-Item -ItemType Directory $out|Out-Null
Add-Type -TypeDefinition @'
using System.IO;using System.Threading;using System.Threading.Tasks;
public static class TopologyGrowthFixture {
 public static Task Write(string path) {return Task.Run(()=>{
  using(var file=new FileStream(path,FileMode.CreateNew,FileAccess.Write,FileShare.ReadWrite|FileShare.Delete))
  using(var writer=new StreamWriter(file,new System.Text.UTF8Encoding(false))){
   for(int i=0;i<70;i++){writer.Write("{\"index\":");writer.Flush();Thread.Sleep(2);writer.Write(i.ToString()+"}\n");writer.Flush();Thread.Sleep(2);}
  }
 });}
}
'@
function Read-Complete([string]$path){
     $topologyStream=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,
      ([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
     try {
      $topologyLength=$topologyStream.Length
      if($topologyLength -gt 65536){throw 'Topology log exceeds snapshot limit'}
      $topologyBytes=New-Object byte[] ([int]$topologyLength);$topologyOffset=0
      while($topologyOffset -lt $topologyBytes.Length){
       $copied=$topologyStream.Read($topologyBytes,$topologyOffset,$topologyBytes.Length-$topologyOffset)
       if(!$copied){throw 'Topology snapshot truncated'};$topologyOffset+=$copied
      }
      $text=[Text.Encoding]::UTF8.GetString($topologyBytes)
     } finally {$topologyStream.Dispose()}
     # The native writer appends complete JSON lines. Ignore a concurrently
     # written final fragment rather than parsing a partial record.
     $end=$text.LastIndexOf("`n")
     if($end -ge 0){$text=$text.Substring(0,$end+1)}else{$text=''}

 return $text
}
$path="$out\topology.jsonl";$writer=[TopologyGrowthFixture]::Write($path)
while(!(Test-Path $path)){Start-Sleep -Milliseconds 1}
$maximum=0
for($i=0;$i -lt 100;$i++){
 $text=Read-Complete $path;if($text -isnot [string]){throw 'Snapshot is not a scalar string'}
 $rows=@($text -split "`n"|Where-Object {$_}|ForEach-Object {$_|ConvertFrom-Json})
 for($n=0;$n -lt $rows.Count;$n++){if($rows[$n].index -ne $n){throw 'Incomplete or reordered snapshot'}}
 $maximum=[Math]::Max($maximum,$rows.Count);Start-Sleep -Milliseconds 2
}
$writer.GetAwaiter().GetResult()
$final=@((Read-Complete $path) -split "`n"|Where-Object {$_}|ForEach-Object {$_|ConvertFrom-Json})
if($final.Count -ne 70){throw 'Missing final rows'}
[ordered]@{concurrent_snapshots=100;final_rows=70;maximum_observed_rows=$maximum;scalar_strings=$true;all_snapshots_complete_json=$true;desktop_offload_tested=$false}|ConvertTo-Json|Tee-Object "$out\report.json"
