"""Admit only the observed CMX/DDR Gather layouts, preserving cache maintenance."""
import hashlib, struct
from ptl_elf_v1 import Elf, check

CASES={(8192,4096),(65536,4096),(262144,4096),(524288,4096),(524288,16384),(1048576,16384),(4194304,16384)}

def inspect(blob,elements,count):
 check((elements,count) in CASES,'observed shape pair');elf=Elf(blob);ddr=elements>=262144;prefix=64 if ddr else 0;records=2 if ddr else 1
 check(not any('dpu' in s['name'].lower() for s in elf.sections),'no DPU carrier')
 tasks=[s for s in elf.sections if s['name'].startswith('task.shave.')]
 check({s['name'] for s in tasks}=={'task.shave.range.0.0','task.shave.invocation.0.0'},'only owned ACT lists')
 ranges=elf.by_name['task.shave.range.0.0'];calls=elf.by_name['task.shave.invocation.0.0'];params=elf.by_name['shave.params'];code=elf.by_name['shave.text'];data=elf.by_name['shave.data']
 check(ranges['size']==records*40 and calls['size']==records*96,'bounded task arrays')
 check(all(s['type']==1 for s in (ranges,calls,params,code,data)) and not elf.relocations(code),'file-backed code and metadata')
 rs=elf.relocations(ranges);cr=elf.relocations(calls);pr=elf.relocations(params)
 check(rs=={(56 if ddr else 16):dict(kind=0,section='shave.text',addend=0)},'one executable code reference')
 expected_cr={8+96*(records-1):dict(kind=0,section='shave.params',addend=prefix),16+96*(records-1):dict(kind=0,section='shave.data',addend=0)}
 if ddr:
  expected_cr[8]=dict(kind=0,section='shave.params',addend=0)
  check(struct.unpack_from('<B7xQQIIII',ranges['data'])==(7,0,0,0,0,0,0),'preserved firmware flush/invalidate task')
 check(cr==expected_cr,'only expected invocation references')
 for i in range(records):check(struct.unpack_from('<I',calls['data'],i*96+84)[0]==0,'single tile')
 kind,entry,base,size,deprecated,unused,pad=struct.unpack_from('<B7xQQIIII',ranges['data'],(records-1)*40)
 check((kind,entry,base,deprecated,unused,pad)==(0,0x1d000000,0,0,0,0) and 0<size<=code['size'],'fixed kernel entry and extent')
 fields={prefix+i*40+j for i in range(3) for j in (16,20)}
 if ddr:fields.add(prefix)
 check(set(pr)==fields,'exact parameter relocations')
 tensors=[];addresses=[]
 for i,(dims,strides,dtype,order) in enumerate([([4,elements],[16,64],2,0x21),([count],[32],9,1),([4,count],[16,64],2,0x21)]):
  at=prefix+i*40;address,static,rank,dp,sp,kind,layout,location=struct.unpack_from('<QIIIIIQI',params['data'],at)
  check((static,rank,dp,sp,kind,layout,location)==(1,len(dims),0,0,dtype,order,1 if ddr and i==0 else 2),'static dense tensor descriptor')
  for field,width,fmt,expected in [(16,rank*4,'<'+str(rank)+'i',dims),(20,rank*8,'<'+str(rank)+'q',strides)]:
   rel=pr[at+field];check(rel['kind']==4 and rel['section']=='shave.params' and prefix+120<=rel['addend'] and rel['addend']+width<=params['size'],'shape array bounds')
   check(list(struct.unpack_from(fmt,params['data'],rel['addend']))==expected,'full tensor shape and stride')
  addresses.append(address);tensors.append(dict(descriptor_offset=at,address=address,location=location,dims=dims,strides_bits=strides,dtype=dtype,address_relocation=pr.get(at)))
 if ddr:
  buffer=elf.by_name['buffer.DDR.0'];check(buffer['type']==8 and buffer['size']==elements*8,'full DDR source allocation')
  check(addresses[0]==0x200000 and pr[prefix]==dict(kind=4,section='buffer.DDR.0',addend=0),'32-bit relocated source base')
  expected=[0x200000,0x40218000,0x40218000+count*4]
 else:expected=[0x40218000,0x40218000+elements*8,0x40218000+elements*8+count*4]
 check(addresses==expected,'disjoint full source/index/output allocations')
 check(addresses[2]+count*8<=0x40400000,'bounded CMX output')
 return dict(accepted=True,blob_sha256=hashlib.sha256(blob).hexdigest(),elements=elements,count=count,source_bytes=elements*8,output_bytes=count*8,source_location='DDR' if ddr else 'CMX',cache_maintenance_preserved=ddr,parameter_offset=prefix,code_offset=code['offset'],code_bytes=size,tensors=tensors,hardware_execution_qualified=False)

def patch(blob,text,elements,count):
 contract=inspect(blob,elements,count);check(0<len(text)<=contract['code_bytes'] and len(text)%16==0,'replacement extent');start=contract['code_offset'];size=contract['code_bytes'];modified=blob[:start]+text+bytes(size-len(text))+blob[start+size:]
 check(len(modified)==len(blob) and modified[:start]==blob[:start] and modified[start+size:]==blob[start+size:],'all non-code bytes preserved');inspect(modified,elements,count);return modified,contract
