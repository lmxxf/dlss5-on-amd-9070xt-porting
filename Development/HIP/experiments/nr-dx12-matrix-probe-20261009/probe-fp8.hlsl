// Same raw row-major FP8 A/B and FP32 C as wmma_validate; no conversions on input.
// Four outputs distinguish C placement and K32 versus two K16 operations.
#include <dx/linalg.h>
ByteAddressBuffer inputA:register(t0);
ByteAddressBuffer inputB:register(t1);
ByteAddressBuffer inputC:register(t2);
RWByteAddressBuffer output:register(u0);
using A32=dx::linalg::Matrix<dx::linalg::ComponentType::F8_E4M3FN,16,32,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
using B32=dx::linalg::Matrix<dx::linalg::ComponentType::F8_E4M3FN,32,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
using A16=dx::linalg::Matrix<dx::linalg::ComponentType::F8_E4M3FN,16,16,dx::linalg::MatrixUse::A,dx::linalg::MatrixScope::Wave>;
using B16=dx::linalg::Matrix<dx::linalg::ComponentType::F8_E4M3FN,16,16,dx::linalg::MatrixUse::B,dx::linalg::MatrixScope::Wave>;
using C=dx::linalg::Matrix<dx::linalg::ComponentType::F32,16,16,dx::linalg::MatrixUse::Accumulator,dx::linalg::MatrixScope::Wave>;
void StorePair(C m,uint base){m.Store(output,base,64,dx::linalg::MatrixLayout::RowMajor,16);m.Cast<dx::linalg::ComponentType::F16>().Store(output,base+1024,32,dx::linalg::MatrixLayout::RowMajor,16);}
[WaveSize(32)] [numthreads(32,1,1)]
void main(uint3 group:SV_GroupID){
 uint ab=group.x*512,cb=group.x*1024,ob=group.x*6144;
 A32 a=A32::Load(inputA,ab,32,dx::linalg::MatrixLayout::RowMajor,16);
 B32 b=B32::Load(inputB,ab,16,dx::linalg::MatrixLayout::RowMajor,16);
 C initial=C::Load(inputC,cb,64,dx::linalg::MatrixLayout::RowMajor,16);
 C full=initial;full.MultiplyAccumulate(a,b);StorePair(full,ob);
 A16 a0=A16::Load(inputA,ab,32,dx::linalg::MatrixLayout::RowMajor,16);
 B16 b0=B16::Load(inputB,ab,16,dx::linalg::MatrixLayout::RowMajor,16);
 A16 a1=A16::Load(inputA,ab+16,32,dx::linalg::MatrixLayout::RowMajor,16);
 B16 b1=B16::Load(inputB,ab+256,16,dx::linalg::MatrixLayout::RowMajor,16);
 C split=initial;split.MultiplyAccumulate(a0,b0);StorePair(split,ob+3072);
 split.MultiplyAccumulate(a1,b1);StorePair(split,ob+1536);
 C late=dx::linalg::Multiply<dx::linalg::ComponentType::F32>(a,b);
 for(uint i=0;i<late.Length();++i)late.Set(i,late.Get(i)+initial.Get(i));
 StorePair(late,ob+4608);
}
