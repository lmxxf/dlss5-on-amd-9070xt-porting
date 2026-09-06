"""Original preblock motion path when optional scalar texture slot18 is absent.

Returns pixel-center coordinates for temporal sampling. float32 division is a
candidate for MUFU.RCP, not yet certified at arbitrary actual-image extents.
"""
import numpy as np
from native_temporal_sampling_reference import bilinear,fma32

def coordinates(motion,valid_width,valid_height,processing_width,processing_height,
                motion_offset=(0,0),motion_extent=None,motion_uv_scale=None):
    h,w,c=motion.shape
    if c<2 or min(valid_width,valid_height,w,h)<2:raise ValueError('motion shape')
    if processing_width>2*valid_width-2 or processing_height>2*valid_height-2:raise ValueError('unverified reflection range')
    extent=np.asarray(motion_extent or (w,h),np.float32)
    scale=np.asarray(motion_uv_scale or (1/valid_width,1/valid_height),np.float32)
    y,x=np.mgrid[:processing_height,:processing_width]
    x=np.where(x<valid_width,x,2*valid_width-x-2)
    y=np.where(y<valid_height,y,2*valid_height-y-2)
    pixel=np.stack([x+.5,y+.5],-1).astype(np.float32)
    uv=pixel*(np.float32(1)/np.asarray([valid_width,valid_height],np.float32))
    sample_uv=fma32(uv,extent,np.asarray(motion_offset,np.float32))*(np.float32(1)/np.asarray([w,h],np.float32))
    vectors=bilinear(motion[:,:,:2],sample_uv*np.asarray([w,h],np.float32),8,8).astype(np.float32)
    previous_uv=fma32(vectors,scale,uv)
    return previous_uv*np.asarray([valid_width,valid_height],np.float32)
