set pagination off
set confirm off
set breakpoint pending on
break cc_tinlayout_fused_pre_block_swin_1h_32_1_ds_fp8
run
set $kernel_entry = $pc
break *($kernel_entry + 0x1800)
disable 1
continue
python
import gdb,json,struct
rows=[]
for lane in range(32):
    gdb.execute("cuda thread (%d,0,0)"%lane,to_string=True)
    raw={str(r):int(gdb.parse_and_eval("$R%d"%r))&0xffffffff for r in (47,59,64)}
    rgb=[struct.unpack('<f',struct.pack('<I',raw[str(r)]))[0] for r in (47,64,59)]
    rows.append({'lane':lane,'pc':str(gdb.parse_and_eval('$pc')),'rgb':rgb,'raw':raw})
with open('release/native-temporal-inputs-gates/sample-registers.json','w') as f:
    json.dump({'scope':'original preblock PC1800 warp0 before half conversion; pixel mapping pending','rows':rows},f,indent=2)
print('Saved original temporal RGB reconstruction registers')
end
quit
