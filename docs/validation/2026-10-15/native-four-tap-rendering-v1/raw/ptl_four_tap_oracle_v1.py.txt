"""Independent NumPy oracle for quad derivatives and the captured four-tap math."""
import struct
import numpy as np
from ptl_rectangle_wide_oracle_v1 import fixture as one_sample

def fixture(iteration,elements,count):
    if elements!=4194304:
        old=one_sample(iteration,elements,count);fields=list(struct.unpack_from('<16I',old));data=bytearray(old);fields[1]=2
        sw,sh=fields[2:4];end=128+sw*sh*4+count*4;data[160:end+32]=old[128:end]
        screen=np.frombuffer(old,'<i4',4,96).astype(np.float32)/np.float32(256)
        if iteration%5==0:screen[1]+=np.float32(1/131072)
    else:
        rng=np.random.default_rng(142000+iteration);sw,sh,w,h=2880,1800,1024,768
        first=[0,64*1024,128*1024,384*1024,688*1024,704*1024,752*1024][iteration%7]
        fields=[0x4e525231,2,sw,sh,w,h,first,count,1,1,87,87,0,15,elements*8,0]
        data=bytearray(elements*8);data[64:80]=np.ones(4,'<f4').tobytes();data[80:96]=np.array([0,0,1,1],'<f4').tobytes()
        bounds=np.array([0,64,1024,704],'<i4')*256;data[96:112]=bounds.tobytes();data[112:128]=bounds.tobytes()
        screen=np.array([0,np.float32(64+1/131072),1024,704],'<f4')
        source=rng.integers(0,256,(sh,sw,4),dtype=np.uint8);source[:,:,:3]=np.minimum(source[:,:,:3],source[:,:,3:4]);data[160:160+source.size]=source.tobytes()
        background=rng.integers(0,256,(count,4),dtype=np.uint8);data[160+source.size:160+source.size+background.size]=background.tobytes()
    struct.pack_into('<16I',data,0,*fields);data[128:144]=screen.tobytes();struct.pack_into('<4I',data,144,iteration%2,0,0,0)
    return bytes(data)

def reference(data,dtype=np.float32):
    f=struct.unpack_from('<16I',data);magic,version,sw,sh,w,h,first,n,filtering,blend,sf,df,opaque,mask,size,reserved=f
    assert magic==0x4e525231 and version==2 and size==len(data) and reserved==0
    color=np.frombuffer(data,'<f4',4,64).astype(dtype);uv=np.frombuffer(data,'<f4',4,80).astype(dtype)
    bounds=np.frombuffer(data,'<i4',4,96);clip=np.frombuffer(data,'<i4',4,112);screen=np.frombuffer(data,'<f4',4,128).astype(dtype);barycentric=struct.unpack_from('<I',data,144)[0]
    source=np.frombuffer(data,np.uint8,sw*sh*4,160).reshape(sh,sw,4)
    bg=np.frombuffer(data,np.uint8,n*4,160+sw*sh*4).reshape(n,4)
    linear=np.arange(first,first+n,dtype=np.int32);px,py=linear%w,linear//w
    point=np.stack([px*256+128,py*256+128],1)
    covered=np.all((point>=bounds[:2])&(point<bounds[2:])&(point>=clip[:2])&(point<clip[2:]),axis=1)
    tri=(point[:,0]-bounds[0]).astype(dtype)*dtype(bounds[3]-bounds[1]) < (point[:,1]-bounds[1]).astype(dtype)*dtype(bounds[2]-bounds[0])
    def coordinate(x,y):
        x=x.astype(dtype)+dtype(.5);y=y.astype(dtype)+dtype(.5)
        if not barycentric:
            t=(np.stack([x,y],1)-screen[:2])/(screen[2:]-screen[:2]);return uv[:2]+t*(uv[2:]-uv[:2])
        ax=np.where(tri,screen[2],screen[0]);ay=np.where(tri,screen[3],screen[1]);bx=np.where(tri,screen[0],screen[2]);by=np.where(tri,screen[3],screen[1]);cx=np.where(tri,screen[0],screen[2]);cy=np.where(tri,screen[1],screen[3])
        def edge(ax,ay,bx,by):return (bx-ax)*(y-ay)-(by-ay)*(x-ax)
        w0,w1,w2=edge(bx,by,cx,cy),edge(cx,cy,ax,ay),edge(ax,ay,bx,by);den=(w0+w1)+w2
        ua=np.where(tri,uv[2],uv[0]);ub=np.where(tri,uv[0],uv[2]);uc=ub
        va=np.where(tri,uv[3],uv[1]);vb=va;vc=np.where(tri,uv[1],uv[3])
        return np.stack([((w0*ua+w1*ub)+w2*uc)/den,((w0*va+w1*vb)+w2*vc)/den],1)
    pos=coordinate(px,py);dx=coordinate(px|1,py)-coordinate(px&~1,py);dy=coordinate(px,py|1)-coordinate(px,py&~1)
    def sample(coord):
        coord=np.clip(np.clip(coord,dtype(0),dtype(1))*np.array([sw,sh],dtype=dtype)-dtype(.5 if filtering else 0),dtype(0),np.array([sw-1,sh-1],dtype=dtype))
        low=coord.astype(np.int32);high=np.minimum(low+1,[sw-1,sh-1]);lx,ly=low.T;hx,hy=high.T
        def texel(y,x):return source[y,x].astype(dtype)/dtype(255)
        value=texel(ly,lx)
        if filtering:
            tx,ty=(coord-low.astype(dtype)).T;tx=tx[:,None];ty=ty[:,None]
            top=value*(dtype(1)-tx)+texel(ly,hx)*tx;bottom=texel(hy,lx)*(dtype(1)-tx)+texel(hy,hx)*tx
            value=top*(dtype(1)-ty)+bottom*ty
        return value
    taps=[sample((dx*dtype(a)+pos)+dy*dtype(b)) for a,b in [(.375,-.125),(-.125,-.375),(-.375,.125),(.125,.375)]]
    a,b,c,d=taps;value=(d+((b+a)+c))*dtype(.25)
    if opaque:value[:,3]=dtype(1)
    alpha=value[:,3:4]*color[3];value=value[:,[2,1,0,3] if sf!=df else [0,1,2,3]]*color[[2,1,0,3] if df==87 else [0,1,2,3]]*dtype(255)
    if blend:value=value+bg.astype(dtype)*(dtype(1)-alpha)
    ideal=np.clip(value,dtype(0),dtype(255));apply=covered[:,None]&((np.array([1,2,4,8])&mask)!=0)
    return np.where(apply,np.rint(ideal).astype(np.uint8),bg),np.where(apply,ideal,bg),int(covered.sum())
