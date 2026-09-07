#include "native_codec_constants_events.cpp"
int main(){
 uint32_t words[16]={1920,1080,1920,1080,0,0,1920,1080,0x3f800000,0x3f800000,0x3f800000,0};
 uint32_t saved[16];std::memcpy(saved,words,sizeof(words));
 if(!candidate(0,16,words)||candidate(1,16,words)||candidate(0,15,words)||candidate(0,16,nullptr))return 1;
 for(uint32_t mode=0;mode<3;mode++){words[11]=mode;if(!candidate(0,16,words))return 2;}
 words[11]=3;if(candidate(0,16,words))return 3;
 words[11]=0;if(std::memcmp(saved,words,sizeof(words)))return 4;
 puts("codec candidate filter passed; no live shader identity proof");return 0;
}
