"""Build a Nintendo Switch NRO using an installed devkitPro (no package changes)."""
import os
from pathlib import Path
import subprocess

root=Path(__file__).resolve().parent
kit=Path(os.environ.get('PLUME_DEVKITPRO', 'C:/devkitPro' if os.name=='nt' else '/opt/devkitpro'))
exe='.exe' if os.name=='nt' else ''
gcc=kit/'devkitA64/bin'/f'aarch64-none-elf-gcc{exe}'
tools=kit/'tools/bin'
build=root/'build'
build.mkdir(exist_ok=True)
env=os.environ.copy()
env['DEVKITPRO']=kit.as_posix()
env['PATH']=str(gcc.parent)+os.pathsep+str(tools)+os.pathsep+env.get('PATH','')

def run(*args):
    print(' '.join(map(str,args)),flush=True)
    subprocess.run(list(map(str,args)),cwd=root,env=env,check=True)

with (build/'fonts.h').open('w') as header:
    for name in ['Manrope','Outfit']:
        data=(root/'assets'/f'{name}.ttf').read_bytes()
        header.write(f'static const unsigned char font_{name.lower()}[]={{\n')
        for i in range(0,len(data),24):
            header.write(','.join(str(v) for v in data[i:i+24])+',\n')
        header.write('};\n')

flags=['-std=gnu11','-O2','-g','-Wall','-Wextra','-Wformat=2','-Werror=implicit-function-declaration',
       '-Werror=incompatible-pointer-types','-ffunction-sections','-fdata-sections',
       '-march=armv8-a+crc+crypto','-mtune=cortex-a57','-mtp=soft','-fPIE','-D__SWITCH__',
       '-Iinclude','-Ithird_party','-Ibuild',f'-I{kit.as_posix()}/libnx/include']
objects=[]
for source in sorted((root/'source').glob('*.c')):
    obj=build/(source.stem+'.o')
    objects.append(obj)
    run(gcc,*flags,'-c',source,'-o',obj)
target=build/'plume-usb'
run(gcc,f'-specs={kit.as_posix()}/libnx/switch.specs',
    '-march=armv8-a+crc+crypto','-mtune=cortex-a57','-mtp=soft','-fPIE',
    f'-Wl,-Map,{target.as_posix()}.map',*objects,f'-L{kit.as_posix()}/libnx/lib','-lnx','-lm','-o',str(target)+'.elf')
run(tools/f'nacptool{exe}','--create','Plume USB','msama','0.1.7',str(target)+'.nacp')
run(tools/f'elf2nro{exe}',str(target)+'.elf',str(target)+'.nro',f'--nacp={target.as_posix()}.nacp',f'--icon={root.as_posix()}/assets/icon.jpg')
print('Built:',str(target)+'.nro')
