#!/bin/python3
import sys
import zlib

sys.stdout.buffer.write(zlib.compress(sys.stdin.buffer.read(), 1))

# Yes, that's all the python script does. I thought about using gzip or something similar,
# but their compression results are slightly different and I honestly don't understand why.
# The output data coincides by about 99% if you remove the header of the file,
# but where this 1% comes from is an unknown mystery to me, which I don't want to spend
# more time on and don’t see much point in it. If you want - you are welcome.
