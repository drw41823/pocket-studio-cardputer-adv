"""Run the nine host suites. Requires Zig or a C++17 compiler."""
import argparse,os,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--zig');args=p.parse_args()
root=Path(__file__).resolve().parent.parent;out=root/'.host-tests';out.mkdir(exist_ok=True)
zig=args.zig or shutil.which('zig');compiler=([zig,'c++'] if zig else [os.environ.get('CXX') or shutil.which('c++') or shutil.which('g++') or 'clang++'])
for source in sorted((root/'tests').glob('*-tests.cpp')):
 target=out/(source.stem+('.exe' if os.name=='nt' else ''))
 extra=[str(root/'src/PlayerMP3.cpp')] if source.stem=='player-tests' else []
 subprocess.run(compiler+['-std=c++17','-O2','-I'+str(root/'include'),str(source)]+extra+['-o',str(target)],cwd=root,check=True)
 subprocess.run([str(target)],cwd=root,check=True)
