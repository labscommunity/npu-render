"""Read completed IDD frames through existing SSH/WinRM management sessions.

Only the disposable NR-GFX-LAB guest is admitted. This adds no guest listener,
changes no drivers and performs no rendering. The local latest file is replaced
only after a complete, validated frame arrives. Run concurrently with a trial.
"""

import argparse
import base64
import hashlib
import json
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
   if(![IO.Directory]::Exists('C:\npu-lab\idd-trace')){return}
   $file=Get-ChildItem C:\npu-lab\idd-trace\latest-frame-*.bin -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
   if(!$file){return}
   $share=[IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete
   $stream=[IO.File]::Open($file.FullName,[IO.FileMode]::Open,[IO.FileAccess]::Read,$share)
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
    [pscustomobject]@{identity=$id;payload=[Convert]::ToBase64String($bytes)}
   } finally {$stream.Dispose()}
  }
  if($result){$seen=$result.identity;[Console]::Out.WriteLine('NRFRAME:'+$result.payload);[Console]::Out.Flush()}
  Start-Sleep -Milliseconds 250
 }
} finally {Remove-PSSession $nrLab}
"""


def run(args):
    if not 1 <= args.seconds <= 300 or args.host.startswith("-"):
        raise ValueError("expected SSH host and duration 1–300 seconds")
    args.output.mkdir(parents=True, exist_ok=False)
    script = SCRIPT.replace("NR_SECONDS", str(args.seconds))
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
    parser.add_argument("--output", type=Path, required=True, help="new local evidence directory")
    run(parser.parse_args())
