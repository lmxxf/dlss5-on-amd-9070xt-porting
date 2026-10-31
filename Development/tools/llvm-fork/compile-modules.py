#!/usr/bin/env python3
"""Compile the canonical PowerShell recipe with standalone Clang/LLD.

Three stages mirror COMGR SOURCE_TO_BC, BC_TO_RELOCATABLE, LINK_TO_EXECUTABLE.
No SDK, device libraries, fast-math or production source edits are involved.
"""
import argparse
import concurrent.futures
import hashlib
import json
import re
import subprocess
import struct
import time
from pathlib import Path


def recipe(hip):
    text = (hip / 'build-modules.ps1').read_text(encoding='utf-8-sig')
    rows = re.findall(r"@\{ name\s*=\s*'([^']+)';\s*defines\s*=\s*@\((.*?)\);\s*sources\s*=\s*@\((.*?)\)\s*(?:;\s*opts\s*=\s*'([^']*)'\s*)?(?:;\s*compiler\s*=\s*'([^']*)'\s*)?(?:;\s*l23defines\s*=\s*@\((.*?)\)\s*)?\}", text)
    if not rows or len({r[0] for r in rows}) != len(rows):
        raise RuntimeError('Empty or duplicate canonical module recipe')
    for name, defines, parts, opts, compiler, l23defines in rows:
        defs = ['HIP_ISA_HALF 1']
        if name.endswith('-packed'):
            defs.append('HIP_PREPACKED_WEIGHTS 1')
        defs += re.findall(r"'([^']+)'", defines)
        if compiler:  # l23defines only apply to the prebuilt (non-COMGR) rows
            defs += re.findall(r"'([^']+)'", l23defines)
        sources = re.findall(r"'([^']+)'", parts)
        chunks = [''.join(f'#define {d}\n' for d in defs)]
        for part in sources:
            if part == '@swin-persistent-types':
                types = hip.parent / 'Development/HIP/swin_persistent_types.h'
                chunks.append(types.read_text().replace('#pragma once', '') + '\n')
            elif part == '@wave-owned-attention-body':
                core = (hip / 'wave_owned_mh.inc').read_text()
                start = core.index(' // One wave owns all keys')
                end = core.index('#define W2_KERNEL')
                chunks.append(core[start:end] + '\n')
            else:
                chunks.append((hip / part).read_text() + '\n')
        yield name, compiler, ''.join(chunks), defs, sources, [o[len('-mllvm='):] for o in opts.split() if o.startswith('-mllvm=')]


def effective_row_options(target, name, options):
    # Preserve the verified production payload: gfx1200 C512 deep uses max-ilp,
    # while gfx1201 uses COMGR's default schedule. See Round5 compiler audit.
    return [] if target == 'gfx1201' and name == 'c512-m32-deep' else options


def main():
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('--hip', type=Path, default=Path(__file__).resolve().parents[3] / 'hip')
    p.add_argument('--bin', type=Path, default=Path('/home/lmxxf/work/llvm-build-dlss5-gfx12/bin'))
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--targets', nargs='+', default=['gfx1201', 'gfx1200'])
    p.add_argument('--only', nargs='*', default=[])
    p.add_argument('--jobs', type=int, default=4)
    p.add_argument('--generate-only', action='store_true')
    p.add_argument('--target-feature', action='append', default=[], help='e.g. -real-true16 (LLVM23 gfx12 defaults to real true16, which rejects the sources\' v_cvt_f32_f16 vN inline asm); front and back end')
    p.add_argument('--compiler-rows', default='', help="only rows tagged compiler = '<this>' (e.g. llvm23, for build-modules.ps1 -PrebuiltDir)")
    p.add_argument('--front-option', action='append', default=[], help='extra front-end option, e.g. -ffp-contract=on')
    p.add_argument('--row-opts', action='store_true', help='also forward the recipe row opts (-mllvm=X), like build-modules.ps1 -RowOpts')
    p.add_argument('--backend-option', action='append', default=[],
                   help='LLVM backend option, forwarded with -mllvm (repeatable)')
    a = p.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    version = '' if a.generate_only else subprocess.check_output([str(a.bin / 'clang'), '--version'], text=True)
    tasks = []
    for target in a.targets:
        if target not in ('gfx1200', 'gfx1201'):
            p.error('Only gfx1200/gfx1201 supported')
        for name, compiler, source, defs, sources, ropts in recipe(a.hip):
            if a.only and name not in a.only:
                continue
            if a.compiler_rows and compiler != a.compiler_rows:
                continue
            directory = a.out / target / 'sources' / name
            directory.mkdir(parents=True, exist_ok=True)
            # COMGR always calls the translation unit probe.hip.
            src = directory / 'probe.hip'
            src.write_text(source)
            tasks.append((target, name, src, defs, sources, effective_row_options(target, name, ropts) if a.row_opts else []))

    def compile_one(task):
        target, name, src, defs, sources, ropts = task
        dest = a.out / target
        bc, obj, asm, hsaco = [dest / (name + ext) for ext in ('.bc', '.o', '.hsaco.s', '.hsaco')]
        sha = hashlib.sha256(src.read_bytes()).hexdigest()
        # Match Windows COMGR's host auxiliary ABI and C++14 defaults.
        front = [str(a.bin / 'clang'), '--target=x86_64-pc-windows-msvc',
                 '--offload-arch=' + target, '-O3', '-x', 'hip', '--offload-device-only',
                 '-cuid=' + hashlib.sha256(struct.pack('<Q', src.stat().st_size) + src.read_bytes()).hexdigest().upper(), '-c', '-emit-llvm', '-fshort-wchar',
                 '-std=c++14', '-fms-compatibility-version=19.44.35229',
                 '-nogpuinc', '-nogpulib'] + list(a.front_option) + [x for t in a.target_feature for x in ('-Xclang', '-target-feature', '-Xclang', t)] + [str(src), '-o', str(bc)]
        back = [str(a.bin / 'clang'), '-target', 'amdgcn-amd-amdhsa', '-mcpu=' + target,
                '-O3', '-nogpulib'] + [x for t in a.target_feature for x in ('-Xclang', '-target-feature', '-Xclang', t)]
        for option in list(a.backend_option) + ropts:
            back += ['-mllvm', option]
        commands = [front,
                    back + ['-S', str(bc), '-o', str(asm)],
                    back + ['-c', '-mllvm', '-amdgpu-internalize-symbols', str(bc), '-o', str(obj)],
                    [str(a.bin / 'ld.lld'), '--no-undefined', '-shared', '-plugin-opt=mcpu=' + target,
                     str(obj), '-o', str(hsaco)]]
        row = dict(target=target, module=name, source_sha256=sha, defines=defs, sources=sources, commands=commands)
        if not a.generate_only:
            start = time.monotonic()
            with (dest / (name + '.log')).open('w') as log:
                for cmd in commands:
                    log.write(json.dumps(cmd) + '\n'); log.flush()
                    subprocess.run(cmd, stdout=log, stderr=log, check=True)
            row.update(seconds=time.monotonic()-start, sha256=hashlib.sha256(hsaco.read_bytes()).hexdigest())
        print(target, name, row.get('sha256', sha), flush=True)
        return row

    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
        results = list(pool.map(compile_one, tasks))
    (a.out / 'manifest.json').write_text(json.dumps(dict(compiler=version, modules=results), indent=2) + '\n')
    if not a.generate_only:
        (a.out / 'SHA256SUMS').write_text(''.join(f"{r['sha256']}  {r['target']}/{r['module']}.hsaco\n" for r in results))


if __name__ == '__main__':
    main()
