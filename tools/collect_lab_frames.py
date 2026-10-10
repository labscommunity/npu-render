"""Read completed IDD frames through existing SSH/WinRM management sessions.

Only the disposable NR-GFX-LAB guest is admitted. This adds no guest listener,
changes no drivers and performs no rendering. The local latest file is replaced
only after a complete, validated frame arrives. Run concurrently with a trial.
"""

import argparse
import base64
import hashlib
import json
import re
import subprocess
import threading
from pathlib import Path

from npu_render.indirect_frame import MAX_BYTES, parse_frame

SCRIPT = r"""
$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue'
$r=Join-Path $env:LOCALAPPDATA 'npu-render'
. ([scriptblock]::Create([IO.File]::ReadAllText("$r\vm\open-guest-console-lite.ps1")))
try {
 $seen='';$watch=[Diagnostics.Stopwatch]::StartNew()
 while($watch.Elapsed.TotalSeconds -lt NR_SECONDS){
  $result=Invoke-Command -Session $nrLab -ArgumentList $seen -ScriptBlock {
   param($previous)
   $ErrorActionPreference='Stop'
   $owned=$env:COMPUTERNAME -eq 'NR-GFX-LAB'
   if(!$owned -or [IO.DriveInfo]::new('C:\').VolumeLabel -ne 'NR_WINDOWS'){
    throw 'Owned lab guest required'
   }
   if('NR_TOPOLOGY' -ne ''){
    $state=[ordered]@{topology_name='NR_TOPOLOGY';sole_indirect=$false}
    $path='C:\npu-lab\NR_TOPOLOGY.jsonl'
    if(Test-Path $path){
     $text=Get-Content $path -Raw
     # The native writer appends complete JSON lines. Ignore a concurrently
     # written final fragment rather than parsing a partial record.
     $end=$text.LastIndexOf("`n")
     if($end -ge 0){$text=$text.Substring(0,$end+1)}else{$text=''}
     $rows=@($text -split "`n" |
      Where-Object {$_.Trim()} | ForEach-Object {$_|ConvertFrom-Json})
     $last=$rows|Where-Object {$null -ne $_.path_count}|Select-Object -Last 1
     $admitted=@($rows|Where-Object {$_.indirect_display_active -eq $true}).Count -gt 0
     $finished=@($rows|Where-Object {
      $_.operation -eq 'restore' -or $null -ne $_.passed -or $null -ne $_.error
     }).Count -gt 0
     $state.sole_indirect=$admitted -and !$finished -and
      $last.phase -eq 'extended' -and $last.path_count -eq 1
    }
    [pscustomobject]@{kind='topology';payload=($state|ConvertTo-Json -Compress)}
   }
   if(![IO.Directory]::Exists('C:\npu-lab\idd-trace')){return}
   $file=Get-ChildItem C:\npu-lab\idd-trace\latest-frame-*.bin -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
   if(!$file){return}
   $share=[IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete
   try {$stream=[IO.File]::Open($file.FullName,[IO.FileMode]::Open,[IO.FileAccess]::Read,$share)}
   catch {
    $error=$_.Exception;while($error.InnerException){$error=$error.InnerException}
    # Atomic replacement can briefly deny a new reader. Retry only absence or
    # sharing/lock conflicts; retain the last complete local frame meanwhile.
    if(($error.HResult -band 65535) -in @(2,32,33)){return}
    throw
   }
   try {
    if($stream.Length -lt 64 -or $stream.Length -gt 16777280){throw 'Frame outside bounded ABI'}
    $header=New-Object byte[] 64;$offset=0
    while($offset -lt 64){
     $n=$stream.Read($header,$offset,64-$offset)
     if(!$n){throw 'Truncated header'};$offset+=$n
    }
    $id=[BitConverter]::ToUInt32($header,56).ToString()+':'+[BitConverter]::ToUInt64($header,16).ToString()+':'+[BitConverter]::ToUInt64($header,48).ToString()
    if($id -eq $previous){return}
    $bytes=New-Object byte[] ([int]$stream.Length);[Array]::Copy($header,$bytes,64)
    while($offset -lt $bytes.Length){
     $n=$stream.Read($bytes,$offset,$bytes.Length-$offset)
     if(!$n){throw 'Truncated pixels'};$offset+=$n
    }
    [pscustomobject]@{kind='frame';identity=$id;payload=[Convert]::ToBase64String($bytes)}
   } finally {$stream.Dispose()}
  }
  foreach($item in $result){
   if($item.kind -eq 'frame'){
    $seen=$item.identity;[Console]::Out.WriteLine('NRFRAME:'+$item.payload)
   }else{[Console]::Out.WriteLine('NRSTATE:'+$item.payload)}
   [Console]::Out.Flush()
  }
  Start-Sleep -Milliseconds 250
 }
} finally {Remove-PSSession $nrLab}
"""


def run(args):
    if not 1 <= args.seconds <= 300 or args.host.startswith("-"):
        raise ValueError("expected SSH host and duration 1–300 seconds")
    args.output.mkdir(parents=True, exist_ok=False)
    if args.topology_name and not re.fullmatch(r"idd-topology-[a-z0-9-]+", args.topology_name):
        raise ValueError("expected owned topology report name")
    script = SCRIPT.replace("NR_SECONDS", str(args.seconds)).replace(
        "NR_TOPOLOGY", args.topology_name or ""
    )
    command = "powershell.exe -NoProfile -NonInteractive -EncodedCommand "
    command += base64.b64encode(script.encode("utf-16-le")).decode("ascii")
    count = 0
    with (args.output / "ssh.stderr.log").open("wb") as stderr:
        child = subprocess.Popen(
            [
                "ssh",
                "-o",
                "BatchMode=yes",
                "-o",
                "ConnectTimeout=8",
                "-o",
                "ServerAliveInterval=10",
                "-o",
                "ServerAliveCountMax=3",
                args.host,
                command,
            ],
            stdout=subprocess.PIPE,
            stderr=stderr,
        )
        deadline = threading.Timer(args.seconds + 90, child.terminate)
        deadline.start()
        try:
            with (args.output / "frames.jsonl").open("x", encoding="utf-8") as log:
                while line := child.stdout.readline(4 * ((MAX_BYTES + 2) // 3) + 32):
                    if line.startswith(b"NRSTATE:"):
                        if len(line) > 4096:
                            raise ValueError("oversized topology status")
                        state = json.loads(line[8:])
                        if state.get("topology_name") != args.topology_name:
                            raise ValueError("topology report identity changed")
                        temporary = args.output / "incoming-state.json"
                        temporary.write_text(json.dumps(state) + "\n", encoding="utf-8")
                        temporary.replace(args.output / "input-ready.json")
                        continue
                    if not line.startswith(b"NRFRAME:"):
                        if len(line) > 4096:
                            raise ValueError("unexpected oversized management output")
                        continue
                    data = base64.b64decode(line[8:].strip(), validate=True)
                    frame = parse_frame(data)
                    temporary = args.output / "incoming.bin"
                    temporary.write_bytes(data)
                    temporary.replace(args.output / "latest.bin")
                    count += 1
                    record = {
                        "identity": frame.identity,
                        "width": frame.width,
                        "height": frame.height,
                        "sha256": hashlib.sha256(data).hexdigest(),
                        "received_frames": count,
                        "displayed_frames_verified": 0,
                    }
                    log.write(json.dumps(record) + "\n")
                    log.flush()
                    print(json.dumps(record), flush=True)
            if child.wait(timeout=15):
                raise RuntimeError("frame collection failed; inspect ssh.stderr.log")
        finally:
            deadline.cancel()
            if child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, help="existing SSH configuration name")
    parser.add_argument("--seconds", type=int, default=120)
    parser.add_argument("--topology-name", help="finite sole-indirect trial's report name")
    parser.add_argument("--output", type=Path, required=True, help="new local evidence directory")
    run(parser.parse_args())
