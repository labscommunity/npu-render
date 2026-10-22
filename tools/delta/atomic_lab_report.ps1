# Atomic status replacement. Readers may keep the previous complete document;
# a reporting failure must never prevent an owned laboratory cleanup action.
function Write-NrLabReport {
 param([Parameter(Mandatory=$true)][string]$Path,[Parameter(Mandatory=$true)]$Report,[switch]$BestEffort)
 $temporary=$Path+'.'+[Guid]::NewGuid().ToString('N')+'.tmp'
 try {
  [IO.File]::WriteAllText($temporary,($Report|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
  for($attempt=0;$attempt -lt 8;$attempt++){
   try {
    if([IO.File]::Exists($Path)){[IO.File]::Replace($temporary,$Path,[System.Management.Automation.Language.NullString]::Value)}else{[IO.File]::Move($temporary,$Path)}
    return
   } catch [IO.IOException] {if($attempt -eq 7){throw};Start-Sleep -Milliseconds 50}
  }
 } catch {
  if(!$BestEffort){throw}
  Write-Warning ('Laboratory status write failed; cleanup continues: '+$_.Exception.Message)
 } finally {
  if([IO.File]::Exists($temporary)){
   try {[IO.File]::Delete($temporary)}catch {if(!$BestEffort){throw};Write-Warning 'Laboratory temporary report cleanup failed'}
  }
 }
}
