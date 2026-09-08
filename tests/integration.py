#!/usr/bin/env python3
"""CLI contracts and committed replay regression checks; never updates goldens."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--bin', type=Path, required=True)
parser.add_argument('--all-goldens', action='store_true')
args = parser.parse_args()
exe = '.exe' if (args.bin/'ashfall_headless.exe').exists() else ''
headless = args.bin/('ashfall_headless'+exe)
replay = args.bin/('ashfall_replay'+exe)
root = Path(__file__).resolve().parents[1]


def run(command, ok=True):
    result = subprocess.run([str(item) for item in command], capture_output=True, text=True)
    if (result.returncode == 0) != ok:
        raise AssertionError(f'{command}\n{result.stdout}\n{result.stderr}')
    return result


for option in (['--ticks', '-1'], ['--threads', '0'], ['--size', '4'],
               ['--seed', 'abc'], ['--unknown', '1'], ['--out']):
    run([headless, *option], ok=False)
with tempfile.TemporaryDirectory() as folder:
    folder = Path(folder)
    config = folder/'bad.toml'
    config.write_text('[forest]\nbase_spred=0.5\n')
    run([headless, '--config', config], ok=False)
    valid=folder/'valid.toml'
    valid.write_text('[world]\nsize=64\nforest_warmup=0\n[simulation]\nenabled=1\n')
    metrics=folder/'metrics.csv'
    run([headless,'--forest-only','--config',valid,'--ticks','1','--metrics',metrics])
    assert 'population' not in metrics.read_text().splitlines()[0]
    run([headless,'--empty','--ticks','1','--metrics',metrics])
    assert len(metrics.read_text().splitlines()[0].split(','))==len(metrics.read_text().splitlines()[1].split(','))
    run([headless,'--ticks','0','--events',metrics,'--kingdom-events',metrics],ok=False)
    output = folder/'run.replay' 
    run([headless, '--size', '64', '--warmup', '10', '--ticks', '80',
         '--seed', '42', '--paranoid', '--out', output])
    for threads in (1, 2, 4, 8):
        run([replay, output, '--verify', '--threads', threads])
    text = output.read_text()
    output.write_text(text.rsplit('\n', 2)[0]+'\n')
    run([replay, output], ok=False)
    lines = text.splitlines()
    lines[-1] = '80 0'
    output.write_text('\n'.join(lines)+'\n')
    result = run([replay, output], ok=False)
    assert 'mismatch at tick 80' in result.stderr
    output.write_text(text+'garbage\n')
    run([replay, output], ok=False)

files = sorted((root/'tests/golden').glob('*.replay'))
assert len(files) == 10, 'expected ten committed golden replays'
for file in files if args.all_goldens else files[:2]:
    run([replay, file, '--threads', '1'])
print('CLI validation, corrupt replay rejection, and golden replays passed')
