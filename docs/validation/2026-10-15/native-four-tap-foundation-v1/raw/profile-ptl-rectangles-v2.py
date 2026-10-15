"""Paired exact scalar/vector custom rendering with NPU task timestamps."""
from datetime import UTC,datetime
from pathlib import Path
import argparse,hashlib,json,os,subprocess,sys,time,traceback
from npu_render.ptl_carrier import read_kernel_text
from ptl_profile_contract_v1 import patch
VM=Path(os.environ['LOCALAPPDATA'])/'npu-render/vm'
def obj(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def write(p,v):p.write_text(json.dumps(v,indent=2)+'\n')
def child(out):
 import openvino as ov
 import numpy as np
 from ptl_rectangle_wide_oracle_v1 import fixture,reference
 report=dict(started=datetime.now(UTC).isoformat(),completed=False,passed=False,checks=[],health=[])
 try:
  assert ov.get_version()=='2026.4.1-22982-e213a147257-releases/2026/4';core=ov.Core()
  def imported(name):
   model=core.import_model((out/(name+'.blob')).read_bytes(),'NPU',{'PERF_COUNT':True});raw=model.get_property('EXECUTION_DEVICES');devices=[raw] if isinstance(raw,str) else list(raw);assert devices in (['NPU'],['NPU.0']);return model
  def health(name):
   model=imported('original');request=model.create_infer_request()
   for i in range(4):
    rng=np.random.default_rng(17000+i);data=rng.integers(-256,256,(524288,4)).astype(np.float16);indices=rng.integers(0,524288,4096,dtype=np.int32);indices[:2]=[0,524287];request.start_async({0:data,1:indices});assert request.wait_for(10000);actual=request.get_output_tensor().data.copy();assert np.array_equal(actual,data[indices])
   report['health'].append(dict(phase=name,passed=True));write(out/'child-report.json',report)
  health('original-before');models={name:imported(name) for name in ['scalar','vector']}
  for i in range(32):
   data=fixture(i,524288,4096);expected,_,_=reference(data);source=np.frombuffer(data,np.float16).reshape(524288,4).copy();indices=np.zeros(4096,np.int32)
   for name in (['scalar','vector'] if i%2==0 else ['vector','scalar']):
    request=models[name].create_infer_request();began=time.perf_counter_ns();request.start_async({0:source,1:indices});assert request.wait_for(10000);actual=request.get_output_tensor().data.copy();host_ms=(time.perf_counter_ns()-began)/1e6;assert np.array_equal(actual,expected.astype(np.float16)),f'{name} independent oracle mismatch'
    profile=[dict(name=p.node_name,kind=p.node_type,engine=p.exec_type,status=str(p.status),task_ms=p.real_time.total_seconds()*1000) for p in request.get_profiling_info()]
    assert len(profile)==1 and profile[0]['engine']=='Shave' and profile[0]['task_ms']>0
    report['checks'].append(dict(fresh_request=True,iteration=i,variant=name,host_request_output_ms=host_ms,profile=profile,exact_components=int(expected.size),input_sha256=hashlib.sha256(data).hexdigest(),output_sha256=hashlib.sha256(actual.tobytes()).hexdigest()))
  del request,models;health('original-after');report['passed']=True
 except Exception as error:report.update(error=repr(error),traceback=traceback.format_exc())
 finally:report.update(completed=True,finished=datetime.now(UTC).isoformat());write(out/'child-report.json',report)
 print(json.dumps({k:v for k,v in report.items() if k!='checks'}));return 0 if report['passed'] else 1
def main():
 p=argparse.ArgumentParser();p.add_argument('--child',type=Path);a=p.parse_args()
 if a.child:return child(a.child)
 out=VM/'ptl-rectangle-profile-v2';out.mkdir(exist_ok=False);process=None;report=dict(started=datetime.now(UTC).isoformat(),completed=False,passed=False,desktop_changed=False,tahoma_tested=False,variants=[])
 try:
  assert obj(VM/'ptl-rectangle-vector-v3/report.json')['passed'];assert obj(VM/'ptl-profile-carrier-v1/report.json')['passed']
  blob=(VM/'ptl-profile-carrier-v1/524288-4096/carrier.blob').read_bytes();assert hashlib.sha256(blob).hexdigest()=='85a88de5bf9e350ec8b68f7ff3902d1e652f1962296e37011cb9743db3e005d5';(out/'original.blob').write_bytes(blob)
  for name,build,digest in [('scalar','ptl-rectangle-wide-build-v1','e2371115e30b4a9d7bd0421966c5851b907355a1155380a7b8e7626bf5d11a1c'),('vector','ptl-rectangle-vector-build-v3','2ce1cb2271f50a77c1a8f5a0d20434b4af16cb02086539cdfe822d1d6358ede9')]:
   elf=(VM/build/'kernel.elf').read_bytes();assert hashlib.sha256(elf).hexdigest()==digest;modified,contract=patch(blob,read_kernel_text(elf),524288,4096,True);(out/(name+'.blob')).write_bytes(modified);write(out/(name+'-contract.json'),contract);report['variants'].append(dict(name=name,kernel_sha256=digest,patched_sha256=hashlib.sha256(modified).hexdigest()))
  with (out/'stdout.log').open('xb') as stdout,(out/'stderr.log').open('xb') as stderr:
   process=subprocess.Popen([sys.executable,__file__,'--child',str(out)],stdout=stdout,stderr=stderr,creationflags=subprocess.CREATE_NO_WINDOW)
   try:process.wait(timeout=60)
   except subprocess.TimeoutExpired:process.kill();process.wait(timeout=5);report['child_timeout']=True
  result=obj(out/'child-report.json');report.update(exit_code=process.returncode,passed=process.returncode==0 and result.get('passed',False),child_result=result)
 except Exception as error:report.update(error=repr(error),traceback=traceback.format_exc())
 finally:
  if process is not None and process.poll() is None:process.kill();process.wait(timeout=5)
  report.update(completed=True,finished=datetime.now(UTC).isoformat());write(out/'report.json',report)
 print(json.dumps({k:v for k,v in report.items() if k!='child_result'}));return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
