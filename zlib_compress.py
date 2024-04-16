#!/bin/python3
import sys
import zlib
from time import sleep

decompressed_data = sys.stdin.buffer.read()
compressed_data = zlib.compress(decompressed_data, 1)
sys.stdout.buffer.write(compressed_data)

# sys.stdout.buffer.write(b'bruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruhbruh');


# Yes, that's all the python script does. I thought about using gzip or something similar,
# but their compression results are slightly different and I honestly don't understand why.
# The output data coincides by about 99% if you remove the header of the file,
# but where this 1% comes from is an unknown mystery to me, which I don't want to spend
# more time on and don’t see much point in it. If you want - you are welcome.
