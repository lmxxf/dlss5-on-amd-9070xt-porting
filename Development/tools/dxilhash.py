# DXBC/DXIL container hash (DxilHash.cpp ComputeHashRetail): MD5 whose final length block is (bitlen, (bitlen>>2)|1).
import struct,sys
S=[7,12,17,22]*4+[5,9,14,20]*4+[4,11,16,23]*4+[6,10,15,21]*4
K=[int(abs(__import__('math').sin(i+1))*2**32)&0xffffffff for i in range(64)]
def rotl(x,c):return ((x<<c)|(x>>(32-c)))&0xffffffff
def block(st,b):
    a,bb,c,d=st;M=struct.unpack('<16I',b)
    for i in range(64):
        if i<16:f=(bb&c)|(~bb&d);g=i
        elif i<32:f=(d&bb)|(~d&c);g=(5*i+1)%16
        elif i<48:f=bb^c^d;g=(3*i+5)%16
        else:f=c^(bb|~d);g=(7*i)%16
        f=(f+a+K[i]+M[g])&0xffffffff;a,d,c,bb=d,c,bb,(bb+rotl(f,S[i]))&0xffffffff
    return [(x+y)&0xffffffff for x,y in zip(st,(a,bb,c,d))]
def dxil_hash(data):
    # DxilHash.cpp ComputeHashRetail: last row = [bitlen][data remainder][0x80,0..][1|(n<<1)]; two rows when leftover >= 56
    st=[0x67452301,0xefcdab89,0x98badcfe,0x10325476];n=len(data);left=n&0x3f
    two=left>=56;pad_amount=(120-left) if two else (56-left);N=(n+pad_amount+8)>>6;padding=bytes([0x80])+bytes(127)
    off=0
    for i in range(N):
        if not two and i==N-1:
            rem=n-off;row=struct.pack('<I',(n<<3)&0xffffffff)+data[off:off+rem]+padding[:pad_amount]+struct.pack('<I',(1|(n<<1))&0xffffffff)
        elif two and i==N-2:
            rem=n-off;row=data[off:off+rem]+padding[:pad_amount-56]
        elif two and i==N-1:
            row=struct.pack('<I',(n<<3)&0xffffffff)+padding[pad_amount-56:pad_amount]+struct.pack('<I',(1|(n<<1))&0xffffffff)
        else: row=data[off:off+64]
        assert len(row)==64,(i,len(row));st=block(st,row);off+=64
    return struct.pack('<4I',*st)
for name in sys.argv[1:]:
    d=bytearray(open(name,'rb').read());d[4:20]=dxil_hash(bytes(d[20:]));out=name.replace('.cso','_h.cso');open(out,'wb').write(d);print(out,d[4:20].hex())
