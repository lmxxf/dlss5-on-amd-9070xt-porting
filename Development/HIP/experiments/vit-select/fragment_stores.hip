 // Native gfx12 WMMA fragment order: 32 lanes x 8 bytes per 16x16 operand tile.
 for(uint tile=0;tile<2&&first+tile*16<tokens;tile++)for(uint step=0;step<2;step++){
  uint at=((h*(tokens/16)+first/16+tile)*2+step)*256+c*8;i2 k{},v{};
  __builtin_memcpy(&k,sk+(tile*16+rc())*36+step*16+gr()*8,8);
  for(uint e=0;e<8;e++){uint x=sv[(tile*16+gr()*8+e)*36+step*16+rc()];v[e/4]=int(uint(v[e/4])|(x<<(8*(e%4))));}
  __builtin_memcpy(pool+at,&k,8);__builtin_memcpy(pool+tokens*1024+at,&v,8);
 }
 for(uint step=0;step<2;step++){
  uint at=((h*(PT/16)+g)*2+step)*256+c*8;i2 k{},v{};
  __builtin_memcpy(&k,spk+rc()*36+step*16+gr()*8,8);
  for(uint e=0;e<8;e++){uint x=spv[(gr()*8+e)*36+step*16+rc()];v[e/4]=int(uint(v[e/4])|(x<<(8*(e%4))));}
  __builtin_memcpy(pool+2*tokens*1024+at,&k,8);__builtin_memcpy(pool+(2*tokens+PT)*1024+at,&v,8);
 }
