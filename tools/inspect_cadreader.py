"""Read-only PE evidence: source file offsets, imports and selected strings.

This is a bounded static inventory, not decompilation or execution evidence.
"""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path


def inspect(path):
    data = path.read_bytes()
    u16 = lambda p: struct.unpack_from('<H', data, p)[0]
    u32 = lambda p: struct.unpack_from('<I', data, p)[0]
    pe = u32(0x3c)
    if data[:2] != b'MZ' or data[pe:pe+4] != b'PE\0\0':
        raise ValueError('Not PE')
    coff = pe + 4
    opt = coff + 20
    is64 = u16(opt) == 0x20b
    base = struct.unpack_from('<Q' if is64 else '<I', data, opt + (24 if is64 else 28))[0]
    sections = []
    for i in range(u16(coff+2)):
        p = opt + u16(coff+16) + 40*i
        sections.append(dict(name=data[p:p+8].rstrip(b'\0').decode('ascii', 'replace'),
                             virtual_size=u32(p+8), rva=u32(p+12), size=u32(p+16), offset=u32(p+20)))

    def rva_offset(rva):
        for s in sections:
            if s['rva'] <= rva < s['rva'] + s['size']:
                return s['offset'] + rva - s['rva']
        raise ValueError(f'Unmapped RVA {rva:x}')

    def location(p):
        for s in sections:
            if s['offset'] <= p < s['offset'] + s['size']:
                return dict(file_offset=hex(p), rva=hex(s['rva'] + p - s['offset']),
                            va=hex(base + s['rva'] + p - s['offset']), section=s['name'])
        return dict(file_offset=hex(p))

    def cstring(p):
        return data[p:data.index(b'\0', p)].decode('ascii', 'replace')

    imports = []
    directory = opt + (112 if is64 else 96)
    import_rva = u32(directory+8)
    if import_rva:
        p = rva_offset(import_rva)
        while u32(p+12):
            dll = cstring(rva_offset(u32(p+12)))
            thunk = rva_offset(u32(p) or u32(p+16))
            iat = u32(p+16)
            funcs = []
            width = 8 if is64 else 4
            for i in range(20000):
                v = struct.unpack_from('<Q' if is64 else '<I', data, thunk+width*i)[0]
                if not v:
                    break
                funcs.append(dict(name=f'ordinal:{v & 0xffff}' if v >> (width*8-1) else cstring(rva_offset(v)+2),
                                  iat_va=hex(base+iat+width*i)))
            imports.append(dict(dll=dll, functions=funcs))
            p += 20

    pattern = re.compile(r'dwg|dxf|teigha|opendesign|open design|oddb|odgs|odrx|odgi|readfile|openfile|open_file|openDwg|zoomext|zoomall|FileOpen|DrawingFile|AC10|RecentFile', re.I)
    strings = []
    for m in re.finditer(rb'[\x20-\x7e]{5,}', data):
        value = m.group().decode('ascii')
        if pattern.search(value) and len(value) < 800:
            strings.append(dict(encoding='ascii', value=value, **location(m.start())))
    for m in re.finditer(rb'(?:[\x20-\x7e]\x00){5,}', data):
        value = m.group().decode('utf-16le')
        if pattern.search(value) and len(value) < 800:
            strings.append(dict(encoding='utf-16le', value=value, **location(m.start())))
    return dict(evidence_id='local-pe-'+hashlib.sha256(data).hexdigest()[:16], path=str(path),
                sha256=hashlib.sha256(data).hexdigest(), size=len(data), machine=hex(u16(coff)),
                image_base=hex(base), sections=sections, imports=imports, selected_strings=strings,
                limitations=['Static file inventory only; no execution, call graph or recovered function bodies.',
                             'Strings are bounded to the documented pattern; absence is not proof of missing behavior.',
                             'Import table excludes delay-load imports and dynamically resolved APIs.'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('target', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    result = inspect(args.target)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(dict(evidence_id=result['evidence_id'], sha256=result['sha256'],
                          imports=len(result['imports']), selected_strings=len(result['selected_strings']))))
