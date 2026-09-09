// Inputs are finite half values. Their squares are exact in float32.
// Recover the sum residual so float32 midpoint rounding cannot alter half RNE.
float NativeHalfSquarePair(float lower,float upper){
 precise float a=lower*lower;
 precise float b=H(upper*upper);
 precise float sum=a+b;
 if(!isfinite(sum))return H(sum);
 precise float large=max(a,b),small=min(a,b);
 precise float recovered=sum-large;
 precise float error=small-recovered;
 if(sum==65520.0)return error<0?65504.0:H(sum);
 float rounded=H(sum);
 if(error==0||!isfinite(rounded)||rounded==sum)return rounded;
 uint bits=f32tof16(rounded);
 uint lo_bits=sum<rounded?bits-1:bits;
 float lo=f16tof32(lo_bits),hi=f16tof32(lo_bits+1);
 precise float midpoint=(lo+hi)*0.5;
 return sum==midpoint?(error>0?hi:lo):rounded;
}
