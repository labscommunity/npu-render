"""Qualify the protocol-3 DLL with real registered D3D11 allocations."""
from native_session_broker_v1 import *
import _winapi,traceback,ctypes,base64
from ctypes import wintypes as W
from native_session_broker_v3 import ProcessGrant,process_sid,k as broker_kernel,adv as broker_adv
from native_token_query_grant_v1 import TokenQueryGrant
a=argparse.ArgumentParser()
for name in ('worker','client','dll','package','runtime','cache','output'):a.add_argument('--'+name,type=Path,required=True)
v=a.parse_args();v.output.mkdir(exist_ok=False);out=v.output
for path in (v.worker,v.client):
 b=obj(path.parent.parent/'registered-build-report.json');assert b['clean_build'] and hashlib.sha256(path.read_bytes()).hexdigest()==b['binary_sha256'][path.name].lower()
package=obj(v.package/'report.json');entry=next(x for x in package['files'] if x['name']=='nr_native_probe_umd.dll')
assert package['prepared'] and package['version']=='0.0.1325.0' and package['admission_protocol']==3 and package['hardware_id']=='ROOT\\NPURENDER_NATIVE_PROBE' and hashlib.sha256(v.dll.read_bytes()).hexdigest()==entry['sha256'].lower()
env=os.environ.copy();env['PATH']=str(v.runtime)+os.pathsep+env.get('PATH','');env.update(NR_RENDER_NPU_CACHE=str(v.cache),NR_RENDER_SHARED_QUEUE='1',NR_RENDER_PIPELINE_CACHE='retained',NR_RENDER_MEMORY_CACHE='1',NR_RENDER_MEMORY_CACHE_CAPACITY='512',NR_RENDER_TEXTURE_SCOPE='1',NR_RENDER_GENERIC_BATCH='4096',NR_RENDER_UNIFORM_LANES='0',NR_RENDER_COMPACT_COVERAGE='0',NR_RENDER_SHARED_CORE='1',NR_RENDER_OPAQUE_CROP='1',NR_RENDER_QUAD_BATCH='1')
model=Path(os.environ['LOCALAPPDATA'])/'npu-render/vm/ptl-six-span-general-v1/patched.blob'
assert hashlib.sha256(model.read_bytes()).hexdigest()=='710f6e5586da59f4040213435c5ecc13bb669e6b1be668f838edd00bca5dd697'
env['NR_RENDER_PTL_FOUR_TAP_BLOB']=str(model)
solid=Path(os.environ['LOCALAPPDATA'])/'npu-render/vm/ptl-six-span-solid-v3/patched.blob'
assert hashlib.sha256(solid.read_bytes()).hexdigest()=='21360321c21d4a0253b260ce62420ffef47ec03daae8b89e6cf05bd4c541ee8f'
env['NR_RENDER_PTL_SOLID_BLOB']=str(solid)
texture=Path(os.environ['LOCALAPPDATA'])/'npu-render/vm/ptl-six-span-three-texture-v1/patched.blob'
assert hashlib.sha256(texture.read_bytes()).hexdigest()=='481765dd51d8d9b051b87db8be7e62ae7931245fe051b33b6457be77816c2e87'
env['NR_RENDER_PTL_TEXTURE_BLOB']=str(texture)
geometry=Path(os.environ['LOCALAPPDATA'])/'npu-render/vm/ptl-six-span-geometry-v2/patched.blob'
assert hashlib.sha256(geometry.read_bytes()).hexdigest()=='ea4432abd7b548396b86665c1b318353e0d2f3ec847bcd3e6f56c10bd1336dfa'
env['NR_RENDER_PTL_GEOMETRY_BLOB']=str(geometry)
masked=Path(os.environ['LOCALAPPDATA'])/'npu-render/vm/ptl-six-span-masked-v1/patched.blob'
assert hashlib.sha256(masked.read_bytes()).hexdigest()=='81104c9ea8760bbeb7cc028ea2a9c81e8605482af219f57c082c737b9662d1e0'
env['NR_RENDER_PTL_MASKED_BLOB']=str(masked)
program=Path(os.environ['LOCALAPPDATA'])/'npu-render/vm/ptl-six-span-program-compact-v1/patched.blob'
assert hashlib.sha256(program.read_bytes()).hexdigest()=='92e04a6734dc6e70ffed15b67fddba02892c66168aa0d47a6cf4bcc65a6a585d'
env['NR_RENDER_PTL_PROGRAM_BLOB']=str(program)
long_model=Path(os.environ['LOCALAPPDATA'])/'npu-render/vm/ptl-six-span-long-shader-v2/patched.blob'
assert hashlib.sha256(long_model.read_bytes()).hexdigest()=='895831be2695bc151454833249996608f38d35880c635900bb731515bc2bcda1'
env['NR_RENDER_PTL_LONG_BLOB']=str(long_model)
complex_model=Path(os.environ['LOCALAPPDATA'])/'npu-render/vm/ptl-six-span-complex-shader-v1/patched.blob'
assert hashlib.sha256(complex_model.read_bytes()).hexdigest()=='47f69b5861732a03014b58160116ebbb928fd6e818a11b7f553e423a4cf3c43a'
env['NR_RENDER_PTL_COMPLEX_BLOB']=str(complex_model)
rectangle_model=Path(os.environ['LOCALAPPDATA'])/'npu-render/vm/ptl-six-span-point-rectangle-v2/patched.blob'
assert hashlib.sha256(rectangle_model.read_bytes()).hexdigest()=='d651d6101dbccc5796873dc2016ec9e23d262cd955e9bc4bee6d56b86756eb2d'
env['NR_RENDER_PTL_RECTANGLE_BLOB']=str(rectangle_model)
resident=Path(os.environ['LOCALAPPDATA'])/'npu-render/vm/ptl-six-span-resident-clip-v3/patched.blob'
assert hashlib.sha256(resident.read_bytes()).hexdigest()=='a5c39dbb825fdd4bd334d8bcbef966e9404132d9b642375665a0892adc48c45c'
env['NR_RENDER_PTL_RESIDENT_CLIP_BLOB']=str(resident)
env['NR_RENDER_INTERACTIVE_PIPE']='1'
env['NR_RENDER_PTL_FRAME_BLOB_DIR']=str(Path(os.environ['LOCALAPPDATA'])/'npu-render/vm/frame-blobs-v1/deploy')
env.pop('NR_RENDER_AFFINE_BATCH',None)
for key in ('NR_RENDER_RECOVERY_TEST_KILL_TICKET','NR_RENDER_RECOVERY_TEST_EXCEPTION_TICKET','NR_RENDER_PROVIDER_TEST_INFER_EXCEPTION_TICKET'):env.pop(key,None)
provider_root=out/'provider-processes';provider_root.mkdir(exist_ok=False)
env.update(NR_RENDER_PROVIDER_RECOVERY='1',NR_RENDER_PROVIDER_RECOVERY_DIR=str(provider_root),NR_RENDER_INFER_TRACE='1',NR_RENDER_COMPILE_TRACE='1')
report=dict(worker_binary_sha256=hashlib.sha256(v.worker.read_bytes()).hexdigest(),dll_sha256=hashlib.sha256(v.dll.read_bytes()).hexdigest(),started=datetime.now(UTC).isoformat(),completed=False,passed=False,controller_pid=os.getpid(),driver_installed=True,registered_adapter_tested=False,desktop_offloaded=False,quad_batch='1',texture_scope='1',memory_cache='1',generic_batch='4096')
def save():(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
save();worker=None;hp=ht=None;lease=None;broker_process=None;grants=[];token_handles=[];task=None
root=Path(os.environ['LOCALAPPDATA'])/'npu-render';vm=root/'vm'
broker=root/'builds/native-auto-interactive-v7/Release/nr_auto_interactive_broker.exe'
assert hashlib.sha256(broker.read_bytes()).hexdigest()=='3d64a9136af00050dd3b8ed7c452f8ccd994cf5307962ebfa74d7f0b4d4bb41c'
# The frame-batch worker is identified by its clean build report (checked above), not the previous worker lineage.
assert obj(vm/'interactive-pipe-diagnostic-v2/report.json')['passed'] and obj(vm/'native-auto-interactive-v7-qualification/report.json')['passed']
def grant_owned(process,sid_text):
 grants.append(ProcessGrant(int(process._handle),sid_text));token=W.HANDLE()
 assert broker_adv.OpenProcessToken(int(process._handle),8|0x20000|0x40000,ctypes.byref(token))
 token_handles.append(token);grants.append(TokenQueryGrant(token,sid_text))
def psquote(value):return "'"+str(value).replace("'","''")+"'"
def powershell(script):return subprocess.run(['powershell.exe','-NoProfile','-NonInteractive','-EncodedCommand',base64.b64encode(script.encode('utf-16-le')).decode()],capture_output=True,timeout=25,creationflags=subprocess.CREATE_NO_WINDOW)
broker_kernel.WTSGetActiveConsoleSessionId.restype=W.DWORD
broker_kernel.OpenFileMappingW.argtypes=[W.DWORD,W.BOOL,W.LPCWSTR];broker_kernel.OpenFileMappingW.restype=W.HANDLE
def absent(name):
 handle=broker_kernel.OpenFileMappingW(4,False,name)
 if handle:broker_kernel.CloseHandle(handle)
 assert not handle and ctypes.get_last_error()==2

try:
 negative=subprocess.run([str(v.client),str(v.dll),'--reject-unleased'],env=env,capture_output=True,timeout=15);(out/'unleased.stdout.jsonl').write_bytes(negative.stdout);(out/'unleased.stderr.log').write_bytes(negative.stderr)
 assert negative.returncode==0 and any(x.get('unleased_native_adapter_rejected') and x.get('passed') for x in rows(out/'unleased.stdout.jsonl'));report['unleased_rejection_passed']=True;save()
 suffix=secrets.token_hex(16);pipe=r'\\.\pipe\npu-render-indexed-'+suffix
 with (out/'worker.stdout.jsonl').open('xb') as ws,(out/'worker.stderr.log').open('xb') as we:
  worker=subprocess.Popen([str(v.worker),pipe,'65536','120'],env=env,stdin=subprocess.DEVNULL,stdout=ws,stderr=we,creationflags=subprocess.CREATE_NO_WINDOW);report['worker_pid']=worker.pid
  until=time.perf_counter()+15
  while not any(x.get('local_worker_ready') for x in rows(out/'worker.stdout.jsonl')):
   assert time.perf_counter()<until and worker.poll() is None;time.sleep(.02)
  session=int(broker_kernel.WTSGetActiveConsoleSessionId());assert session not in (0,0xffffffff)
  sid_text=process_sid(broker_kernel.GetCurrentProcess())[1];grant_owned(worker,sid_text)
  case=out/'interactive';case.mkdir()
  acl=subprocess.run(['icacls.exe',str(case),'/grant','*'+sid_text+':(OI)(CI)M'],capture_output=True,timeout=10);(out/'interactive-output-acl.log').write_bytes(acl.stdout+acl.stderr);assert acl.returncode==0
  with (out/'broker.stdout.jsonl').open('xb') as bs,(out/'broker.stderr.log').open('xb') as be:
   broker_process=subprocess.Popen([str(broker),str(worker.pid),suffix,str(session)],env=env,stdin=subprocess.DEVNULL,stdout=bs,stderr=be,creationflags=subprocess.CREATE_NO_WINDOW)
   until=time.perf_counter()+5
   while not any(x.get('automatic_broker_ready') for x in rows(out/'broker.stdout.jsonl')):
    assert time.perf_counter()<until and broker_process.poll() is None;time.sleep(.01)
   grant_owned(broker_process,sid_text)
   job=dict(output=str(case),binary=str(v.client),binary_sha256=hashlib.sha256(v.client.read_bytes()).hexdigest(),dll=str(v.dll),dll_sha256=hashlib.sha256(v.dll.read_bytes()).hexdigest(),session=session,worker_pipe=pipe)
   jobpath=case/'job.json';jobpath.write_text(json.dumps(job,indent=2)+'\n')
   task='T8-NpuRender-InteractiveGraphicsV2';arguments=subprocess.list2cmdline([str(vm/'run-native-interactive-graphics-v2.py'),str(jobpath)])
   script="$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';if(Get-ScheduledTask -TaskName "+psquote(task)+" -ErrorAction SilentlyContinue){throw 'Task already exists'};"+"$action=New-ScheduledTaskAction -Execute "+psquote(root/'toolchains/openvino-2026.4.1/Scripts/pythonw.exe')+" -Argument "+psquote(arguments)+";"+"$principal=New-ScheduledTaskPrincipal -UserId "+psquote(sid_text)+" -LogonType Interactive -RunLevel Limited;"+"$settings=New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 2) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries;"+"Register-ScheduledTask -TaskName "+psquote(task)+" -Action $action -Principal $principal -Settings $settings|Out-Null;Start-ScheduledTask -TaskName "+psquote(task)
   launch=powershell(script);(out/'task.stdout.log').write_bytes(launch.stdout);(out/'task.stderr.log').write_bytes(launch.stderr);assert launch.returncode==0
   until=time.perf_counter()+112
   while not (case/'consumer-report.json').exists():assert time.perf_counter()<until;time.sleep(.05)
   consumer=obj(case/'consumer-report.json');report['interactive_consumer']=consumer;save();assert consumer['completed'] and consumer['passed'] and not consumer['elevated'] and consumer['session']==session
   pid=consumer['consumer_pid'];report.update(consumer_pid=pid,consumer_exit_code=consumer['consumer_exit_code'],consumer_started_suspended=False,automatic_admission=True,preauthorized_consumer_pid=False,broker_pid=broker_process.pid,consumer_session=session,consumer_elevated=False,pipe_server_creation_access_denied=consumer['pipe_server_creation_access_denied'])
   broker_process.wait(timeout=8);assert broker_process.returncode==0
  for name in ['consumer.stdout.jsonl','consumer.stderr.log']:(out/name).write_bytes((case/name).read_bytes())
  broker_records=rows(out/'broker.stdout.jsonl');admitted=[x for x in broker_records if x.get('automatic_consumer_admitted')];received=[x for x in broker_records if x.get('automatic_request_received')];closed=[x for x in broker_records if x.get('automatic_broker_completed')]
  assert len(admitted)==len(received)==len(closed)==1 and admitted[0]['client_pid']==pid and admitted[0]['session']==session and admitted[0]['worker_pid']==worker.pid and admitted[0]['actual_pipe_client_verified'] and closed[0]['owned_mappings_removed'] and closed[0]['passed']
  assert received[0]['actual_client_pid']==pid and received[0]['actual_session']==session and all(received[0][n] for n in ['creation_matches','sid_matches','signature_matches','nonce_matches'])
  absent('Global\\T8.NpuRender.AutoAdmission.v2.'+str(session));absent('Global\\T8.NpuRender.NativeSession.v3.'+str(pid))
  report.update(automatic_broker_receipts=1,automatic_broker_cleanup_passed=True,admission_protocol=3,lease_scope='normally started non-elevated interactive client; scope three; finite lease')
  evidence=rows(out/'consumer.stdout.jsonl');done=[x for x in evidence if x.get('native_registered_component')]
  assert len(done)==1 and done[0]['passed'] and done[0]['npu_local_executions']==8 and done[0]['clears']==6 and done[0]['draws']==2 and done[0]['normal_d3d11_runtime'] and done[0]['registered_adapter_tested'] and not done[0]['allocation_callbacks_emulated'] and done[0]['cpu_executions']==done[0]['gpu_executions']==0
  worker.terminate();worker.wait(timeout=10);logs=rows(out/'worker.stdout.jsonl');renders=[x for x in logs if x.get('passed') and x.get('execution_device')=='NPU'];acks=[x for x in logs if x.get('worker_success')];finish=[x for x in logs if x.get('local_worker_finished')]
  assert 1<=len(renders)==len(acks)<=8 and all(x['execution_device']=='NPU' and x['client_pid']==pid for x in renders);report['npu_requests_received']=len(renders)
  report['worker_finished_rows']=len(finish);assert not (out/'worker.stderr.log').read_bytes()
  child_logs=[x for path in provider_root.glob('generation-*/worker.stdout.jsonl') for x in rows(path)]
  child_renders=[x for x in child_logs if x.get('passed') and x.get('execution_device')=='NPU']
  assert len(child_renders)==len(renders) and all(x['client_pid']==worker.pid for x in child_renders);report['frame_batches']=len([x for x in child_logs if x.get('frame_batch')])
  report.update(isolated_provider=True,hardware_child_receipts=len(child_renders),provider_generations=len(list(provider_root.glob('generation-*'))),device_loss_recovery_tested=False)
  report.update(passed=True,registered_adapter_tested=True,component=done[0],npu_operation_receipts=len(renders),delivery_acknowledgments=len(acks),worker_exit_code=worker.returncode,worker_terminated_after_client=True)
except Exception as e:report.update(error=repr(e),traceback=traceback.format_exc())
finally:
 restored=[]
 for grant in reversed(grants):
  try:grant.restore();restored.append(grant.restored)
  except Exception as error:restored.append(False);report.setdefault('restore_errors',[]).append(repr(error))
 report['owned_process_and_token_acls_restored_exactly']=bool(grants) and all(restored)
 for token in token_handles:broker_kernel.CloseHandle(token)
 if broker_process is not None and broker_process.poll() is None:broker_process.kill();broker_process.wait(timeout=5)
 if task:
  cleanup=powershell("$ErrorActionPreference='Stop';Stop-ScheduledTask -TaskName "+psquote(task)+" -ErrorAction SilentlyContinue;Unregister-ScheduledTask -TaskName "+psquote(task)+" -Confirm:$false -ErrorAction SilentlyContinue;if(Get-ScheduledTask -TaskName "+psquote(task)+" -ErrorAction SilentlyContinue){throw 'Owned task remains'}")
  report['interactive_task_removed']=cleanup.returncode==0
 report['passed']=report['passed'] and report['owned_process_and_token_acls_restored_exactly'] and report.get('interactive_task_removed',False)
 if hp is not None:
  if _winapi.WaitForSingleObject(hp,0)!=0:_winapi.TerminateProcess(hp,1);_winapi.WaitForSingleObject(hp,5000);report['owned_consumer_terminated']=True
  _winapi.CloseHandle(hp)
 if ht is not None:_winapi.CloseHandle(ht)
 if worker is not None and worker.poll() is None:worker.kill();worker.wait(timeout=5);report['owned_worker_terminated']=True
 if lease:lease.close()
 report.update(completed=True,finished=datetime.now(UTC).isoformat());save()
print(json.dumps(report))
if not report['passed']:raise SystemExit(1)
