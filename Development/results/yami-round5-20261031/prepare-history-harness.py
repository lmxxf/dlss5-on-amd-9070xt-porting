from pathlib import Path
p=Path(__file__).parent;s=Path('Development/HIP/benchmark_live_capture.cpp').read_text();a='// Restore the SAME original HDR texture; never process the previous output recursively.';assert s.count(a)==1
b='''// Deterministic changing input for history regression; preserve binary16 sign/exponent and alpha.
 ck(up->Map(0,&none,&p));size_t input_changed=0;
 for(UINT y=0;y<H;y++){auto*row=reinterpret_cast<uint16_t*>(static_cast<char*>(p)+fp.Offset+size_t(y)*fp.Footprint.RowPitch);
  for(UINT x=0;x<W;x++)for(UINT c=0;c<4;c++){const uint16_t h=frozen[(size_t(y)*W+x)*4+c];const unsigned delta=c<3?i*(1u+(x+3u*y+c)%3u):0u;const uint16_t v=uint16_t((h&0xfc00u)|((unsigned(h)+delta)&0x03ffu));row[x*4+c]=v;input_changed+=v!=h;}}
 up->Unmap(0,nullptr);printf("R5_HISTORY_INPUT frame=%u changed_halves=%zu\\n",i,input_changed);
'''+a
s=s.replace(a,b).replace('1296x720','1280x720');(p/'frame-history-r5.cpp').write_text(s)
