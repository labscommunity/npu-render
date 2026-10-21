"""Bounded native indirect-display admission; never disables a physical output."""
from native_frame_io_v1 import open_shared_frame, statistics as frame_io_statistics
from native_session_broker_v3 import *
from native_incremental_evidence_v1 import NativeEvidence
from native_process_observer_v1 import PinnedProcess,named_processes,find_compositor,find_indirect_host,process_image
import traceback,struct
from native_shared_app_v30 import SharedInteractiveApp
p=argparse.ArgumentParser()
for n in ('worker','runtime','cache','session_probe','devcon','idd_inf','transaction','trace'):
    p.add_argument('--'+n.replace('_','-'),type=Path,required=True)
p.add_argument('--interactive-dll',type=Path,required=True)
p.add_argument('--monitor-seconds',type=int,default=360)
p.add_argument('--ptl-long-blob',type=Path,required=True)
p.add_argument('--ptl-complex-blob',type=Path,required=True)
p.add_argument('--ptl-rectangle-blob',type=Path,required=True)
p.add_argument('--ptl-program-blob',type=Path,required=True)
p.add_argument('--ptl-masked-blob',type=Path,required=True)
p.add_argument('--ptl-geometry-blob',type=Path,required=True)
p.add_argument('--ptl-texture-blob',type=Path,required=True)
p.add_argument('--ptl-solid-blob',type=Path,required=True)
p.add_argument('--ptl-four-tap-blob',type=Path,required=True)
p.add_argument('--memory-cache',choices=('0','1'),default='0')
p.add_argument('--generic-batch',choices=('4096','8192','16384'),default='4096')
p.add_argument('--uniform-lanes',choices=('0','1'),default='0')
p.add_argument('--texture-scope',choices=('0','1'),default='0')
p.add_argument('--frame-blob-dir',type=Path,required=True)
v=p.parse_args();assert hashlib.sha256(v.ptl_complex_blob.read_bytes()).hexdigest()=='47f69b5861732a03014b58160116ebbb928fd6e818a11b7f553e423a4cf3c43a';assert hashlib.sha256(v.ptl_rectangle_blob.read_bytes()).hexdigest()=='d651d6101dbccc5796873dc2016ec9e23d262cd955e9bc4bee6d56b86756eb2d';assert hashlib.sha256(v.ptl_long_blob.read_bytes()).hexdigest()=='895831be2695bc151454833249996608f38d35880c635900bb731515bc2bcda1'
assert hashlib.sha256(v.ptl_program_blob.read_bytes()).hexdigest()=='92e04a6734dc6e70ffed15b67fddba02892c66168aa0d47a6cf4bcc65a6a585d'
assert hashlib.sha256(v.ptl_masked_blob.read_bytes()).hexdigest()=='81104c9ea8760bbeb7cc028ea2a9c81e8605482af219f57c082c737b9662d1e0'
assert hashlib.sha256(v.ptl_geometry_blob.read_bytes()).hexdigest()=='ea4432abd7b548396b86665c1b318353e0d2f3ec847bcd3e6f56c10bd1336dfa'
assert hashlib.sha256(v.ptl_texture_blob.read_bytes()).hexdigest()=='481765dd51d8d9b051b87db8be7e62ae7931245fe051b33b6457be77816c2e87'
assert hashlib.sha256(v.ptl_solid_blob.read_bytes()).hexdigest()=='21360321c21d4a0253b260ce62420ffef47ec03daae8b89e6cf05bd4c541ee8f'
assert hashlib.sha256(v.ptl_four_tap_blob.read_bytes()).hexdigest()=='710f6e5586da59f4040213435c5ecc13bb669e6b1be668f838edd00bca5dd697'
v.ptl_resident_clip_blob=Path(os.environ['LOCALAPPDATA'])/'npu-render/vm/ptl-six-span-resident-clip-v3/patched.blob'
assert hashlib.sha256(v.ptl_resident_clip_blob.read_bytes()).hexdigest()=='a5c39dbb825fdd4bd334d8bcbef966e9404132d9b642375665a0892adc48c45c'
assert 60<=v.monitor_seconds<=2220
out=v.transaction/'bootstrap';out.mkdir(exist_ok=False)
report=dict(started=datetime.now(UTC).isoformat(),passed=False,completed=False,desktop_offloaded=False,physical_frame_presented=False,display_topology_requested=False,actual_dwm_npu_execution=False,actual_umdf_npu_execution=False,controller_pid=os.getpid())
def save():
 report['frame_io_statistics']=frame_io_statistics.copy()
 (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
def gate():
    result=subprocess.run([str(v.session_probe),'--inspect-session'],capture_output=True,timeout=5,creationflags=subprocess.CREATE_NO_WINDOW)
    assert result.returncode==0 and not result.stderr
    state=json.loads(result.stdout);assert state['ready_for_interactive_task'] and state['signed_in_and_unlocked'] and state['controller_matches_console_user'],'Signed-in unlocked owner console is required'
    return state
state=obj(v.transaction/'ownership.json');assert state['schema']==3 and state['mode']=='native-admission'
assert hashlib.sha256(v.devcon.read_bytes()).hexdigest()==state['devcon_sha256'].lower()
assert len(state['packages'])==2
idd=next(x for x in state['packages'] if x['inf']=='nr_native_indirect_probe.inf')
assert idd['hardware_id']==r'ROOT\NPURENDER_NATIVE_INDIRECT_V1' and hashlib.sha256(v.idd_inf.read_bytes()).hexdigest()==idd['inf_sha256'].lower()
def own_process(handle,role):
    entry=process_identity(handle);image=process_image(handle)
    state.setdefault('owned_processes',[]).append(dict(pid=entry.pid,created=entry.created,image=image,sha256=hashlib.sha256(Path(image).read_bytes()).hexdigest(),role=role))
    temporary=v.transaction/'ownership-python.tmp';temporary.write_text(json.dumps(state,indent=2)+'\n');temporary.replace(v.transaction/'ownership.json')
def remove_indirect():
    result=subprocess.run([str(v.devcon),'remove',idd['hardware_id']],capture_output=True,timeout=45,creationflags=subprocess.CREATE_NO_WINDOW)
    (out/'remove.stdout.log').write_bytes(result.stdout);(out/'remove.stderr.log').write_bytes(result.stderr)
    report['indirect_remove_exit']=result.returncode;assert result.returncode==0,'Indirect device removal failed or requested restart'
def frame_header(path,pid):
    # A shared-delete handle follows the publisher's atomic complete-file swap.
    k.CreateFileW.argtypes=[W.LPCWSTR,W.DWORD,W.DWORD,W.LPVOID,W.DWORD,W.DWORD,W.HANDLE];k.CreateFileW.restype=W.HANDLE
    k.ReadFile.argtypes=[W.HANDLE,W.LPVOID,W.DWORD,C.POINTER(W.DWORD),W.LPVOID];k.ReadFile.restype=W.BOOL
    k.GetFileSizeEx.argtypes=[W.HANDLE,C.POINTER(C.c_longlong)];k.GetFileSizeEx.restype=W.BOOL
    handle=open_shared_frame(path)
    if handle is None:return None
    try:
        size=C.c_longlong();require(k.GetFileSizeEx(handle,C.byref(size)));buffer=C.create_string_buffer(64);count=W.DWORD();require(k.ReadFile(handle,buffer,64,C.byref(count),None));assert count.value==64
        magic,version,header,sequence,width,height,fmt,row,byte_count,qpc,producer,reserved=struct.unpack('<QIIQIIIIQQII',buffer.raw)
        assert magic==0x31454d415246524e and version==1 and header==64 and sequence>0 and producer==pid and reserved==0 and qpc>0
        assert 0<width<=8192 and 0<height<=8192 and width*height<=4*1024*1024 and fmt in (28,87) and row==width*4 and byte_count==width*height*4 and size.value==byte_count+64
        key=(producer,sequence,qpc)
        if key not in frame_hashes:
            pixels=C.create_string_buffer(byte_count);require(k.ReadFile(handle,pixels,byte_count,C.byref(count),None));assert count.value==byte_count
            frame_hashes[key]=hashlib.sha256(pixels.raw).hexdigest()
        return dict(sequence=sequence,width=width,height=height,format=fmt,pixel_bytes=byte_count,producer_pid=producer,qpc=qpc,pixel_sha256=frame_hashes[key])
    finally:k.CloseHandle(handle)
observed_frames=[];first_frame_at=None;seen_frames=set();frame_hashes={}
shared=None;worker=None;installer=None;compositor=None;host=None;permissions=[];leases=[];install_attempted=False;streams=[];save()
try:
    session=gate();report['session_gate']=session
    compositor=find_compositor(session['console_session']);report['compositor']=compositor.report()
    assert process_identity(k.GetCurrentProcess()).session==0
    own_process(k.GetCurrentProcess(),'controller')
    baseline=set()
    for pid in named_processes('WUDFHost.exe'):
        peer=PinnedProcess(pid)
        try:baseline.add((peer.identity.pid,peer.identity.created))
        finally:peer.close()
    assert find_indirect_host(set(),0) is None,'Owned indirect host already exists'
    stamp=C.c_uint64();k.GetSystemTimeAsFileTime.argtypes=[W.LPVOID];k.GetSystemTimeAsFileTime(C.byref(stamp))
    for name in ('install.stdout.log','install.stderr.log','worker.stdout.jsonl','worker.stderr.log'):streams.append((out/name).open('xb'))
    install_attempted=True
    installer=subprocess.Popen([str(v.devcon),'install',str(v.idd_inf),idd['hardware_id']],stdin=subprocess.DEVNULL,stdout=streams[0],stderr=streams[1],creationflags=subprocess.CREATE_NO_WINDOW)
    deadline=time.perf_counter()+30
    while host is None:
        assert time.perf_counter()<deadline,'New indirect host did not appear'
        if installer.poll() is not None:assert installer.returncode==0,'Indirect installer exited unsuccessfully'
        host=find_indirect_host(baseline,stamp.value)
        if host is None:time.sleep(.02)
    ready_deadline=time.perf_counter()+8;report['indirect_host']=host.report();report['host_found_after_ms']=(time.perf_counter()-(deadline-30))*1000;save()
    gate();compositor.verify();host.verify()
    suffix=secrets.token_hex(16);pipe=r'\\.\pipe\npu-render-indexed-'+suffix
    env=os.environ.copy();env['NR_RENDER_PTL_RESIDENT_CLIP_BLOB']=str(v.ptl_resident_clip_blob);env['NR_RENDER_INTERACTIVE_PIPE']='1';env['NR_RENDER_PTL_FRAME_BLOB_DIR']=str(v.frame_blob_dir);env['NR_RENDER_PTL_COMPLEX_BLOB']=str(v.ptl_complex_blob);env['NR_RENDER_PTL_RECTANGLE_BLOB']=str(v.ptl_rectangle_blob);env['NR_RENDER_PTL_LONG_BLOB']=str(v.ptl_long_blob);env['NR_RENDER_PTL_PROGRAM_BLOB']=str(v.ptl_program_blob);env['NR_RENDER_PTL_MASKED_BLOB']=str(v.ptl_masked_blob);env['NR_RENDER_PTL_GEOMETRY_BLOB']=str(v.ptl_geometry_blob);env['NR_RENDER_PTL_TEXTURE_BLOB']=str(v.ptl_texture_blob);env['NR_RENDER_PTL_SOLID_BLOB']=str(v.ptl_solid_blob);env['NR_RENDER_PTL_FOUR_TAP_BLOB']=str(v.ptl_four_tap_blob);env['PATH']=str(v.runtime)+os.pathsep+env.get('PATH','');env.update(NR_RENDER_NPU_CACHE=str(v.cache),NR_RENDER_SHARED_QUEUE='1',NR_RENDER_PIPELINE_CACHE='retained',NR_RENDER_MEMORY_CACHE=v.memory_cache,NR_RENDER_MEMORY_CACHE_CAPACITY='512',NR_RENDER_GENERIC_BATCH=v.generic_batch,NR_RENDER_UNIFORM_LANES=v.uniform_lanes,NR_RENDER_TEXTURE_SCOPE=v.texture_scope,NR_RENDER_COMPACT_COVERAGE='0',NR_RENDER_FAILURE_PACKET=str(out/'failed-request.bin'),NR_RENDER_SHARED_CORE='1',NR_RENDER_OPAQUE_CROP='1',NR_RENDER_QUAD_BATCH='1')
    provider_root=out/'provider-processes';provider_root.mkdir(exist_ok=False)
    for key in ('NR_RENDER_RECOVERY_TEST_KILL_TICKET','NR_RENDER_RECOVERY_TEST_EXCEPTION_TICKET','NR_RENDER_PROVIDER_TEST_INFER_EXCEPTION_TICKET'):env.pop(key,None)
    env.update(NR_RENDER_PROVIDER_RECOVERY='1',NR_RENDER_PROVIDER_RECOVERY_DIR=str(provider_root),NR_RENDER_INFER_TRACE='0',NR_RENDER_COMPILE_TRACE='0',NR_RENDER_INFER_PROFILE='0')
    report.update(provider_isolated=True,provider_fault_injected=False,provider_timing_scope='Child roundtrip includes compilation, transport and any recovery.')
    capture=out/'requests';capture.mkdir(exist_ok=False);env['NR_RENDER_CAPTURE_DIRECTORY']=str(capture);env.pop('NR_RENDER_AFFINE_BATCH',None)
    report.update(frame_blob_dir=str(v.frame_blob_dir),frame_blob_manifest_sha256=hashlib.sha256((v.frame_blob_dir/'manifest.txt').read_bytes()).hexdigest(),request_recording_enabled=True,recording_byte_budget=17179869184,performance_measurement_valid=False,
                  affine_batch='default',memory_cache=v.memory_cache,memory_capacity='512',generic_batch=v.generic_batch,
                  uniform_lanes=v.uniform_lanes,texture_scope=v.texture_scope,pipeline_cache='retained',monitor_seconds=v.monitor_seconds,quad_batch='1',
                  ptl_resident_clip_model_sha256=hashlib.sha256(v.ptl_resident_clip_blob.read_bytes()).hexdigest(),ptl_complex_model_sha256=hashlib.sha256(v.ptl_complex_blob.read_bytes()).hexdigest(),ptl_rectangle_model_sha256=hashlib.sha256(v.ptl_rectangle_blob.read_bytes()).hexdigest(),ptl_long_model_sha256=hashlib.sha256(v.ptl_long_blob.read_bytes()).hexdigest(),ptl_program_model_sha256=hashlib.sha256(v.ptl_program_blob.read_bytes()).hexdigest(),ptl_masked_model_sha256=hashlib.sha256(v.ptl_masked_blob.read_bytes()).hexdigest(),ptl_geometry_model_sha256=hashlib.sha256(v.ptl_geometry_blob.read_bytes()).hexdigest(),ptl_texture_model_sha256=hashlib.sha256(v.ptl_texture_blob.read_bytes()).hexdigest(),ptl_solid_model_sha256=hashlib.sha256(v.ptl_solid_blob.read_bytes()).hexdigest(),ptl_four_tap_model_sha256=hashlib.sha256(v.ptl_four_tap_blob.read_bytes()).hexdigest(),worker_seconds=v.monitor_seconds+45,lease_milliseconds=(v.monitor_seconds+60)*1000,incremental_evidence=True)
    evidence=NativeEvidence(out,v.trace/f'process-{host.identity.pid}.jsonl')
    worker=subprocess.Popen([str(v.worker),pipe,'65536',str(v.monitor_seconds+45),str(compositor.identity.pid),str(compositor.identity.created),str(host.identity.pid),str(host.identity.created)],env=env,stdin=subprocess.DEVNULL,stdout=streams[2],stderr=streams[3],creationflags=subprocess.CREATE_NO_WINDOW)
    own_process(int(worker._handle),'worker');report['worker_pid']=worker.pid;save()
    evidence.poll()
    while not evidence.ready:
        assert worker.poll() is None and time.perf_counter()<ready_deadline,'Worker was not ready before native-host admission deadline';time.sleep(.01);evidence.poll()
    for handle in (k.GetCurrentProcess(),int(worker._handle)):
        for sid in (compositor.sid,host.sid):permissions.append(ProcessGrant(handle,sid))
    gate();compositor.verify();host.verify();assert time.perf_counter()<ready_deadline
    # Publish compositor first. Host Ready allows DriverEntry and monitor arrival.
    leases.append(LeaseGrant(int(worker._handle),compositor.handle,suffix,scope=1,desktop_session=session['console_session'],milliseconds=(v.monitor_seconds+60)*1000))
    leases.append(LeaseGrant(int(worker._handle),host.handle,suffix,scope=2,desktop_session=session['console_session'],milliseconds=(v.monitor_seconds+60)*1000))
    report['admission_published']=True;report['admission_publication_ms']=(time.perf_counter()-(ready_deadline-8))*1000;save()
    shared=SharedInteractiveApp(v.transaction,worker,suffix,session['console_session'],v.interactive_dll,env,own_process)
    admission_started=time.perf_counter();deadline=admission_started+v.monitor_seconds;last_gate=0
    while time.perf_counter()<deadline:
        assert not (v.transaction/'stop').exists(),'Controller requested native restoration'
        compositor.verify();host.verify()
        if time.perf_counter()-last_gate>=1:gate();last_gate=time.perf_counter()
        if installer.poll() is not None:assert installer.returncode==0,'Native indirect installation failed'
        assert worker.poll() is None,'NPU worker stopped before native frame evidence'
        evidence.poll();report.update(evidence.snapshot(compositor.identity.pid,host.identity.pid))
        shared.poll(evidence,compositor.identity.pid);report['shared_application_qualified']=shared.passed
        frame=frame_header(v.trace/f'latest-frame-{host.identity.pid}.bin',host.identity.pid)
        if frame:
            report['latest_frame']=frame
            key=(frame['producer_pid'],frame['sequence'],frame['qpc'])
            if key not in seen_frames:
                seen_frames.add(key);now=time.perf_counter()
                if first_frame_at is None:first_frame_at=now
                sample=dict(frame,observed=datetime.now(UTC).isoformat(),elapsed_ms=(now-admission_started)*1000)
                observed_frames.append(sample)
                with (out/'frames.jsonl').open('a') as stream:stream.write(json.dumps(sample)+'\n')
                report['observed_frame_count']=len(observed_frames)
                report['distinct_pixel_hashes']=len({x['pixel_sha256'] for x in observed_frames})
        matched_acks=evidence.ack_pids[compositor.identity.pid]>0
        save()
        if frame and not report['evidence_backlog'] and report['native_driver_entry'] and report['preferred_adapter_selected'] and report['successful_frame_readback'] and report['actual_dwm_npu_execution'] and matched_acks:
            report['passed']=True;report['native_indirect_frame_produced']=True
            report['multiple_frames_observed']=len(observed_frames)>=3
            report['sustained_rendering_qualified']=False
        time.sleep(.2)
    assert report['passed'] and shared.passed,'Native frame or shared default application evidence incomplete at trial deadline'
except Exception as error:report.update(passed=False,error=repr(error),traceback=traceback.format_exc())
finally:
    if install_attempted:
        try:remove_indirect()
        except Exception as error:report['remove_error']=repr(error);report['passed']=False
    if shared:
        try:shared.close();report['shared_application']=shared.report
        except Exception as error:report['shared_application_cleanup_error']=repr(error);report['passed']=False
    for lease in reversed(leases):lease.close()
    for grant in reversed(permissions):
        try:grant.restore()
        except Exception as error:report.setdefault('permission_restore_errors',[]).append(repr(error));report['passed']=False
    report['owned_process_acls_restored_exactly']=bool(permissions) and all(x.restored for x in permissions)
    for process,label in ((installer,'installer'),(worker,'worker')):
        if process is not None and process.poll() is None:
            process.kill();process.wait(timeout=5);report[label+'_terminated_after_device_removal']=True
        if process is not None:report[label+'_exit_code']=process.returncode
    for peer in (host,compositor):
        if peer:peer.close()
    for stream in streams:stream.close()
    report.update(completed=True,finished=datetime.now(UTC).isoformat());save()
print(json.dumps(report));raise SystemExit(0 if report['passed'] else 1)
