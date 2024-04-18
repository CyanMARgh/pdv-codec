## what it is? ##
This is CLI utility that converts set of frames into a .pdv file (video format for the Playadate console). Instead of dithering it replaces sepicified colors with black&white patterns. Also, it can process images the same way.

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

`./pdv_codec -f "source_frames/frame_0.png" -i -o image.png -m ff0000ff:f0f0f0f00f0f0f0f -m ffffffff:ffffffffffffffff`

## arguments ##
- `-o` output file name.
- `-n NUMBER` number of frames.
- `-f` input file name / input file names format for video.
- `-b INDEX` - begin index of frames (1 by default).
- `-m` mask in format aaaaaaaa:bbbbbbbbbbbbbbbb, where aaaaaaaa - hex code of color, that be replaced with palette bbbbbbbbbbbbbbbb (64-bit number, that represents 8x8 block of pixels). You can specify up to 16 masks. If you don't specify any, ffffffff:ffffffffffffffff will be selected automatically. In case of images, fully-transparent pixels remain fully-transparent.
- `-no` - disable debug output.
- `-p` - write to stdout instead of creating file.
- `-i` - process single image instead of video.