set pagination off
set confirm off
set breakpoint pending on
break cc_tinlayout_fused_post_block_swin_1h_32_fp8
run
set $kernel_entry = $pc
python
import os,gdb,json
offset=int(os.environ['DLSS5_POST_DEBUG_PC'],0)
gdb.execute('break *($kernel_entry + %d)'%offset)
gdb.execute('condition 2 blockIdx.x == 1 && blockIdx.y == 0 && threadIdx.y == 1')
end
disable 1
continue
python
rows=[]
for lane in range(32):
    gdb.execute('cuda block (1,0,0) thread (%d,1,0)'%lane,to_string=True)
    rows.append({'lane':lane,'pc':str(gdb.parse_and_eval('$pc')),
        'raw':{str(k):int(gdb.parse_and_eval('$R%d'%k))&0xffffffff for k in range(128)}})
with open('release/native-temporal-valid1080/post70/crop/registers-%x.json'%offset,'w') as f:
    json.dump({'scope':'original post70 crop block1,row0,warp1; PC-specific decoding required','rows':rows},f,indent=2)
end
quit
