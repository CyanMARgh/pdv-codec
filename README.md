## requirements ##
- linux
- gcc
- python3
- zlib

## build ##
gcc pdv_codec.c -lm -o pdv_codec

## usage ##

`./pdv_codec -n <NUMBER OF FRAMES> -f <printf-like FORMAT OF FRAMES PATH> -o <OUTPUT FILE NAME>`

## working example ##

`./pdv_codec -b 0 -n 4 -f "source_frames/frame_%d.png" -m ffffffff:ffffffffffffffff -o land.pdv`


## extra arguments ##
- `-b INDEX` - begin index of frames (1 by default).
- `-m` mask in format aaaaaaaa:bbbbbbbbbbbbbbbb, where aaaaaaaa - hex code of color, that be replaced with palette bbbbbbbbbbbbbbbb (64-bit number, that represents 8x8 block of pixels). You can specify up to 16 masks. If you don't specify any, ffffffff:ffffffffffffffff will be selected automatically.
- `-no` - disable debug output.
- `-p` - write to stdout instead of creating file.