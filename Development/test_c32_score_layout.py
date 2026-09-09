"""Prove index coverage/capacity for reused score storage in both layouts."""
for row,wide in [(32,64),(33,65)]:
    capacity=128*row
    for parts,cols,stride in [(2,32,row),(1,64,wide),(1,32,row)]:
        scalar={part*64*stride+t*stride+c
                for part in range(parts) for t in range(64) for c in range(cols)}
        matrix={part*64*stride+qr*16*stride+cr*16+r*stride+c
                for part in range(parts) for wave in range(2)
                for qr in range(wave,4,2) for cr in range(cols//16)
                for r in range(16) for c in range(16)}
        assert scalar==matrix
        assert len(scalar)==parts*64*cols
        assert min(scalar)==0 and max(scalar)<capacity
print('C32 score matrix/scalar layouts agree; all phases fit shared capacity')
