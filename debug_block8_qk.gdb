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
quit
