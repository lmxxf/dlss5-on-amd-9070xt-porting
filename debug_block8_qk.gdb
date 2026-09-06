set pagination off
set confirm off
set breakpoint pending on
break cc_tinlayout_fused_swin_2h_64_2_fp8
run
set $kernel_entry = $pc
break *($kernel_entry + 0x5030)
disable 1
continue
cuda thread (0,1,0)
info registers pc
info registers R8 R9 R10 R11 R80 R81 R84 R85
python
import gdb,json
rows=[]
for lane in range(32):
    gdb.execute("cuda thread (%d,1,0)"%lane,to_string=True)
    values={str(r):int(gdb.parse_and_eval("$R%d"%r)) & 0xffffffff for r in range(160)}
    rows.append({"lane":lane,"registers":values})
path="release/native-rgb-valid1080/encoder-c64/window46-18/qk-registers-5030.json"
with open(path,"w") as f:json.dump({"scope":"original kernel PC+5030 head1 registers; tensor decoding pending","rows":rows},f,indent=2)
print("Saved32 lanes at original QK boundary")
end
quit
