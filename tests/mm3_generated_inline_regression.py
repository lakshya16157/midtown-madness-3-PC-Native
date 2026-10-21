"""Controlled /Ob1 versus /Ob2 comparison on actual generated title kernels.

This builds only temporary small test translation units, never the main game.
Actual function bodies and calls are identical except for symbol prefixes.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import tempfile

import mm3_x87_exp_regression as common

ROOT=common.ROOT
ENTRIES=('sub_00259180','sub_00259722','sub_002598CD','sub_00012D29',
         'sub_00012D9A','sub_00259982','sub_001C793A')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    if not shutil.which('cl'):
        raise SystemExit('Run scripts/enter-env.ps1 first.')
    bodies={}
    for path in (ROOT/'src/recomp/gen').glob('recomp_*.c'):
        data=path.read_text()
        for entry in ENTRIES:
            match=re.search(r'void '+entry+r'\(void\)\n\{.*?\n\}',data,re.S)
            if match:
                assert entry not in bodies,entry
                bodies[entry]=match.group()
    assert len(bodies)==len(ENTRIES),bodies.keys()
    source='\n'.join('void '+name+'(void);' for name in ENTRIES)+'\n'+'\n'.join(bodies[n] for n in ENTRIES)
    native=(ROOT/'src/mm3_matrix.c').read_text()
    image=common.Image(str(ROOT/'game_files/default.xbe'))
    constants={int(x,16) for x in re.findall(r'MEM[FD]\((0x[0-9A-Fa-f]+)\)',source)}
    with tempfile.TemporaryDirectory(prefix='mm3-generated-inline-') as temporary:
        folder=Path(temporary)
        (folder/'kernels.h').write_text('\n'.join('void '+prefix+name+'(void);'
            for prefix in ('ob1_','ob2_') for name in (*ENTRIES,'matrix_native')))
        (folder/'constants.inc').write_text('\n'.join(
            f'MEM32(0x{va:X})=0x{int.from_bytes(image.d[image.raw(va):image.raw(va)+4],"little"):X}u;'
            for va in sorted(constants)))
        objects=[];sizes={};assembly={}
        common_flags=['cl','/nologo','/w','/O2','/std:c11','/D_CRT_SECURE_NO_WARNINGS',
            '/DRECOMP_GENERATED_CODE','/I'+str(folder),'/I'+str(ROOT/'src'),
            '/I'+str(ROOT/'src/recomp/gen'),'/I'+str(ROOT/'tools/xboxrecomp/include')]
        for mode in ('ob1','ob2'):
            original=re.sub(r'\b(sub_[0-9A-F]{8})\b',lambda m:mode+'_'+m.group(),source)
            optimized=native.replace('sub_00259982_original',mode+'_sub_00259982').replace(
                'void sub_00259982(void)','void '+mode+'_matrix_native(void)')
            code=folder/(mode+'.c');obj=folder/(mode+'.obj');asm=folder/(mode+'.asm')
            code.write_text('#include "mm3_recomp_types.h"\n'+original+'\n'+optimized.replace('#include "recomp_types.h"',''))
            subprocess.run(common_flags+['/'+mode.capitalize(),'/c','/FAs','/Fa'+str(asm),
                '/Fo:'+str(obj),str(code)],check=True,cwd=folder)
            objects.append(str(obj));sizes[mode]=obj.stat().st_size
            text=asm.read_text()
            assembly[mode]={name:len(re.findall(r'call\s+'+mode+'_'+name+r'\b',text)) for name in ENTRIES}
        executable=folder/'contract.exe'
        subprocess.run(common_flags+['/Ob1',str(ROOT/'tests/mm3_generated_inline_contract.c'),*objects,
            '/Fo:'+str(folder/'contract.obj'),'/Fe:'+str(executable)],check=True,cwd=folder)
        result=subprocess.run([str(executable)],check=True,capture_output=True,text=True)
        print(result.stdout,end='')
        rows=[]
        for line in result.stdout.splitlines():
            if line.startswith('BENCH '):
                _,name,pair,a,b,ratio=line.split()
                rows.append({'kernel':name,'pair':int(pair),'ob1_ns':float(a),'ob2_ns':float(b),'speedup':float(ratio)})
        summary={name:{'ob1_median_ns':statistics.median(r['ob1_ns'] for r in rows if r['kernel']==name),
            'ob2_median_ns':statistics.median(r['ob2_ns'] for r in rows if r['kernel']==name),
            'median_pair_speedup':statistics.median(r['speedup'] for r in rows if r['kernel']==name)}
            for name in sorted({r['kernel'] for r in rows})}
        report={'correctness':'PASS 120000 exact /Ob1 vs /Ob2 state/RAM/MXCSR cases',
            'scope':'Actual extracted generated kernels and current production matrix; small temporary TUs, not whole-game or cross-TU performance',
            'object_bytes':sizes,'remaining_direct_calls_in_assembly':assembly,
            'benchmark':'500000 calls per function/pair, 9 interleaved pairs; shared argument setup included; PC53; QPC wall time',
            'summary':summary,'rows':rows}
        print(json.dumps({k:v for k,v in report.items() if k!='rows'},indent=2))
        if args.output:
            args.output.write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':
    main()
