"""Exact NPU source-window test against an independent full-texture reference."""
from pathlib import Path
from datetime import UTC,datetime
import argparse,hashlib,json,os,struct,subprocess,sys,time,traceback
from npu_render.ptl_carrier import read_kernel_text
from ptl_crop_contract_v1 import patch
vm=Path(os.environ['LOCALAPPDATA'])/'npu-render/vm'
def obj(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def write(p,v):p.write_text(json.dumps(v,indent=2)+'\n')
def child(out):
 import openvino as ov
 import numpy as np
 from ptl_crop_fixture_v1 import crop_fixture,reference
 report=dict(started=datetime.now(UTC).isoformat(),completed=False,passed=False,health=[],checks=[])
 try:
  assert ov.get_version()=='2026.4.1-22982-e213a147257-releases/2026/4';core=ov.Core()
  def imported(name):
   model=core.import_model((out/(name+'.blob')).read_bytes(),'NPU');devices=model.get_property('EXECUTION_DEVICES');devices=[devices] if isinstance(devices,str) else list(devices);assert devices in (['NPU'],['NPU.0']);return model
  def health(phase):
   model=imported('original');request=model.create_infer_request()
   for i in range(8):
    rng=np.random.default_rng(153000+i);data=rng.integers(-128,128,(131072,4)).astype(np.float16);ix=rng.integers(0,131072,16384,dtype=np.int32);ix[:2]=[0,131071];request.start_async({0:data,1:ix});assert request.wait_for(10000);assert np.array_equal(request.get_output_tensor().data,data[ix])
   report['health'].append(dict(phase=phase,passed=True,exact_components=8*65536));write(out/'child-report.json',report)
  health('original-before');model=imported('patched');request=model.create_infer_request();indices=np.zeros(16384,np.int32)
  for i in range(32):
   raw,full=crop_fixture(i);expected,ideal,covered=reference(full);source=np.frombuffer(raw,np.float16).reshape(131072,4).copy();began=time.perf_counter_ns();request.start_async({0:source,1:indices});assert request.wait_for(10000);actual=request.get_output_tensor().data.copy();elapsed=(time.perf_counter_ns()-began)/1e6
   if not np.array_equal(actual,expected.astype(np.float16)):
    report['mismatch']=dict(iteration=i,positions=np.argwhere(actual!=expected)[:16].tolist(),actual=actual[actual!=expected][:16].astype(float).tolist(),expected=expected[actual!=expected][:16].astype(float).tolist());raise AssertionError('full-source oracle mismatch')
   report['checks'].append(dict(iteration=i,exact_components=int(expected.size),covered_pixels=covered,request_ms=elapsed,input_sha256=hashlib.sha256(raw).hexdigest(),full_input_sha256=hashlib.sha256(full).hexdigest(),output_sha256=hashlib.sha256(actual.tobytes()).hexdigest(),crop=struct.unpack_from('<4I',raw,160)))
  bad,full=crop_fixture(1);bad=bytearray(bad);struct.pack_into('<I',bad,164,1000);source=np.frombuffer(bad,np.float16).reshape(131072,4).copy();request.start_async({0:source,1:indices});assert request.wait_for(10000);actual=request.get_output_tensor().data.copy();assert np.all(actual==np.float16(-1)),'out-of-window sample must fail explicitly';report['out_of_window_rejected_components']=int(actual.size)
  del request,model;health('original-after');report['passed']=True
 except Exception as error:report.update(error=repr(error),traceback=traceback.format_exc())
 finally:report.update(completed=True,finished=datetime.now(UTC).isoformat());write(out/'child-report.json',report)
 print(json.dumps({k:v for k,v in report.items() if k!='checks'}));return 0 if report['passed'] else 1

def main():
 p=argparse.ArgumentParser();p.add_argument('--child',type=Path);a=p.parse_args()
 if a.child:return child(a.child)
 out=vm/'ptl-four-tap-crop-v3';out.mkdir(exist_ok=False);process=None;report=dict(started=datetime.now(UTC).isoformat(),completed=False,passed=False,desktop_changed=False,tahoma_tested=False)
 try:
  assert obj(vm/'ptl-four-tap-v1/report.json')['passed'];assert obj(vm/'ptl-crop-carrier-v1/report.json')['passed'];q=obj(vm/'ptl-crop-contract-v1-qualification.json');assert q['passed'] and len(q['checks'])==17
  for name,digest in q['source_sha256'].items():assert hashlib.sha256((vm/name).read_bytes()).hexdigest()==digest
  original=(vm/'ptl-crop-carrier-v1/131072-16384/carrier.blob').read_bytes();assert hashlib.sha256(original).hexdigest()=='21449fdc8700c8b8684436ee1db05d66382bdbc2ad809ea24c5a31214b086e80'
  elf=(vm/'ptl-four-tap-crop-v2-build/kernel.elf').read_bytes();assert hashlib.sha256(elf).hexdigest()=='219c987fbfb116062d43181ce08a37d2eb91e037569f6dc400081d36d226c7a8'
  modified,contract=patch(original,read_kernel_text(elf),131072,16384);(out/'original.blob').write_bytes(original);(out/'patched.blob').write_bytes(modified);write(out/'contract.json',contract);report.update(patched_sha256=hashlib.sha256(modified).hexdigest(),kernel_sha256=hashlib.sha256(elf).hexdigest())
  with (out/'stdout.log').open('xb') as stdout,(out/'stderr.log').open('xb') as stderr:
   process=subprocess.Popen([sys.executable,__file__,'--child',str(out)],stdout=stdout,stderr=stderr,creationflags=subprocess.CREATE_NO_WINDOW)
   try:process.wait(timeout=90)
   except subprocess.TimeoutExpired:process.kill();process.wait(timeout=5);report['child_timeout']=True
  result=obj(out/'child-report.json');report.update(exit_code=process.returncode,child_result=result,passed=process.returncode==0 and result.get('passed',False))
 except Exception as error:report.update(error=repr(error),traceback=traceback.format_exc())
 finally:
  if process is not None and process.poll() is None:process.kill();process.wait(timeout=5)
  report.update(completed=True,finished=datetime.now(UTC).isoformat());write(out/'report.json',report)
 print(json.dumps({k:v for k,v in report.items() if k!='child_result'}));return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
