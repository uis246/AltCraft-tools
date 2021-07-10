# Proof of concept mipmap generator

Compress in 4 by 4 tiles. Up to 16 by 16.

## How it works for RGBA8 images:

#### Version one. For premultiplied alpha.
1) Sum two neighbour horisontal pixels and store as RGBA16. Size of array is same.
2) Sum two neighbout vertical pixels, divide by 4 and store in RGBA8. Size of array is one-fourth of original

#### This implementatin. PMA internally.
1) Expand to RGBA8 to RGBA16 PMA
2) Vertical halfsum of pixels(vhadd in neon)
3) Horisontal halfsum of pixels
4) Divide by alpha and write to RGBA8
5) Export as PNG
6) if (i < 4) goto 2
