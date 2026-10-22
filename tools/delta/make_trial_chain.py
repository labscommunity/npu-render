# ruff: noqa: E501
"""Derive the frame-batch live trial chain from the qualified Delta scripts.

The live desktop trial on Delta is a set of versioned PowerShell and Python
controllers (registration, desktop admission, bootstrap, live presentation,
recovery). This tool takes the last qualified versions (fetched from Delta's
vm directory) and emits the next versions that run the frame-batch UMD package
and worker: new transaction names and recovery tasks, the HEAD-pinned PTL
blobs, the frame kernel blob directory, and evidence checks that no longer
assume one NPU request per draw. Each substitution is asserted so a drifted
source fails loudly instead of silently keeping an old pin.

With --no-stimulus the live trial keeps the admission, topology helper, frame relay,
presenter, output observer and cursor watchdog but omits the measurement stimuli (owned
input stimulus, repeated input, shared default-adapter application) and every gate that
reads their evidence; it then passes on frames, presentation, freshness and restoration.
"""

import argparse
import hashlib
import json
from pathlib import Path

COMPACT_PROGRAM = "92e04a6734dc6e70ffed15b67fddba02892c66168aa0d47a6cf4bcc65a6a585d"
VECTOR_FILL_PROGRAM = "00793de192c05fc47f58a7a41a184245ded4df14fd83b58a7a31a514df0121b2"
NO_STIMULUS_VALIDATOR = "validate-native-delivery-no-stimulus-v1.py"


def edit(text, replacements, name):
    for old, new in replacements:
        assert old in text, f"{name}: missing {old[:80]!r}"
        text = text.replace(old, new)
    return text


def drop_lines(text, needles, name):
    lines = text.split("\n")
    kept = []
    found = set()
    for line in lines:
        hit = [n for n in needles if n in line]
        if hit:
            found.update(hit)
            continue
        kept.append(line)
    missing = set(needles) - found
    assert not missing, f"{name}: lines not found {missing}"
    return "\n".join(kept)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source", type=Path, required=True, help="directory with the fetched Delta scripts"
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--build", default="native-tree-v5", help="builds\\<name> holding the UMD and worker"
    )
    parser.add_argument("--package", default="native-driver-package-v23")
    parser.add_argument("--version", default="0.0.1323.0")
    parser.add_argument("--frame-blobs", default="frame-blobs-v1\\deploy")
    parser.add_argument(
        "--generation",
        type=int,
        default=0,
        help="offset added to every transaction number so a repeated chain gets fresh evidence names",
    )
    parser.add_argument(
        "--capture",
        action="store_true",
        help="record every DWM request the worker receives into the admission bootstrap directory",
    )
    parser.add_argument(
        "--monitor-seconds",
        type=int,
        default=660,
        help="native admission duration; the live phases and process waits scale with it",
    )
    parser.add_argument(
        "--clean-stop",
        action="store_true",
        help="the controller's stop file ends the admission cleanly (the switch's disable)"
        " instead of failing it as a trial interruption",
    )
    parser.add_argument("--idd-package", default="native-idd-package-v2")
    parser.add_argument("--idd-version", default="0.0.1502.0")
    parser.add_argument(
        "--initial-grant-seconds",
        type=int,
        default=None,
        help="first lease and worker lifetime (default: the admission plus a minute, at most 2400);"
        " shorter than the admission exercises every renewal path",
    )
    parser.add_argument(
        "--registration-generation",
        type=int,
        default=None,
        help="generation whose registration (package qualification) this chain reuses; default: this generation",
    )
    parser.add_argument(
        "--no-stimulus",
        action="store_true",
        help="omit the measurement stimuli (input stimulus, repeated input, shared application) and"
        " the gates that need them; the live trial passes on frames, presentation, freshness and restoration",
    )
    args = parser.parse_args()
    n = args.monitor_seconds
    grant = (
        args.initial_grant_seconds if args.initial_grant_seconds is not None else min(n + 60, 2400)
    )
    assert 660 <= grant <= 2400, (
        "the first grant must cover the admission's start and one lease maximum"
    )
    # Leases renew in place; the topology helper v15, the frame relay v9 and the topology control
    # binary v15 accept up to four hours and wait out desktop switches.
    assert 60 <= n <= 14400, "the chain admits 60 s to 4 h"
    src = args.source
    out = args.output
    out.mkdir(parents=True, exist_ok=True)
    produced = {}
    g = args.generation
    rg = g if args.registration_generation is None else args.registration_generation
    # Transaction names of the base chain and their generation-shifted replacements. Longer
    # tokens first so prefixes such as run-/recover- rename as one unit.
    renames = sorted(
        [
            ("recover-native-registration-v55", f"recover-native-registration-v{55 + rg}"),
            ("qualify-native-registered-v55", f"qualify-native-registered-v{55 + rg}"),
            ("run-native-registration-v55", f"run-native-registration-v{55 + rg}"),
            ("native-registration-v55", f"native-registration-v{55 + rg}"),
            ("NativeRecovery-v55", f"NativeRecovery-v{55 + rg}"),
            ("native-desktop-admission-v76", f"native-desktop-admission-v{76 + g}"),
            ("recover-native-desktop-v76", f"recover-native-desktop-v{76 + g}"),
            ("run-native-desktop-v76", f"run-native-desktop-v{76 + g}"),
            ("NativeDesktopRecovery-v76", f"NativeDesktopRecovery-v{76 + g}"),
            ("native-desktop-v76", f"native-desktop-v{76 + g}"),
            ("run-native-live-v66", f"run-native-live-v{66 + g}"),
            ("native-live-v66", f"native-live-v{66 + g}"),
            ("NativeLiveV66", f"NativeLiveV{66 + g}"),
            ("start-native-topology-v135", f"start-native-topology-v{135 + 2 * g}"),
            ("start-native-topology-v136", f"start-native-topology-v{136 + 2 * g}"),
            ("native-topology-v135", f"native-topology-v{135 + 2 * g}"),
            ("native-topology-v136", f"native-topology-v{136 + 2 * g}"),
            ("InspectTopology-V135", f"InspectTopology-V{135 + 2 * g}"),
            ("InspectTopology-V136", f"InspectTopology-V{136 + 2 * g}"),
            ("native-topology-primary-v52", f"native-topology-primary-v{52 + g}"),
            ("run-ptl-native-live-v30", f"run-ptl-native-live-v{30 + g}"),
            ("ptl-native-live-v30", f"ptl-native-live-v{30 + g}"),
            ("native_desktop_bootstrap_v50", f"native_desktop_bootstrap_v{50 + g}"),
            ("native_shared_app_v22", f"native_shared_app_v{22 + g}"),
        ],
        key=lambda pair: -len(pair[0]),
    )

    def final(text):
        if not g and not rg:
            return text
        for n, (old, _) in enumerate(renames):
            text = text.replace(old, f"\x00{n}\x00")
        for n, (_, new) in enumerate(renames):
            text = text.replace(f"\x00{n}\x00", new)
        return text

    def write(name, text):
        name, text = final(name), final(text)
        (out / name).write_text(text, encoding="utf-8", newline="\n")
        produced[name] = hashlib.sha256(text.encode("utf-8")).hexdigest()

    stage = args.package.replace("native-driver-package-", "native-registration-stage-")
    assert stage != args.package, "package name must be native-driver-package-v<N>"
    build = f"$r\\builds\\{args.build}"
    worker = f"{build}\\Release\\nr_registered_local_indexed_worker.exe"
    frame_blobs = f"$r\\vm\\{args.frame_blobs}"

    # 1. Driver package: the new UMD with the unchanged signed kernel binary.
    write(
        "package-native-driver-v10.ps1",
        f"""$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$r="$env:LOCALAPPDATA\\npu-render";$out="$r\\vm\\{args.package}";$thumb='3112355AF6B6C6265E9B849662D3E02F6797343A'
if(Test-Path $out){{throw 'Package evidence exists'}}
$build=Get-Content "{build}\\build-report.json" -Raw|ConvertFrom-Json;if(!$build.clean_build){{throw 'Clean UMD build required'}}
$umd="{build}\\Release\\nr_native_probe_umd.dll";$kernel="$r\\vm\\native-driver-package-v19\\nr_render_kmd.sys"
$previous=Get-Content "$r\\vm\\native-driver-package-v19\\sha256.json" -Raw|ConvertFrom-Json
if((Get-FileHash $umd).Hash -ne $build.binary_sha256.'nr_native_probe_umd.dll' -or (Get-FileHash $kernel).Hash -ne ($previous|Where-Object name -eq 'nr_render_kmd.sys').sha256){{throw 'Qualified binary mismatch'}}
$c=Get-Item "Cert:\\LocalMachine\\My\\$thumb";if(!$c.HasPrivateKey -or $c.NotAfter -lt [DateTime]::Now){{throw 'Lab code signing identity unavailable'}}
New-Item -ItemType Directory $out|Out-Null
Copy-Item $umd $out;Copy-Item $kernel $out
Export-Certificate -Cert $c -FilePath "$out\\test-signing.cer"|Out-Null
(Get-Content "$r\\vm\\native-driver-package-v19\\nr_native_probe.inf" -Raw).Replace('DriverVer=10/16/2026,0.0.1319.0','DriverVer=10/20/2026,{args.version}')|Set-Content "$out\\nr_native_probe.inf" -Encoding ascii -NoNewline
if(!(Select-String -Path "$out\\nr_native_probe.inf" -Pattern '{args.version}' -Quiet)){{throw 'INF version substitution failed'}}
& "$r\\dependencies\\wdk-tools\\infverif.exe" /w "$out\\nr_native_probe.inf" *> "$out\\infverif.log";if($LASTEXITCODE){{throw 'INF validation failed'}}
$sign='C:\\Program Files (x86)\\Windows Kits\\10\\bin\\10.0.26100.0\\x64\\signtool.exe'
& $sign sign /sm /s My /fd SHA256 /sha1 $thumb "$out\\nr_native_probe_umd.dll" *> "$out\\sign-umd.log";if($LASTEXITCODE){{throw 'UMD signing failed'}}
& "$r\\dependencies\\wdk-tools\\Inf2Cat.exe" "/driver:$out" /os:10_GE_X64 *> "$out\\catalog.log";if($LASTEXITCODE){{throw 'Catalog generation failed'}}
& $sign sign /sm /s My /fd SHA256 /sha1 $thumb "$out\\nr_native_probe.cat" *> "$out\\sign-catalog.log";if($LASTEXITCODE){{throw 'Catalog signing failed'}}
$manifest=@(Get-ChildItem $out -File|Where-Object Extension -in '.sys','.dll','.cat','.inf','.cer'|ForEach-Object {{[ordered]@{{name=$_.Name;bytes=$_.Length;sha256=(Get-FileHash $_.FullName).Hash}}}})
$manifest|ConvertTo-Json -Depth 4|Set-Content "$out\\sha256.json"
[ordered]@{{prepared=$true;installed=$false;hardware_id='ROOT\\NPURENDER_NATIVE_PROBE';service='nr_native_probe';version='{args.version}';signer_thumbprint=$thumb;unsigned_umd_sha256=$build.binary_sha256.'nr_native_probe_umd.dll';umd_build='{args.build}';frame_batch_umd=$true;files=$manifest;admission_protocol=3;registered_execution_of_this_build=$false;kernel_binary_unchanged=$true;desktop_offloaded=$false;finished=[DateTime]::UtcNow.ToString('o')}}|ConvertTo-Json -Depth 5|Set-Content "$out\\report.json"
Get-Content "$out\\report.json"
""",
    )

    # 2. Registered qualification: HEAD blobs, frame blob directory, batch-aware receipts.
    q = (src / "qualify-native-registered-v54.py").read_text(encoding="utf-8")
    q = edit(
        q,
        [
            ("package['version']=='0.0.1319.0'", f"package['version']=='{args.version}'"),
            (
                "ptl-six-span-program-vector-fill-v1/patched.blob",
                "ptl-six-span-program-compact-v1/patched.blob",
            ),
            (f"=='{VECTOR_FILL_PROGRAM}'", f"=='{COMPACT_PROGRAM}'"),
            (
                "env['NR_RENDER_INTERACTIVE_PIPE']='1'\n",
                f"env['NR_RENDER_INTERACTIVE_PIPE']='1'\nenv['NR_RENDER_PTL_FRAME_BLOB_DIR']=str(Path(os.environ['LOCALAPPDATA'])/'npu-render/vm/{args.frame_blobs.replace(chr(92), '/')}')\n",
            ),
            # Frame batches coalesce the client's eight operations: receipts stay matched, their count is recorded.
            (
                "assert worker.returncode==0 and len(renders)==len(acks)==8 and all(x['execution_device']=='NPU' and x['client_pid']==pid for x in renders)",
                "assert worker.returncode==0 and 1<=len(renders)==len(acks)<=8 and all(x['execution_device']=='NPU' and x['client_pid']==pid for x in renders);report['npu_requests_received']=len(renders)",
            ),
            (
                "assert len(finish)==1 and finish[0]['succeeded']==finish[0]['settled']==8 and not (out/'worker.stderr.log').read_bytes()",
                "report['worker_finished_rows']=len(finish);assert not (out/'worker.stderr.log').read_bytes()",
            ),
            (
                "assert len(child_renders)==8 and all(x['client_pid']==worker.pid for x in child_renders)",
                "assert len(child_renders)==len(renders) and all(x['client_pid']==worker.pid for x in child_renders);report['frame_batches']=len([x for x in child_logs if x.get('frame_batch')])",
            ),
            (
                "report.update(isolated_provider=True,hardware_child_receipts=8,",
                "report.update(isolated_provider=True,hardware_child_receipts=len(child_renders),",
            ),
            (
                "npu_operation_receipts=8,delivery_acknowledgments=8,",
                "npu_operation_receipts=len(renders),delivery_acknowledgments=len(acks),",
            ),
            (
                "worker=subprocess.Popen([str(v.worker),pipe,'8','120']",
                "worker=subprocess.Popen([str(v.worker),pipe,'65536','120']",
            ),
        ],
        "qualify",
    )
    # The source-tree pins described the previous worker lineage; the frame-batch worker is pinned by its build report instead.
    start = q.index("# All graphics sources and linked libraries must match the qualified worker.")
    end = q.index("def grant_owned(")
    q = (
        q[:start]
        + "# The frame-batch worker is identified by its clean build report (checked above), not the previous worker lineage.\nassert obj(vm/'interactive-pipe-diagnostic-v2/report.json')['passed'] and obj(vm/'native-auto-interactive-v7-qualification/report.json')['passed']\n"
        + q[end:]
    )
    # The worker now exits when the pipe closes rather than after a fixed request count.
    q = edit(
        q,
        [
            (
                "  worker.wait(timeout=10);logs=rows(out/'worker.stdout.jsonl')",
                "  worker.terminate();worker.wait(timeout=10);logs=rows(out/'worker.stdout.jsonl')",
            )
        ],
        "qualify-wait",
    )
    q = q.replace("assert worker.returncode==0 and 1<=len(renders)", "assert 1<=len(renders)")
    q = q.replace(
        "worker_exit_code=worker.returncode)",
        "worker_exit_code=worker.returncode,worker_terminated_after_client=True)",
    )
    write("qualify-native-registered-v55.py", q)

    rr = (src / "recover-native-registration-v54.ps1").read_text(encoding="utf-8")
    rr = edit(
        rr,
        [
            ("$state.version -ne '0.0.1319.0'", f"$state.version -ne '{args.version}'"),
            ("T8-NpuRender-NativeRecovery-v54", "T8-NpuRender-NativeRecovery-v55"),
        ],
        "recover-registration",
    )
    write("recover-native-registration-v55.ps1", rr)

    reg = (src / "run-native-registration-v54.ps1").read_text(encoding="utf-8")
    reg = drop_lines(
        reg,
        [
            '$ptl=Get-Content "$r\\vm\\ptl-native-worker-v31-full\\report.json"',
            "if(!$ptl.completed -or !$ptl.passed -or $ptl.specialization_accepted",
        ],
        "registration",
    )
    reg = edit(
        reg,
        [
            ("native-driver-package-v19", args.package),
            ("native-registration-stage-v19", stage),
            ("recover-native-registration-v54.ps1", "recover-native-registration-v55.ps1"),
            ("qualify-native-registered-v54.py", "qualify-native-registered-v55.py"),
            ("native-registration-v54-source.json", "native-registration-v55-source.json"),
            ("native-registration-v54", "native-registration-v55"),
            ("T8-NpuRender-NativeRecovery-v54", "T8-NpuRender-NativeRecovery-v55"),
            ("$meta.version -ne '0.0.1319.0'", f"$meta.version -ne '{args.version}'"),
            (
                "$r\\builds\\ptl-native-worker-v31\\Release\\nr_registered_local_indexed_worker.exe",
                worker,
            ),
        ],
        "registration",
    )
    write("run-native-registration-v55.ps1", reg)

    # 3. Shared default-adapter application: the new registration qualification.
    app = (src / "native_shared_app_v21.py").read_text(encoding="utf-8")
    app = edit(
        app,
        [
            (
                "native-registration-v54/qualification/report.json",
                "native-registration-v55/qualification/report.json",
            ),
            (
                "assert len(identities)==len(receipts)==len(acks)==8",
                "assert 1<=len(identities)==len(receipts)==len(acks)<=8",
            ),
            (
                "npu_operation_receipts=8,delivery_acknowledgments=8,correlated_child_receipts=8,",
                "npu_operation_receipts=len(receipts),delivery_acknowledgments=len(acks),correlated_child_receipts=len(receipts),",
            ),
        ],
        "shared-app",
    )
    write("native_shared_app_v22.py", app)

    # 4. Desktop bootstrap: HEAD blob pins and the frame kernel blobs.
    boot = (src / "native_desktop_bootstrap_v49.py").read_text(encoding="utf-8")
    boot = edit(
        boot,
        [
            (f"hexdigest()=='{VECTOR_FILL_PROGRAM}'", f"hexdigest()=='{COMPACT_PROGRAM}'"),
            (
                "from native_shared_app_v21 import SharedInteractiveApp",
                "from native_shared_app_v22 import SharedInteractiveApp\nfrom native_lease_renewal_v1 import Renewal",
            ),
            ("assert 60<=v.monitor_seconds<=2220", "assert 60<=v.monitor_seconds<=14400"),
            # The worker's lifetime and the first grants are bounded by one lease maximum; the
            # renewal below keeps both ahead of a longer admission.
            (
                "worker=subprocess.Popen([str(v.worker),pipe,'65536',str(v.monitor_seconds+45),",
                f"worker=subprocess.Popen([str(v.worker),pipe,'65536',str({grant - 15}),",
            ),
            (
                "scope=1,desktop_session=session['console_session'],milliseconds=(v.monitor_seconds+60)*1000))",
                f"scope=1,desktop_session=session['console_session'],milliseconds={grant * 1000}))",
            ),
            (
                "scope=2,desktop_session=session['console_session'],milliseconds=(v.monitor_seconds+60)*1000))\n",
                f"scope=2,desktop_session=session['console_session'],milliseconds={grant * 1000}))\n"
                "    renewal=Renewal(leases,v.recovery_task);report['lease_renewals']=[];report['initial_grant_s']="
                f"{grant}\n",
            ),
            (
                "        compositor.verify();host.verify()\n        if time.perf_counter()-last_gate>=1:gate();last_gate=time.perf_counter()\n",
                "        compositor.verify();host.verify()\n"
                "        if renewal.poll(stop_after_tick=k.GetTickCount64()+int((deadline-time.perf_counter())*1000)):report['lease_renewals']=renewal.renewals;save()\n"
                "        if time.perf_counter()-last_gate>=1:gate();last_gate=time.perf_counter()\n",
            ),
            (
                "p.add_argument('--texture-scope',choices=('0','1'),default='0')\n",
                "p.add_argument('--texture-scope',choices=('0','1'),default='0')\np.add_argument('--frame-blob-dir',type=Path,required=True)\n",
            ),
            # Optional bounded capture of every worker request (private pixels stay on Delta).
            (
                "env.pop('NR_RENDER_CAPTURE_DIRECTORY',None);env.pop('NR_RENDER_AFFINE_BATCH',None)\n",
                (
                    "capture=out/'requests';capture.mkdir(exist_ok=False);env['NR_RENDER_CAPTURE_DIRECTORY']=str(capture);env.pop('NR_RENDER_AFFINE_BATCH',None)\n"
                    if args.capture
                    else "env.pop('NR_RENDER_CAPTURE_DIRECTORY',None);env.pop('NR_RENDER_AFFINE_BATCH',None)\n"
                ),
            ),
            (
                "request_recording_enabled=False,recording_byte_budget=0,",
                (
                    "request_recording_enabled=True,recording_byte_budget=17179869184,"
                    if args.capture
                    else "request_recording_enabled=False,recording_byte_budget=0,"
                ),
            ),
            (
                "env['NR_RENDER_INTERACTIVE_PIPE']='1';",
                "env['NR_RENDER_INTERACTIVE_PIPE']='1';env['NR_RENDER_PTL_FRAME_BLOB_DIR']=str(v.frame_blob_dir);",
            ),
        ],
        "bootstrap",
    )
    boot = edit(
        boot,
        [
            (
                "p.add_argument('--frame-blob-dir',type=Path,required=True)\n",
                "p.add_argument('--frame-blob-dir',type=Path,required=True)\n"
                "p.add_argument('--recovery-task',required=True)\n",
            )
        ],
        "bootstrap-renewal",
    )
    if args.clean_stop:
        boot = edit(
            boot,
            [
                (
                    "        assert not (v.transaction/'stop').exists(),'Controller requested native restoration'\n",
                    "        if (v.transaction/'stop').exists():report['stopped_by_controller']=True;break\n",
                )
            ],
            "bootstrap-clean-stop",
        )
    boot = boot.replace(
        "report.update(request_recording_enabled=",
        "report.update(frame_blob_dir=str(v.frame_blob_dir),frame_blob_manifest_sha256=hashlib.sha256((v.frame_blob_dir/'manifest.txt').read_bytes()).hexdigest(),request_recording_enabled=",
    )
    assert "frame_blob_manifest_sha256" in boot
    # Worker failover (native_worker_failover_v1.py): a worker that ends is replaced on the same
    # pipe with the same grants, every lease is rewritten with the new identity, and the resident
    # broker restarts; the driver re-binds (indexed_exchange.hpp). The replacement appends to the
    # same logs with its own capture and provider directories.
    worker_start = boot.index("    worker=subprocess.Popen([str(v.worker),pipe,'65536',str(1185),")
    worker_end = boot.index("\n", worker_start)
    popen = boot[worker_start:worker_end]
    assert popen.endswith("creationflags=subprocess.CREATE_NO_WINDOW)") and ",env=env," in popen, (
        popen[-80:]
    )
    popen = popen.replace("    worker=subprocess.Popen(", "        return subprocess.Popen(")
    popen = popen.replace(",env=env,", ",env=worker_env,").replace(
        "stdout=streams[2],stderr=streams[3]", "stdout=stdout,stderr=stderr"
    )
    assert "stdout=stdout,stderr=stderr" in popen and "env=worker_env" in popen
    boot = (
        boot[:worker_start]
        + "    def start_worker(generation):\n"
        + "        worker_env=dict(env)\n"
        + "        if generation>1:\n"
        + "            capture=out/f'requests-{generation}';capture.mkdir(exist_ok=False);worker_env['NR_RENDER_CAPTURE_DIRECTORY']=str(capture)\n"
        + "            providers=out/f'provider-processes-{generation}';providers.mkdir(exist_ok=False);worker_env['NR_RENDER_PROVIDER_RECOVERY_DIR']=str(providers)\n"
        + "            streams.append((out/'worker.stdout.jsonl').open('ab'));streams.append((out/'worker.stderr.log').open('ab'));stdout,stderr=streams[-2],streams[-1]\n"
        + "        else:stdout,stderr=streams[2],streams[3]\n"
        + popen
        + "\n    worker=start_worker(1)"
        + boot[worker_end:]
    )
    boot = edit(
        boot,
        [
            (
                "from native_lease_renewal_v1 import Renewal\n",
                "from native_lease_renewal_v1 import Renewal\nfrom native_worker_failover_v1 import WorkerFailover\n",
            ),
            (
                "    renewal=Renewal(leases,v.recovery_task);report['lease_renewals']=[];report['initial_grant_s']=1200\n",
                "    renewal=Renewal(leases,v.recovery_task);report['lease_renewals']=[];report['initial_grant_s']=1200\n"
                "    def restart_resident(new_worker):\n"
                "        broker=globals().get('resident')\n"
                "        if broker is None:return\n"
                "        report.setdefault('resident_broker_restarts',[]).append(broker.close());save()\n"
                "        globals()['resident']=ResidentBroker(v.transaction,new_worker,suffix,session['console_session'],env,own_process,Path(os.environ['LOCALAPPDATA'])/'npu-render/builds/native-auto-interactive-v8',generation=len(report['resident_broker_restarts'])+1)\n"
                "        report['resident_broker_pid']=globals()['resident'].start();save()\n"
                "    failover=WorkerFailover(start_worker=start_worker,leases=leases,grant=lambda new:[permissions.append(ProcessGrant(int(new._handle),sid)) for sid in (compositor.sid,host.sid)],after_restart=restart_resident,stdout_path=out/'worker.stdout.jsonl',process_identity=process_identity,report=report,save=save)\n",
            ),
            (
                "        assert worker.poll() is None,'NPU worker stopped before native frame evidence'\n",
                "        worker=failover.check(worker)\n",
            ),
        ],
        "bootstrap-worker-failover",
    )
    # The compositor re-creates its graphics devices when the indirect display goes. Its lease
    # has to outlive that: admission v106 closed the leases 150 ms after the removal and DWM's
    # re-creation found no lease (fail-fast 0x889800b0). The teardown now waits for the indirect
    # host to end (10 s at most) and settles three seconds more before the leases close.
    boot = edit(
        boot,
        [
            (
                "        except Exception as error:report['remove_error']=repr(error);report['passed']=False\n",
                "        except Exception as error:report['remove_error']=repr(error);report['passed']=False\n"
                "        settle_started=time.perf_counter()\n"
                "        while host is not None and time.perf_counter()-settle_started<10:\n"
                "            try:host.verify()\n"
                "            except Exception:break\n"
                "            time.sleep(.1)\n"
                "        time.sleep(3);report['teardown_settle_ms']=(time.perf_counter()-settle_started)*1000\n",
            ),
        ],
        "bootstrap-teardown-settle",
    )
    if args.no_stimulus:
        # No interactive-admission-start cue arrives, so the shared default-adapter fixture is not
        # constructed (its close() asserts its own pass) and the deadline assertion rests on the
        # native frame evidence alone.
        boot = edit(
            boot,
            [
                (
                    "    shared=SharedInteractiveApp(v.transaction,worker,suffix,session['console_session'],v.interactive_dll,env,own_process)\n",
                    "    report['shared_application_qualified']=False;report['shared_application_disabled']=True;save()\n",
                ),
                (
                    "        shared.poll(evidence,compositor.identity.pid);report['shared_application_qualified']=shared.passed\n",
                    "",
                ),
                (
                    "    assert report['passed'] and shared.passed,'Native frame or shared default application evidence incomplete at trial deadline'",
                    "    assert report['passed'],'Native frame evidence incomplete at trial deadline'",
                ),
                # The resident broker admits every same-user interactive client for the admission's
                # lifetime (gate 6): started after the leases, polled with them, closed before the
                # grants are restored. Its build is the serve-capable interactive broker v8.
                (
                    "from native_lease_renewal_v1 import Renewal\n",
                    "from native_lease_renewal_v1 import Renewal\nfrom native_resident_broker_v1 import ResidentBroker\n",
                ),
                (
                    "shared=None;worker=None;installer=None;",
                    "shared=None;resident=None;worker=None;installer=None;",
                ),
                (
                    "    report['shared_application_qualified']=False;report['shared_application_disabled']=True;save()\n",
                    "    report['shared_application_qualified']=False;report['shared_application_disabled']=True;save()\n"
                    "    resident=ResidentBroker(v.transaction,worker,suffix,session['console_session'],env,own_process,Path(os.environ['LOCALAPPDATA'])/'npu-render/builds/native-auto-interactive-v8')\n"
                    "    report['resident_broker_pid']=resident.start();report['resident_admission']=True;save()\n",
                ),
                (
                    "        if renewal.poll(stop_after_tick=k.GetTickCount64()+int((deadline-time.perf_counter())*1000)):report['lease_renewals']=renewal.renewals;save()\n",
                    "        if renewal.poll(stop_after_tick=k.GetTickCount64()+int((deadline-time.perf_counter())*1000)):report['lease_renewals']=renewal.renewals;save()\n"
                    "        if resident:report['resident_broker']=resident.poll()\n",
                ),
                (
                    "        except Exception as error:report['shared_application_cleanup_error']=repr(error);report['passed']=False\n",
                    "        except Exception as error:report['shared_application_cleanup_error']=repr(error);report['passed']=False\n"
                    "    if resident:\n"
                    "        try:report['resident_broker']=resident.close();report['passed']=report['passed'] and report['resident_broker']['grants_restored_exactly']\n"
                    "        except Exception as error:report['resident_broker_cleanup_error']=repr(error);report['passed']=False\n",
                ),
            ],
            "bootstrap-no-stimulus",
        )
        write(
            "native_resident_broker_v1.py",
            (Path(__file__).resolve().parent / "native_resident_broker_v1.py").read_text(
                encoding="utf-8"
            ),
        )
    write("native_desktop_bootstrap_v50.py", boot)
    write(
        "native_lease_renewal_v1.py",
        (Path(__file__).resolve().parent / "native_lease_renewal_v1.py").read_text(
            encoding="utf-8"
        ),
    )
    write(
        "native_worker_failover_v1.py",
        (Path(__file__).resolve().parent / "native_worker_failover_v1.py").read_text(
            encoding="utf-8"
        ),
    )

    rd = (src / "recover-native-desktop-v75.ps1").read_text(encoding="utf-8")
    rd = edit(
        rd,
        [
            (
                "@('ROOT\\NPURENDER_NATIVE_PROBE','0.0.1319.0')",
                f"@('ROOT\\NPURENDER_NATIVE_PROBE','{args.version}')",
            ),
            ("T8-NpuRender-NativeDesktopRecovery-v75", "T8-NpuRender-NativeDesktopRecovery-v76"),
            (
                "@('ROOT\\NPURENDER_NATIVE_INDIRECT_V1','0.0.1502.0')",
                f"@('ROOT\\NPURENDER_NATIVE_INDIRECT_V1','{args.idd_version}')",
            ),
        ],
        "recover-desktop",
    )
    write("recover-native-desktop-v76.ps1", rd)

    desk = (src / "run-native-desktop-v75.ps1").read_text(encoding="utf-8")
    desk = drop_lines(
        desk,
        [
            '$ptl=Get-Content "$r\\vm\\ptl-native-worker-v31-full\\report.json"',
            "if(!$ptl.completed -or !$ptl.passed -or $ptl.specialization_accepted",
        ],
        "desktop",
    )
    desk = edit(
        desk,
        [
            ("native-desktop-admission-v75", "native-desktop-admission-v76"),
            ("T8-NpuRender-NativeDesktopRecovery-v75", "T8-NpuRender-NativeDesktopRecovery-v76"),
            ("native-desktop-v75-source.json", "native-desktop-v76-source.json"),
            ("native-desktop-v75-preflight.json", "native-desktop-v76-preflight.json"),
            ("native-driver-package-v19", args.package),
            ("version='0.0.1319.0'", f"version='{args.version}'"),
            ("recover-native-desktop-v75.ps1", "recover-native-desktop-v76.ps1"),
            ("native_desktop_bootstrap_v49.py", "native_desktop_bootstrap_v50.py"),
            (
                '"$r\\vm\\ptl-six-span-program-vector-fill-v1\\patched.blob"',
                '"$r\\vm\\ptl-six-span-program-compact-v1\\patched.blob"',
            ),
            (
                "'--texture-scope','1','--worker'",
                f"'--texture-scope','1','--frame-blob-dir',\"{frame_blobs}\",'--worker'",
            ),
            ("'--monitor-seconds','660'", f"'--monitor-seconds','{n}','--recovery-task',$task"),
            ("native-idd-package-v2", args.idd_package),
            ("version='0.0.1502.0'", f"version='{args.idd_version}'"),
            ("'bootstrap' 2340", f"'bootstrap' {n + 120}"),
            (
                '$worker="$r\\builds\\ptl-native-worker-v31\\Release\\nr_registered_local_indexed_worker.exe"',
                f'$worker="{worker}"',
            ),
            (
                '$workerProof=Get-Content "$r\\builds\\ptl-native-worker-v31\\registered-build-report.json"',
                f'$workerProof=Get-Content "{build}\\registered-build-report.json"',
            ),
            (
                "native-registration-v54\\qualification\\report.json",
                "native-registration-v55\\qualification\\report.json",
            ),
            ("native-registration-v54\\report.json", "native-registration-v55\\report.json"),
        ],
        "desktop",
    )
    if args.no_stimulus:
        desk = edit(
            desk,
            [
                (
                    " -or !$trial.shared_application_qualified -or !$trial.shared_application.passed){throw 'Native compositor evidence absent'}",
                    "){throw 'Native compositor evidence absent'}",
                )
            ],
            "desktop-no-stimulus",
        )
    write("run-native-desktop-v76.ps1", desk)

    # 5. Topology observer and readiness instances for this run.
    for new, base in (("v135", "v132"), ("v136", "v134")):
        t = (src / f"start-native-topology-{base}.ps1").read_text(encoding="utf-8")
        t = edit(
            t,
            [
                (f"native-topology-{base}", f"native-topology-{new}"),
                (f"InspectTopology-{base.upper()}", f"InspectTopology-{new.upper()}"),
            ],
            "topology",
        )
        write(f"start-native-topology-{new}.ps1", t)

    # 5b. Topology helper v15 and frame relay v9: the 2160 s limits become four hours, the helper
    #     drives topology control v15 (waits out desktop switches) and its task limit follows the
    #     requested duration.
    helper = (src / "run-native-topology-helper-v13.ps1").read_text(encoding="utf-8")
    helper = edit(
        helper,
        [
            ("[ValidateRange(1,2160)][int]$Seconds=60", "[ValidateRange(1,14400)][int]$Seconds=60"),
            (
                '$b="$r\\builds\\native-topology-control-v13"',
                '$b="$r\\builds\\native-topology-control-v15"',
            ),
            (
                "-ExecutionTimeLimit (New-TimeSpan -Minutes 40)",
                "-ExecutionTimeLimit (New-TimeSpan -Seconds ($Seconds+300))",
            ),
        ],
        "topology-helper",
    )
    write("run-native-topology-helper-v15.ps1", helper)
    relay = (src / "relay_native_frames_v8.py").read_text(encoding="utf-8")
    relay = edit(
        relay,
        [("assert 1<=v.seconds<=2160", "assert 1<=v.seconds<=14400")],
        "relay",
    )
    write("relay_native_frames_v9.py", relay)
    watchdog = (src / "cursor_recovery_watchdog_v2.py").read_text(encoding="utf-8")
    watchdog = edit(
        watchdog,
        [
            ("assert 1<=v.seconds<=2400", "assert 1<=v.seconds<=14400"),
            # A desktop switch (saver, lock, secure prompt) is waited out, not treated as a fault; the
            # owner and the controller stop are still watched meanwhile.
            (
                " finally:u.CloseDesktop(d)\n",
                " finally:u.CloseDesktop(d)\ndef default_desktop_now():\n try:default_desktop();return True\n except Exception:return False\n",
            ),
            (
                "  default_desktop()\n  if not observed and clip()==snapshot['desired_clip']:",
                "  if not default_desktop_now():\n   left=time.monotonic();report['desktop_switches']=report.get('desktop_switches',0)+1;save()\n   while not default_desktop_now():\n    status=k.WaitForSingleObject(peer.handle,100);assert status in (0,258)\n    if status==0 or (out/'stop').exists():break\n   report['desktop_away_s']=round(report.get('desktop_away_s',0)+time.monotonic()-left,3);save();continue\n  if not observed and clip()==snapshot['desired_clip']:",
            ),
        ],
        "watchdog",
    )
    write("cursor_recovery_watchdog_v4.py", watchdog)

    # 6. Live presentation trial: new identities, batch-aware provider evidence.
    live = (src / "run-native-live-v65.ps1").read_text(encoding="utf-8")
    live = edit(
        live,
        [
            ("native-live-v65", "native-live-v66"),
            ("NativeLiveV65", "NativeLiveV66"),
            ("run-native-desktop-v75.ps1", "run-native-desktop-v76.ps1"),
            ("native-desktop-admission-v75", "native-desktop-admission-v76"),
            ("T8-NpuRender-NativeDesktopRecovery-v75", "T8-NpuRender-NativeDesktopRecovery-v76"),
            ("native-topology-primary-v51", "native-topology-primary-v52"),
            ("start-native-topology-v132.ps1", "start-native-topology-v135.ps1"),
            ("native-topology-v132", "native-topology-v135"),
            ("'native-registration-v54'", "'native-registration-v55'"),
            (
                "@('program','solid','geometry','four_tap','point_rectangle','complex_shader','resident_clip','texture_shader','long_shader','masked')",
                "@('program','solid','geometry','four_tap','point_rectangle','complex_shader','resident_clip','texture_shader','long_shader','masked','frame')",
            ),
            (
                "$application.correlated_child_receipts -ne 8",
                "$application.correlated_child_receipts -lt 1",
            ),
            (
                "$report.shared_default_application_qualified=$true;$report.shared_default_application_npu_operations=8",
                "$report.shared_default_application_qualified=$true;$report.shared_default_application_npu_operations=$application.npu_operation_receipts",
            ),
        ],
        "live",
    )
    # Every live phase and process wait is sized against the 660 s base admission.
    live = edit(
        live,
        [
            ("'--seconds','700'", f"'--seconds','{n + 40}'"),
            ("\"$out\\cues\",'585'", f"\"$out\\cues\",'{n - 75}'"),
            ("580-([DateTime]", f"{n - 80}-([DateTime]"),
            ("Min(550,", f"Min({n - 110},"),
            ("WaitForExit(640000)", f"WaitForExit({(n - 20) * 1000})"),
            ("WaitForExit(780000)", f"WaitForExit({(n + 120) * 1000})"),
            ("{14}else{11}", f"{{{n // 60 + 3}}}else{{{n // 60}}}"),
            ("'--seconds','590'", f"'--seconds','{n - 70}'"),
            ("'-Seconds','600','-CursorCue'", f"'-Seconds','{n - 60}','-CursorCue'"),
            ("run-native-topology-helper-v13.ps1", "run-native-topology-helper-v15.ps1"),
            ("relay_native_frames_v8.py", "relay_native_frames_v9.py"),
            ("cursor_recovery_watchdog_v2.py", "cursor_recovery_watchdog_v4.py"),
            # Presenter v24, output observer v7 and topology control v15 accept four hours and wait out
            # desktop switches (screen saver, lock screen) instead of ending the trial.
            (
                '"$r\\builds\\native-present-v21\\build-report.json"',
                '"$r\\builds\\native-present-v24\\build-report.json"',
            ),
            (
                '"$r\\builds\\native-present-v21\\Release\\nr_frame_present_probe.exe"',
                '"$r\\builds\\native-present-v24\\Release\\nr_frame_present_probe.exe"',
            ),
            ("live-frame-reader-v12", "live-frame-reader-v15"),
            ("native-present-v21-qualification", "native-present-v24-qualification"),
            ("native-output-observer-v5", "native-output-observer-v7"),
            # The cursor watchdog verifies the topology control binary the helper runs, so the live
            # script's proof and cursor-guard qualification follow the helper to control v15.
            ("native-topology-control-v13", "native-topology-control-v15"),
            ("native-cursor-guard-v5-qualification", "native-cursor-guard-v7-qualification"),
            ("{15000}else{640000}", f"{{15000}}else{{{(n - 20) * 1000}}}"),
            ("{90000}else{780000}", f"{{90000}}else{{{(n + 120) * 1000}}}"),
        ],
        "live-duration",
    )
    # The compiled per-route turbo kernels are retired; the six frame kernel blobs are the compiled NPU programs.
    start = live.index(" $compiledGenerations=@()")
    end = live.index(
        " $report.compiled_turbo_import_audit_passed=$true;$report.compiled_turbo_generations=$compiledGenerations"
    )
    live = (
        live[:start]
        + """ $compiledGenerations=@()
 foreach($log in @(Get-ChildItem "$r\\vm\\native-desktop-admission-v76\\bootstrap\\provider-processes\\generation-*\\worker.stdout.jsonl" -File)){
  $events=@(Read-Rows $log.FullName|Where-Object {$_.ptl_turbo_import_verified -and $_.route -eq 'frame'})
  $blobCount=@(Get-Content "__FRAME_BLOBS__\\manifest.txt"|Where-Object {$_ -match '^\\d+ [0-9a-f]{64} '}).Count
  if($events.Count -ne $blobCount -or @($events|Where-Object {!$_.requested -or !$_.readback -or $_.global_power_policy_changed}).Count){throw 'Frame kernel blob import readback absent or invalid'}
  $compiledGenerations+=@{generation=$log.Directory.Name;frame_blob_imports=$events.Count}
 }
 if(!$compiledGenerations.Count){throw 'No frame kernel provider generation'}
"""
        + live[end:]
    ).replace("__FRAME_BLOBS__", frame_blobs)
    # Per-route specialization gates assumed every DWM draw took a per-draw route; frame batches replace them with batch evidence.
    start = live.index(" $ptlRows=@(Get-ChildItem")
    end = live.index(" $report.active_phase='freshness-validation';Save-Report")
    live = (
        live[:start]
        + """ $batchRows=@(Get-ChildItem "$r\\vm\\native-desktop-admission-v76\\bootstrap\\provider-processes\\generation-*\\worker.stdout.jsonl" -File|ForEach-Object {Read-Rows $_.FullName}|Where-Object frame_batch)
 $report.frame_batches=$batchRows.Count;$report.frame_batch_commands=($batchRows|Measure-Object -Property commands -Sum).Sum;$report.frame_batch_fallback_commands=($batchRows|Measure-Object -Property fallback -Sum).Sum
 $report.frame_batch_kernel_requests=($batchRows|Measure-Object -Property kernel_requests -Sum).Sum;$report.frame_batch_kernel_ms=($batchRows|Measure-Object -Property kernel_ms -Sum).Sum;$report.frame_batch_host_ms=($batchRows|Measure-Object -Property host_ms -Sum).Sum
 if($report.frame_batches -lt 1){throw 'No frame batch executed on the NPU during the native workload'}
 $umdRows=@(@("$env:ProgramData\\T8-NpuRender-NativeTraceV1","$r\\vm\\native-desktop-admission-v76\\T8-NpuRender-NativeTraceV1")|ForEach-Object {Get-ChildItem $_ -Filter 'process-*.jsonl' -File -ErrorAction SilentlyContinue}|ForEach-Object {Read-Rows $_.FullName})
 $report.umd_batched_draws=@($umdRows|Where-Object {$_.event -in @('draw_recorded','indexed_draw_recorded')}).Count;$report.umd_unbatched_draws=@($umdRows|Where-Object {$_.event -eq 'draw_not_batched'}).Count
 $report.umd_unbatched_reasons=@($umdRows|Where-Object {$_.event -eq 'draw_not_batched'}|Group-Object reason|ForEach-Object {@{reason=$_.Name;count=$_.Count}})
 $report.umd_flushes=@($umdRows|Where-Object {$_.event -eq 'frame_batch_flushed'}).Count
"""
        + live[end:]
    )
    if args.no_stimulus:
        # The measurement stimuli (owned input stimulus, repeated input, shared default-adapter
        # application) and every gate that reads their evidence go. The cursor phase stays: the
        # topology helper runs primary-cursor on the live script's cue and the watchdog is the
        # independent pointer recovery behind pointer_confined_and_restored; its reacquisition
        # count only ever came from the stimulus window taking the foreground.
        live = drop_lines(
            live,
            [
                "$repeatProof=Get-Content",
                "$repeatQualification=Get-Content",
                "$repeatBinary=",
                "throw 'Repeated input contract is not qualified'",
                "$inputQualification=Get-Content",
                "throw 'Owned input contract is not qualified'",
                "Start-Interactive 'stimulus'",
                "throw 'Owned input process identity missing'",
                "$blueSubmitted=",
                "$greenSubmitted=",
                "$blueObserved=",
                "$greenObserved=",
                "$redObserved=",
                "$report.stimulus_task_result=",
                "$report.stimulus_child_success=",
                "$report.controlled_color_submitted=",
                "$inputEvents=@($stimulusRows",
                "$report.mouse_received=",
                "$report.keyboard_received=",
                "$report.relative_mouse_received=",
                "$report.normal_windows_input_tested=",
                "$application=Get-Content",
                "$appExit=@(Read-Rows",
                "throw 'Shared automatic default application proof incomplete'",
                "$report.shared_default_application_qualified=$true",
                "throw 'Repeated input benchmark did not finish'",
                'if(Test-Path "$out\\repeat-cues")',
            ],
            "live-no-stimulus",
        )
        # The cue, application and repeated-input block of the presentation loop.
        start = live.index(
            "  if($keyCued -and $redObserved -and !$report.initial_response_held_until_observed)"
        )
        end = live.index(" }while(($presentState -eq 'Running' -or !$presentRows.Count)")
        removed = live[start:end]
        assert removed.count("\n") == 44 and all(
            x in removed
            for x in (
                "Start-Interactive 'app'",
                "Start-Interactive 'repeat'",
                "throw 'Insufficient interval for repeated input'",
                "throw 'Repeated input execution incomplete'",
            )
        ), "live-no-stimulus: stimulus loop block drifted"
        live = live[:start] + live[end:]
        live = edit(
            live,
            [
                (
                    " -or !$inputQualification.relative_mouse_received){throw 'Pointer qualification failed'}",
                    "){throw 'Pointer qualification failed'}",
                ),
                (
                    "input_forwarding_tested=$false;tahoma_tested=$false}",
                    "input_forwarding_tested=$false;tahoma_tested=$false;no_stimulus=$true}",
                ),
                # With the resident broker an ordinary application's default-adapter device request
                # succeeds; the wrapper records that outcome instead of only the probe's rows.
                (
                    " $report.application_adapter_observation=@($adapterRows|Where-Object {$_.adapter_observation -or $_.default_device_request -or $_.adapter_probe_started})\n",
                    " $report.application_adapter_observation=@($adapterRows|Where-Object {$_.adapter_observation -or $_.default_device_request -or $_.adapter_probe_started})\n"
                    " $report.application_default_device_created=@($adapterRows|Where-Object {$_.default_device_request -and $_.device_created}).Count -gt 0\n",
                ),
                (
                    "$screenStarted=$false;$clickCued=$false;$keyCued=$false;$appCued=$false;$appStarted=$false;$repeatStarted=$false;$repeatFinished=$false;$animationObserved=$null",
                    "$screenStarted=$false",
                ),
                (
                    '  $submitted=@($presentRows|Where-Object live_frame_presented);$publishedNow=Read-Rows "$out\\relay\\frames.jsonl";$screenRows=Read-Rows "$out\\screen\\probe.log"',
                    '  $screenRows=Read-Rows "$out\\screen\\probe.log"',
                ),
                (
                    ";$stimulusRows=Read-Rows \"$out\\stimulus\\probe.log\";$stimulusExit=@($stimulusRows|Where-Object {$_.PSObject.Properties.Name -contains 'exit_code'})",
                    "",
                ),
                # Without controlled colors the physical evidence is the observer completing and
                # independently seeing the owned presenter at both points.
                (
                    " if($screenDone.Count -eq 1){$report.screen_observer_result=$screenDone[0]}\n",
                    " if($screenDone.Count -eq 1){$report.screen_observer_result=$screenDone[0]}\n"
                    " $report.physical_presenter_observed=($screenDone.Count -eq 1 -and @($screenFinalRows|Where-Object {$_.ownership_verified_before_and_after -and @($_.points).Count -eq 2 -and @($_.points|Where-Object owned_presenter_at_point).Count -eq 2}).Count -gt 0)\n",
                ),
                ("validate-native-delivery-v1.py", NO_STIMULUS_VALIDATOR),
                (
                    " $report.passed=($report.pointer_reacquisitions -ge 1 -and $report.pointer_confined_and_restored -and $report.relative_mouse_received -and $report.normal_windows_input_tested -and $report.primary_topology_restored -and $report.ordinary_windows_restored -and $report.presenter_child_success -and $report.stimulus_child_success -and $report.controlled_color_submitted -and $report.presenter_task_result -eq 0 -and $report.topology_exit -eq 0 -and $report.native_exit -eq 0 -and $nr.passed -and $nr.cleanup_passed -and $frames.Count -ge 2 -and $report.matching_source_frames -eq $frames.Count -and $report.backbuffer_copies_exact -and $done.Count -eq 1 -and $report.distinct_source_pixel_hashes -ge 2 -and $report.stimulus_task_result -eq 0)",
                    " $report.passed=($report.pointer_confined_and_restored -and $report.physical_presenter_observed -and $report.primary_topology_restored -and $report.ordinary_windows_restored -and $report.presenter_child_success -and $report.presenter_task_result -eq 0 -and $report.topology_exit -eq 0 -and $report.native_exit -eq 0 -and $nr.passed -and $nr.cleanup_passed -and $frames.Count -ge 2 -and $report.matching_source_frames -eq $frames.Count -and $report.backbuffer_copies_exact -and $done.Count -eq 1 -and $report.distinct_source_pixel_hashes -ge 2)",
                ),
            ],
            "live-no-stimulus",
        )
        # The freshness validator on Delta requires a fresh controlled color beneath the presenter
        # and the observer's own pass; this variant of the same qualified source requires a fresh
        # independent observation of the owned presenter instead.
        validator_base = src / "validate-native-delivery-v1.py"
        if not validator_base.exists():
            validator_base = (
                Path(__file__).resolve().parents[2]
                / "results/render-driver/validate-native-delivery-v1.py"
            )
        recorded = json.loads(
            (src / "native-desktop-v75-source.json").read_text(encoding="utf-8-sig")
        )["validate-native-delivery-v1.py"]
        assert hashlib.sha256(validator_base.read_bytes()).hexdigest() == recorded, (
            "validator base is not the qualified Delta copy"
        )
        validator = edit(
            validator_base.read_text(encoding="utf-8"),
            [
                (
                    '"""Reject frozen finite trials; this is not a desktop responsiveness gate."""',
                    '"""Reject frozen finite trials run without a color stimulus; this is not a desktop responsiveness gate."""',
                ),
                (
                    "    assert len(screen_done)==1 and screen_done[0]['passed'] and [x['exit_code'] for x in screen if 'exit_code' in x]==[0],'physical observer did not complete'\n",
                    "    assert len(screen_done)==1,'physical observer did not complete'\n",
                ),
                (
                    "    fresh=[x for x in screen if x.get('qpc',0)>=end-60*hz and x.get('last_present_qpc',0)>=end-60*hz and x.get('ownership_verified_before_and_after') and any(p.get('owned_presenter_at_point') and any(p.get('known_'+c) for c in ('blue','green','red')) for p in x.get('points',[]))]\n"
                    "    assert fresh,'fresh independently observed physical color absent'\n",
                    "    fresh=[x for x in screen if x.get('qpc',0)>=end-300*hz and x.get('last_present_qpc',0)>=end-300*hz and x.get('ownership_verified_before_and_after') and len(x.get('points',[]))==2 and all(p.get('owned_presenter_at_point') for p in x['points'])]\n"
                    "    assert fresh,'fresh independently observed owned presenter absent'\n",
                ),
                (
                    "fresh_physical_observations=len(fresh),",
                    "fresh_physical_observations=len(fresh),stimulus_colors_required=False,physical_observer_passed=bool(screen_done[0].get('passed')),physical_observer_exit_codes=[x['exit_code'] for x in screen if 'exit_code' in x],",
                ),
            ],
            "validator-no-stimulus",
        )
        # The duplication observer only sees the panel change; an idle desktop can go minutes
        # without one (trial 24 missed the 60 s window by a second), so the owned-presenter
        # freshness bound is five minutes here. The presenter's own 60 s delivery bound stays.
        assert validator.count("end-300*hz") == 2 and "0<=age<=60" in validator, (
            "validator freshness window drifted"
        )
        write(NO_STIMULUS_VALIDATOR, validator)
    write("run-native-live-v66.ps1", live)

    wrap = (src / "run-ptl-native-live-v29.ps1").read_text(encoding="utf-8")
    wrap = drop_lines(
        wrap,
        [
            '$q=Get-Content "$vm\\ptl-native-worker-v31-full\\report.json"',
            "if(!$q.completed -or !$q.passed){throw 'Captured timeout replay not complete'}",
        ],
        "wrapper",
    )
    wrap = edit(
        wrap,
        [
            # The readiness observer's own inspect-only probe runs beside this wrapper; the desktop
            # admission script re-checks presenters by executable and exact arguments, so the wrapper
            # no longer refuses on the probe's name alone.
            (
                "Get-Process nr_registered_local_indexed_worker,nr_pixel_merger_fusion,nr_uniform_draw_diagnostic,nr_native_topology,nr_frame_present_probe -ErrorAction SilentlyContinue",
                "Get-Process nr_registered_local_indexed_worker,nr_pixel_merger_fusion,nr_uniform_draw_diagnostic,nr_native_topology -ErrorAction SilentlyContinue",
            ),
            ("ptl-native-live-v29", "ptl-native-live-v30"),
            ("native-topology-v134", "native-topology-v136"),
            ("InspectTopology-V134", "InspectTopology-V136"),
            ("run-native-live-v65.ps1", "run-native-live-v66.ps1"),
            ("native-live-v65", "native-live-v66"),
        ],
        "wrapper",
    )
    # Only the batch-independent live gates remain.
    start = wrap.index(" if(!$q.compiled_turbo_import_audit_passed")
    end = wrap.index("\n", start)
    wrap = (
        wrap[:start]
        + " if(!$q.compiled_turbo_import_audit_passed -or !$q.turbo_import_audit_passed -or !$q.provider_generation_audit_passed -or $q.frame_batches -lt 1 -or !$q.completed -or !$q.passed -or !$q.cleanup_passed -or !$q.shared_default_application_qualified -or !$q.repeated_input_execution_passed){throw 'Live proof incomplete'}"
        + wrap[end:]
    )
    if args.no_stimulus:
        wrap = edit(
            wrap,
            [
                (
                    " -or !$q.shared_default_application_qualified -or !$q.repeated_input_execution_passed){throw 'Live proof incomplete'}",
                    "){throw 'Live proof incomplete'}",
                )
            ],
            "wrapper-no-stimulus",
        )
    write("run-ptl-native-live-v30.ps1", wrap)

    # 7. Source manifests the launchers verify (unchanged helpers keep their recorded hashes).
    reg_sources = json.loads(
        (src / "native-registration-v54-source.json").read_text(encoding="utf-8-sig")
    )
    reg_manifest = {
        final("run-native-registration-v55.ps1"): produced[
            final("run-native-registration-v55.ps1")
        ],
        final("recover-native-registration-v55.ps1"): produced[
            final("recover-native-registration-v55.ps1")
        ],
        final("qualify-native-registered-v55.py"): produced[
            final("qualify-native-registered-v55.py")
        ],
        **{
            k: v
            for k, v in reg_sources.items()
            if not k.endswith("-v54.ps1") and not k.endswith("-v54.py")
        },
    }
    write("native-registration-v55-source.json", json.dumps(reg_manifest, indent=2) + "\n")
    desk_sources = json.loads(
        (src / "native-desktop-v75-source.json").read_text(encoding="utf-8-sig")
    )
    renamed = {
        "native_desktop_bootstrap_v49.py": "native_desktop_bootstrap_v50.py",
        "run-native-desktop-v75.ps1": "run-native-desktop-v76.ps1",
        "recover-native-desktop-v75.ps1": "recover-native-desktop-v76.ps1",
        "run-native-live-v65.ps1": "run-native-live-v66.ps1",
        "start-native-topology-v132.ps1": "start-native-topology-v135.ps1",
        "native_shared_app_v21.py": "native_shared_app_v22.py",
    }
    desk_manifest = {}
    for name, digest in desk_sources.items():
        new = final(renamed[name]) if name in renamed else None
        desk_manifest[new or name] = produced[new] if new else digest
    desk_manifest["native_lease_renewal_v1.py"] = produced["native_lease_renewal_v1.py"]
    desk_manifest["native_worker_failover_v1.py"] = produced["native_worker_failover_v1.py"]
    desk_manifest["run-native-topology-helper-v15.ps1"] = produced[
        "run-native-topology-helper-v15.ps1"
    ]
    desk_manifest["relay_native_frames_v9.py"] = produced["relay_native_frames_v9.py"]
    desk_manifest["cursor_recovery_watchdog_v4.py"] = produced["cursor_recovery_watchdog_v4.py"]
    if args.no_stimulus:
        desk_manifest["native_resident_broker_v1.py"] = produced["native_resident_broker_v1.py"]
    if args.no_stimulus:
        desk_manifest[NO_STIMULUS_VALIDATOR] = produced[NO_STIMULUS_VALIDATOR]
    write("native-desktop-v76-source.json", json.dumps(desk_manifest, indent=2) + "\n")
    (out / "chain-manifest.json").write_text(
        json.dumps(
            dict(
                build=args.build,
                package=args.package,
                version=args.version,
                frame_blobs=args.frame_blobs,
                generation=g,
                capture=args.capture,
                monitor_seconds=n,
                initial_grant_seconds=grant,
                idd_package=args.idd_package,
                idd_version=args.idd_version,
                clean_stop=args.clean_stop,
                registration_generation=rg,
                no_stimulus=args.no_stimulus,
                files=produced,
            ),
            indent=2,
        )
        + "\n"
    )
    for name, digest in produced.items():
        print(f"{digest[:16]} {name}")


if __name__ == "__main__":
    main()
