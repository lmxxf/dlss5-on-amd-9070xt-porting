"""Verify four/eight-wave matrix ownership matches the same output sets."""
for waves in [4,8]:
    for columns in [32,64]:
        coords=[]
        for wave in range(waves):
            query_wave=wave//2 if waves==8 else wave
            start=wave%2 if waves==8 else 0
            step=2 if waves==8 else 1
            for query in range(query_wave,4,4):
                for col in range(start,columns//16,step):
                    coords.extend((query*16+r,col*16+c) for r in range(16) for c in range(16))
        assert len(coords)==64*columns and len(set(coords))==len(coords)
        assert set(coords)=={(r,c) for r in range(64) for c in range(columns)}
print('four/eight-wave matrices cover all query/channel cells exactly once')
