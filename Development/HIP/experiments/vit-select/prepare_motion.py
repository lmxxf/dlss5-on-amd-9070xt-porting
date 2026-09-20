"""Controlled 1-pixel translations, no temporal history. Quality diagnostics, never a timing benchmark."""
from pathlib import Path
import subprocess,sys
here=Path(__file__).resolve().parent
subprocess.run([sys.executable,str(here/'prepare_packed.py'),'--fragments'],check=True)
out=Path('/tmp/vit-select-src')
p=out/'Development/HIP/benchmark_live_capture.cpp';s=p.read_text()
s=s.replace(' std::ifstream source(argv[3]', ' const bool translate_frames=std::getenv("DLSS5_TEST_TRANSLATE")&&std::strcmp(std::getenv("DLSS5_TEST_TRANSLATE"),"0");if(translate_frames&&N>32)throw std::runtime_error("motion diagnostic limited to 32 frames");\n std::ifstream source(argv[3]',1)
needle='for(UINT i=0;i<N;i++){// Restore the SAME original HDR texture; never process the previous output recursively.'
assert needle in s
s=s.replace(needle,'''for(UINT i=0;i<N;i++){
 if(translate_frames){void*mp=nullptr;ck(up->Map(0,&none,&mp));for(UINT y=0;y<H;y++)for(UINT x=0;x<W;x++){UINT sx=(x+W-i%W)%W;memcpy(static_cast<char*>(mp)+fp.Offset+size_t(y)*fp.Footprint.RowPitch+size_t(x)*8,frozen.data()+(size_t(y)*W+sx)*4,8);}up->Unmap(0,nullptr);}
 // Restore this frame's input; motion diagnostics use a controlled translation of the capture.''')
s=s.replace('auto s=stats(last,W,H);','if(translate_frames)save_frame(prefix+L"-motion-"+std::to_wstring(i),last,W,H);auto s=stats(last,W,H);')
p.write_text(s)
p=out/'Development/HIP/hip_reference_network.h';s=p.read_text().replace('std::set<U>select_logged;','std::set<U>select_logged;U select_frame{};')
s=s.replace('const char*setting=std::getenv("DLSS5_VIT_SELECT_THRESHOLD");','if(block==31)++select_frame;const char*setting=std::getenv("DLSS5_VIT_SELECT_THRESHOLD");')
s=s.replace('path&&*path&&!select_logged.count(block)','path&&*path')
s=s.replace('%.9g,%.9g\\n",n,block,h,g,r[at],distance,threshold','%.9g,%.9g,%u\\n",n,block,h,g,r[at],distance,threshold,select_frame')
p.write_text(s)
