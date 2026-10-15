"""Copy-only source windows for the exact captured geometry; full source is oracle."""
import struct
from ptl_four_tap_oracle_v1 import fixture as full_fixture,reference

def crop_fixture(iteration):
    full=full_fixture(iteration,4194304,16384);first=struct.unpack_from('<I',full,24)[0];y=first//1024
    if y<64 or y>=704:top,bottom=0,1
    else:top=max(0,(y-64)*45//16-4);bottom=min(1800,(y+16-64)*45//16+4)
    size=(bottom-top)*2880*4;raw=bytearray(1048576);raw[:160]=full[:160];struct.pack_into('<I',raw,4,3);struct.pack_into('<I',raw,56,len(raw));struct.pack_into('<4I',raw,160,0,top,2880,bottom-top)
    raw[192:192+size]=full[160+top*2880*4:160+bottom*2880*4]
    bg=160+2880*1800*4;raw[192+size:192+size+16384*4]=full[bg:bg+16384*4]
    assert 192+size+16384*4<=len(raw)
    return bytes(raw),full
