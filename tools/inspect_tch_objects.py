"""Inventory every literal TCH_ identifier in one installed PE (read only).

Records exact case, all ASCII/UTF-16LE occurrences and static PE locations.
An identifier is evidence of a stored string, not proof of a working decoder.
"""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path


def inspect(path):
    data = path.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if data[:2] != b'MZ' or data[pe:pe+4] != b'PE\0\0':
        raise ValueError('Expected a PE executable')
    coff, opt = pe + 4, pe + 24
    magic = struct.unpack_from('<H', data, opt)[0]
    base = struct.unpack_from('<Q' if magic == 0x20b else '<I', data, opt + (24 if magic == 0x20b else 28))[0]
    sections = []
    for i in range(struct.unpack_from('<H', data, coff+2)[0]):
        p = opt + struct.unpack_from('<H', data, coff+16)[0] + 40*i
        vs, rva, size, offset = struct.unpack_from('<IIII', data, p+8)
        sections.append((data[p:p+8].rstrip(b'\0').decode('ascii'), rva, size, offset))

    def location(offset):
        for section, rva, size, start in sections:
            if start <= offset < start+size:
                return dict(file_offset=hex(offset), rva=hex(rva+offset-start),
                            static_va=hex(base+rva+offset-start), section=section)
        return dict(file_offset=hex(offset))

    objects = {}
    expressions = [('ascii', rb'(?<![A-Za-z0-9_])TCH_[A-Za-z0-9_]+'),
                   ('utf-16le', rb'(?<![A-Za-z0-9_]\x00)T\x00C\x00H\x00_\x00(?:[A-Za-z0-9_]\x00)+')]
    for encoding, expression in expressions:
        for match in re.finditer(expression, data):
            name = match.group().decode(encoding)
            row = objects.setdefault(name, dict(identifier=name, occurrences=[],
                private_decoder='unrecovered', display='proxy-graphics-if-supported-records',
                sample_verified=False))
            row['occurrences'].append(dict(encoding=encoding, **location(match.start())))
    for name, row in objects.items():
        if name == 'TCH_WALL':
            row.update(private_decoder='schema-5-straight-wall', type='TchWall', sample_verified=True)
        elif name == 'TCH_BLOCK_INSERT':
            row.update(private_decoder='standard-INSERT-prefix', type='TchBlockInsert', sample_verified=False)
        row['occurrence_count'] = len(row['occurrences'])
    # Inventory MSVC x86 type descriptors separately. A vtable slot is a
    # function-pointer candidate; only focused disassembly identifies its role.
    native_types = []
    if magic == 0x10b:
        def address_offset(address):
            for _, rva, size, start in sections:
                if base+rva <= address < base+rva+size:
                    return start+address-base-rva
            return None

        def pointers(value):
            return (m.start() for m in re.finditer(re.escape(struct.pack('<I', value)), data))

        def in_code(address):
            return any(name == '.text' and base+rva <= address < base+rva+size for name, rva, size, _ in sections)

        for match in re.finditer(rb'\.\?AVTDb[A-Za-z0-9_]+@@\x00', data):
            name = match.group()[:-1].decode('ascii')
            desc_location = location(match.start()-8)
            if 'static_va' not in desc_location: continue
            descriptor = int(desc_location['static_va'], 16)
            vtables = []
            for ref in pointers(descriptor):
                col = ref-12
                if col < 0 or col+20 > len(data): continue
                signature, displacement, constructor_displacement, _, hierarchy = struct.unpack_from('<IIIII', data, col)
                if signature != 0 or displacement > 4096 or constructor_displacement > 256 or address_offset(hierarchy) is None: continue
                col_location = location(col)
                if 'static_va' not in col_location: continue
                for pointer in pointers(int(col_location['static_va'], 16)):
                    if location(pointer).get('section') != '.rdata': continue
                    slots = []
                    for i in range(160):
                        p = pointer+4+4*i
                        if p+4 > len(data): break
                        value = struct.unpack_from('<I', data, p)[0]
                        if not in_code(value): break
                        slots.append(dict(slot=i, static_va=hex(value)))
                    if len(slots) > 5:
                        vtables.append(dict(subobject_offset=displacement, **location(pointer+4),
                            slot_count=len(slots), slot_36=slots[36]['static_va'] if len(slots) > 36 else None))
            native_types.append(dict(rtti=name, **location(match.start()), vtable_candidates=vtables))
    return dict(evidence_id='cadreader-tch-'+hashlib.sha256(data).hexdigest()[:16],
                target=str(path), sha256=hashlib.sha256(data).hexdigest(), size=len(data),
                image_base=hex(base), search_boundary='all file bytes; token-boundary case-sensitive literal ASCII and UTF-16LE TCH_[A-Za-z0-9_]+; excludes STITCH/HATCH suffixes',
                object_count=len(objects), objects=[objects[key] for key in sorted(objects)], native_types=native_types,
                limitations=['Stored identifiers can name entities, non-graphical objects, base classes or interfaces.',
                             'No runtime registration or full support claim follows from an identifier.',
                             'Compressed, encrypted, constructed, differently cased or other-encoding identifiers are outside this literal search.'])


def markdown(report):
    lines = ['# CADReader 主程序 TCH_ 标识对象记录', '',
        f"目标：`{report['target']}`", f"SHA-256：`{report['sha256']}`", '',
        f"扫描整个主程序，发现 **{report['object_count']} 个不同的字面标识**。保留大小写；JSON 记录全部出现位置。",
        '这些标识包括实体、非图形对象、基类和接口，不能全部视作可绘制实体。', '',
        '实现、原生读取入口和真实样例验证见 [天正兼容说明](Tch-compatibility.md)。代理支持取决于实际保存的绘制记录。', '',
        '| 标识 | 次数 | 首次文件偏移 | 私有数据支持 | 样例验证 |',
        '|---|---:|---|---|---|']
    for row in report['objects']:
        status = {'unrecovered':'未恢复；可走代理图形', 'schema-5-straight-wall':'TchWall：已验证的直墙布局',
                  'standard-INSERT-prefix':'TchBlockInsert：标准块参照前缀，数据库保留原始私有数据'}[row['private_decoder']]
        lines.append(f"| `{row['identifier']}` | {row['occurrence_count']} | `{row['occurrences'][0]['file_offset']}` | {status} | {'原图' if row['sample_verified'] else '待真实样例'} |")
    lines += ['', '通用代理图形路径只接受已实现的绘制记录；空代理、未知绘制命令和未知私有布局会明确报告，不能等同于全部天正原生对象支持。',
              '', '重复扫描：', '', '```powershell',
              "python tools/inspect_tch_objects.py 'C:\\Program Files (x86)\\CADReader\\CADReader.exe' docs/evidence/cadreader-tch-objects.json --markdown docs/CADReader-tch-objects.md", '```', '']
    return '\n'.join(lines)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('target', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--markdown', type=Path)
    args = parser.parse_args()
    report = inspect(args.target)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    if args.markdown:
        args.markdown.write_text(markdown(report), encoding='utf-8')
    print(json.dumps(dict(object_count=report['object_count'], occurrences=sum(r['occurrence_count'] for r in report['objects']), sha256=report['sha256'])))
