set pagination off
set confirm off
set breakpoint pending on
break cc_tinlayout_fused_pre_block_swin_1h_32_1_ds_fp8
run
set $kernel_entry = $pc
python
import os
offset=int(os.environ.get('DLSS5_TEMPORAL_DEBUG_PC','0x1800'),0)
gdb.execute('break *($kernel_entry + %d)'%offset)
end
disable 1
continue
python
import gdb,json,struct
rows=[]
for lane in range(32):
    gdb.execute("cuda thread (%d,0,0)"%lane,to_string=True)
    raw={str(r):int(gdb.parse_and_eval("$R%d"%r))&0xffffffff for r in range(19,65)}
    rgb=[struct.unpack('<f',struct.pack('<I',raw[str(r)]))[0] for r in (47,64,59)]
    rows.append({'lane':lane,'pc':str(gdb.parse_and_eval('$pc')),'rgb':rgb,'raw':raw})
filename='sample-registers.json' if offset==0x1800 else 'sample-registers-%x.json'%offset
with open('release/native-temporal-inputs-gates/'+filename,'w') as f:
    json.dump({'scope':'original preblock warp0 register snapshot; PC-specific decoding required','pc_offset':offset,'rows':rows},f,indent=2)
print('Saved original temporal RGB reconstruction registers')
end
quit
