# Proof of concept mipmap generator

Compress in 4 by 4 tiles. Up to 16 by 16.

##How it works for RGBA8 images:
####Version one. For premultiplied alpha.
1) Sum two neighbour horisontal pixels and store as RGBA16. Size of array is same.
2) Sum two neighbout vertical pixels, divide by 4 and store in RGBA8. Size of array is one-fourth of original
