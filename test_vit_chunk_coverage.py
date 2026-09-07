"""Check old/doubled expand chunks cover exactly the same 16-token tiles."""
for tokens in [64,256,640]:
    expected=list(range(0,tokens,16))
    for chunk_values in [65536,131072]:
        output_count=tokens*4096
        tiles=[]
        for base in range(0,output_count,chunk_values):
            size=min(chunk_values,output_count-base)
            assert size%(4096*16)==0 and base%4096==0
            tiles.extend(base//4096+group*16 for group in range(size//4096//16))
        assert tiles==expected
print('ViT expand chunk sizes cover same token/output domain without overlap')
