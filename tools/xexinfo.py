import struct, sys
d = open(sys.argv[1], 'rb').read()
magic, flags, pe_off, _, sec_off, nhdr = struct.unpack('>4sIIIII', d[:24])
print(magic, hex(flags), 'pe_off', hex(pe_off), 'sec', hex(sec_off), 'nhdr', nhdr)
for i in range(nhdr):
    key, val = struct.unpack('>II', d[24 + i*8: 32 + i*8])
    print(f'  hdr {key:08X} = {val:08X}')
    if key == 0x000003FF:
        size, enc, comp = struct.unpack('>IHH', d[val:val+8])
        print('    fileformat size', size, 'enc', enc, 'comp', comp)
sec = d[sec_off:]
img_size = struct.unpack('>I', sec[4:8])[0]
print('image size', hex(img_size), 'load addr', hex(struct.unpack('>I', sec[0x110:0x114])[0]))
