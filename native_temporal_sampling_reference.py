"""Five-tap temporal RGB reconstruction inferred from original preblock SASS.

Coordinates are pixel centers AFTER motion/transform. This does not yet model
CUDA texture interpolation precision or the preceding motion coordinate path.
"""
import numpy as np

def axis(position, extent):
    p=np.asarray(position,dtype=np.float64)
    center=np.floor(p-.5)+.5
    t=np.clip(p-center,0,1)
    t2=t*t;t3=t2*t
    left=t2-.5*(t+t3)
    inner_left=1+1.5*t3-2.5*t2
    right=.5*(t3-t2)
    inner_right=((1-left)-inner_left)-right
    middle=inner_left+inner_right
    # Original combines adjacent central samples into one bilinear lookup.
    positions=[center-1,center+inner_right/middle,center+2]
    return np.stack([np.clip(x,.5,extent-.5) for x in positions],axis=-1),np.stack([left,middle,right],axis=-1)

def geometry(x,y,width,height):
    px,wx=axis(x,width);py,wy=axis(y,height)
    # SASS fetch order: top, left, center, bottom, right.
    xy=np.stack([np.stack([px[...,1],py[...,0]],-1),
                 np.stack([px[...,0],py[...,1]],-1),
                 np.stack([px[...,1],py[...,1]],-1),
                 np.stack([px[...,1],py[...,2]],-1),
                 np.stack([px[...,2],py[...,1]],-1)],axis=-2)
    weights=np.stack([wx[...,1]*wy[...,0],wx[...,0]*wy[...,1],
                      wx[...,1]*wy[...,1],wx[...,1]*wy[...,2],wx[...,2]*wy[...,1]],-1)
    return xy,weights/weights.sum(axis=-1,keepdims=True)

def bilinear(image,xy,fraction_bits=None,product_bits=None):
    image=np.asarray(image,dtype=np.float64);h,w,_=image.shape
    x=np.clip(xy[...,0]-.5,0,w-1);y=np.clip(xy[...,1]-.5,0,h-1)
    x0=np.floor(x).astype(int);y0=np.floor(y).astype(int)
    x1=np.minimum(x0+1,w-1);y1=np.minimum(y0+1,h-1)
    a=(x-x0)[...,None];b=(y-y0)[...,None]
    if fraction_bits is not None:
        scale=2**fraction_bits
        a=np.rint(a*scale)/scale;b=np.rint(b*scale)/scale
    if product_bits is not None:
        corner=np.rint((1-a)*(1-b)*2**product_bits)/2**product_bits
        adjacent_x=(1-b)-corner;adjacent_y=(1-a)-corner
        weights=np.stack([corner,adjacent_x,adjacent_y,a-adjacent_x],axis=-2)
        pixels=np.stack([image[y0,x0],image[y0,x1],image[y1,x0],image[y1,x1]],axis=-2)
        return (weights*pixels).sum(axis=-2)
    return (image[y0,x0]*(1-a)+image[y0,x1]*a)*(1-b)+(image[y1,x0]*(1-a)+image[y1,x1]*a)*b

def sample(image,x,y):
    h,w,_=image.shape;xy,weight=geometry(x,y,w,h)
    return (bilinear(image,xy)*weight[...,None]).sum(axis=-2)

def fma32(a,b,c):
    return (np.asarray(a,np.float32).astype(np.float64)*np.asarray(b,np.float32).astype(np.float64)+np.asarray(c,np.float32).astype(np.float64)).astype(np.float32)

def axis32(position,extent):
    p=np.asarray(position,np.float32)
    center=(np.floor(p-np.float32(.5))+np.float32(.5)).astype(np.float32)
    t=np.clip(p-center,0,1).astype(np.float32);t2=t*t;t3=t2*t
    left=fma32(t+t3,-.5,t2)
    inner_left=fma32(t3,1.5,-(t2*np.float32(2.5)))+np.float32(1)
    right=(t3-t2)*np.float32(.5)
    inner_right=((np.float32(1)-left)-inner_left)-right
    middle=inner_left+inner_right
    position=fma32(inner_right,np.float32(1)/middle,center)
    return np.stack([np.clip(v,.5,extent-.5) for v in (center-1,position,center+2)],-1),np.stack([left,middle,right],-1)

def sample32(image,x,y):
    """SASS-ordered candidate; MUFU reciprocal and TEX rounding still audited."""
    h,w,_=image.shape;px,wx=axis32(x,w);py,wy=axis32(y,h)
    xy=np.stack([np.stack([px[...,i],py[...,j]],-1) for i,j in ((1,0),(0,1),(1,1),(1,2),(2,1))],-2)
    weights=np.stack([wx[...,i]*wy[...,j] for i,j in ((1,0),(0,1),(1,1),(1,2),(2,1))],-1)
    pixels=bilinear(image,xy,8,8).astype(np.float32)
    total=pixels[...,0,:]*weights[...,0,None]
    for i in range(1,5):total=fma32(pixels[...,i,:],weights[...,i,None],total)
    denominator=weights[...,1]+weights[...,0]
    for i in range(2,5):denominator=denominator+weights[...,i]
    return total*(np.float32(1)/denominator)[...,None]

if __name__=='__main__':
    import json
    rng=np.random.default_rng(7302);image=rng.random((8,8,3))
    y,x=np.mgrid[:8,:8];identity=sample(image,x+.5,y+.5)
    assert np.array_equal(identity,image)
    px=rng.uniform(-2,10,1000);py=rng.uniform(-2,10,1000)
    xy,weights=geometry(px,py,8,8)
    assert np.isfinite(xy).all() and np.isfinite(weights).all()
    constant=sample(np.full((8,8,3),.25),px,py)
    assert np.max(np.abs(constant-.25))<1e-15
    assert np.max(np.abs(weights.sum(-1)-1))<1e-15
    print(json.dumps({'pixel_center_identity':True,'constant_preservation':True,
                      'scope':'mathematical reference invariants only; original numerical and GPU validation pending'}))
