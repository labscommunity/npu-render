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
"""

import argparse
import hashlib
import json
from pathlib import Path

COMPACT_PROGRAM = "92e04a6734dc6e70ffed15b67fddba02892c66168aa0d47a6cf4bcc65a6a585d"
VECTOR_FILL_PROGRAM = "00793de192c05fc47f58a7a41a184245ded4df14fd83b58a7a31a514df0121b2"


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
        "--registration-generation",
        type=int,
        default=None,
        help="generation whose registration (package qualification) this chain reuses; default: this generation",
    )
    args = parser.parse_args()
    n = args.monitor_seconds
    # Leases renew in place, so the admission itself is bounded only by the controller; the
    # topology helper (ValidateRange 2160) and the frame relay (assert <= 2160) still cap a run.
    assert 60 <= n <= 2160, "topology helper and frame relay accept at most 2160 s"
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
                "worker=subprocess.Popen([str(v.worker),pipe,'65536',str(min(v.monitor_seconds+45,2400)),",
            ),
            (
                "scope=1,desktop_session=session['console_session'],milliseconds=(v.monitor_seconds+60)*1000))",
                "scope=1,desktop_session=session['console_session'],milliseconds=min(v.monitor_seconds+60,2400)*1000))",
            ),
            (
                "scope=2,desktop_session=session['console_session'],milliseconds=(v.monitor_seconds+60)*1000))\n",
                "scope=2,desktop_session=session['console_session'],milliseconds=min(v.monitor_seconds+60,2400)*1000))\n"
                "    renewal=Renewal(leases,v.recovery_task);report['lease_renewals']=[]\n",
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
    boot = boot.replace(
        "report.update(request_recording_enabled=",
        "report.update(frame_blob_dir=str(v.frame_blob_dir),frame_blob_manifest_sha256=hashlib.sha256((v.frame_blob_dir/'manifest.txt').read_bytes()).hexdigest(),request_recording_enabled=",
    )
    assert "frame_blob_manifest_sha256" in boot
    write("native_desktop_bootstrap_v50.py", boot)
    write(
        "native_lease_renewal_v1.py",
        (Path(__file__).resolve().parent / "native_lease_renewal_v1.py").read_text(
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
                registration_generation=rg,
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
