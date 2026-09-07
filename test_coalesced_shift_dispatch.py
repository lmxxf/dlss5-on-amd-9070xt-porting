"""Check channel-flattened dispatch geometry, including the 65535 boundary."""
import numpy as np

for width,height,channels in [(480,288,64),(240,144,128),(120,72,256),(8,4,256)]:
    for shift in range(4):
        px=4 if shift&1 else 0
        py=4 if shift&2 else 0
        ww=(width+px+7)&~7
        wh=(height+py+7)&~7
        for pixels in [width*height,ww*wh]:
            count=pixels*channels
            groups=(count+63)//64
            gx=min(groups,65535)
            gy=(groups+65534)//65535
            actual=(np.arange(gy)[:,None]*65535+np.arange(gx)).ravel()
            assert np.array_equal(actual[actual<groups],np.arange(groups))
        # Crop only reads within the packed rectangle for every shift.
        p=np.arange(width*height)
        source=(p//width+py)*ww+p%width+px
        assert source.min()>=0 and source.max()<ww*wh
        assert len(np.unique(source))==width*height
print('coalesced shift dispatch and crop bounds passed')
