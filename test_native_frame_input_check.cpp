#include "native_frame_input_check.h"
#include <cstdio>
int main(){
 std::vector<unsigned char>b(1920ull*1080*8);
 if(CheckNativeFrameInput(b)!=NativeFrameInputCheck::black)return 1;
 b[1]=0x80;b[7]=0x3c; // Negative RGB zero and nonzero alpha are still black.
 if(CheckNativeFrameInput(b)!=NativeFrameInputCheck::black)return 2;
 b[1]=0x3c;if(CheckNativeFrameInput(b)!=NativeFrameInputCheck::valid)return 3;
 b[7]=0x7e;if(CheckNativeFrameInput(b)!=NativeFrameInputCheck::nonfinite)return 4;
 b.resize(1);if(CheckNativeFrameInput(b)!=NativeFrameInputCheck::wrong_size)return 5;
 if(!NativeFrameRequestValid(7,7,2,1)||NativeFrameRequestValid(7,8,2,1)||NativeFrameRequestValid(7,7,1,1)||NativeFrameRequestValid(7,7,0,0)||NativeFrameRequestValid(7,7,0xffffffff,1))return 6;
 puts("frame guard: signed zero/alpha do not bypass black rejection; finite/size checks pass");
}
