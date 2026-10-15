"""Qualify unmodified larger Gather carriers before inspecting direct-kernel spans."""
from datetime import UTC, datetime
from pathlib import Path
import argparse, hashlib, json, os, subprocess, sys, time, traceback
from ptl_elf_v1 import Elf

VM=Path(os.environ['LOCALAPPDATA'])/'npu-render/vm'
CASES=[(4194304,16384)]
def write(p,v):p.write_text(json.dumps(v,indent=2)+'\n')
def obj(p):return json.loads(p.read_text(encoding='utf-8-sig'))

def child(out,elements,count):
 import openvino as ov
 from openvino import opset13 as op
 import numpy as np
 report=dict(started=datetime.now(UTC).isoformat(),completed=False,passed=False,elements=elements,count=count,custom_kernel_executed=False)
 try:
  assert ov.get_version()=='2026.4.1-22982-e213a147257-releases/2026/4';core=ov.Core()
  source=op.parameter([elements,4],ov.Type.f16,name='source');indices=op.parameter([count],ov.Type.i32,name='indices');graph=ov.Model([op.gather(source,indices,op.constant(np.int64(0)))],[source,indices])
  config=dict(PERFORMANCE_HINT='LATENCY',NPU_COMPILATION_MODE='ReferenceSW',NPU_TILES=1)
  report.update(stage='compile',config=config);write(out/'report.json',report)
  began=time.perf_counter_ns();model=core.compile_model(graph,'NPU',config);report['compile_ms']=(time.perf_counter_ns()-began)/1e6
  devices=model.get_property('EXECUTION_DEVICES');devices=[devices] if isinstance(devices,str) else list(devices);assert devices in (['NPU'],['NPU.0']);report.update(execution_devices=devices,stage='export')
  blob=model.export_model().getvalue();(out/'carrier.blob').write_bytes(blob);report.update(blob_sha256=hashlib.sha256(blob).hexdigest(),blob_bytes=len(blob))
  elf=Elf(blob)
  try:
   metadata=elf.describe(3);write(out/'layout.json',metadata);report.update(invocations=len(metadata['invocations']),ranges=len(metadata['ranges']))
  except ValueError as error:
   report['narrow_inspector_rejected']=str(error);write(out/'sections.json',[{k:v for k,v in s.items() if k!='data'} for s in elf.sections])
  report['stage']='infer';write(out/'report.json',report)
  request=model.create_infer_request();checks=[]
  for iteration in range(4):
   rng=np.random.default_rng(11000+iteration);data=rng.integers(-256,256,(elements,4)).astype(np.float16);ix=rng.integers(0,elements,count,dtype=np.int32);ix[:4]=[0,elements-1,elements//2,elements//2-1];expected=data[ix].copy()
   began=time.perf_counter_ns();request.start_async({0:data,1:ix});assert request.wait_for(10000),'inference deadline';actual=request.get_output_tensor(0).data.copy();elapsed=(time.perf_counter_ns()-began)/1e6
   assert actual.dtype==expected.dtype and actual.shape==expected.shape and np.array_equal(actual,expected),'independent indexed output oracle'
   checks.append(dict(iteration=iteration,request_ms=elapsed,exact_components=int(expected.size),input_sha256=hashlib.sha256(data.tobytes()).hexdigest(),output_sha256=hashlib.sha256(actual.tobytes()).hexdigest()))
  report.update(passed=True,checks=checks,stage='complete',max_tiles=str(core.get_property('NPU','NPU_MAX_TILES')))
 except Exception as error:report.update(error=repr(error),traceback=traceback.format_exc())
 finally:report.update(completed=True,finished=datetime.now(UTC).isoformat());write(out/'report.json',report)
 print(json.dumps(report));return 0 if report['passed'] else 1

def main():
 p=argparse.ArgumentParser();p.add_argument('--child',type=Path);p.add_argument('--elements',type=int);p.add_argument('--count',type=int);a=p.parse_args()
 if a.child:return child(a.child,a.elements,a.count)
 out=VM/'ptl-desktop-carrier-v1';out.mkdir(exist_ok=False);report=dict(started=datetime.now(UTC).isoformat(),completed=False,passed=False,custom_kernel_executed=False,desktop_changed=False,cases=[]);process=None
 try:
  assert obj(VM/'ptl-rectangle-v1/report.json')['passed']
  for elements,count in CASES:
   dest=out/f'{elements}-{count}';dest.mkdir();process=None
   with (dest/'stdout.log').open('xb') as stdout,(dest/'stderr.log').open('xb') as stderr:
    process=subprocess.Popen([sys.executable,__file__,'--child',str(dest),'--elements',str(elements),'--count',str(count)],stdout=stdout,stderr=stderr,creationflags=subprocess.CREATE_NO_WINDOW)
    try:process.wait(timeout=90)
    except subprocess.TimeoutExpired:process.kill();process.wait(timeout=5);report['child_timeout']=True
   result=obj(dest/'report.json') if (dest/'report.json').exists() else dict(passed=False,completed=False)
   result['child_exit']=process.returncode;report['cases'].append(result);write(out/'report.json',report)
   if not result.get('passed') and result.get('stage')!='compile':raise AssertionError('non-compilation failure requires inspection before next hardware call')
  report['passed']=all(x['passed'] and x['child_exit']==0 for x in report['cases'])
 except Exception as error:report.update(error=repr(error),traceback=traceback.format_exc())
 finally:
  if process is not None and process.poll() is None:process.kill();process.wait(timeout=5)
  report.update(completed=True,finished=datetime.now(UTC).isoformat());write(out/'report.json',report)
 print(json.dumps(report));return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
