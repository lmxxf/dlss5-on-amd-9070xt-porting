from pathlib import Path
root=Path('release/native-rgb128');out=root/'amd';out.mkdir(parents=True,exist_ok=True)
for path in Path('release/native-front-chain').glob('block*-*.f32'):(out/path.name).write_bytes(path.read_bytes())
(out/'input.rgba32f').write_bytes((root/'input-tiles.rgba32f').read_bytes())
for name in ('ffwd','ffwd-projection','attention'):
 (out/f'block23-{name}.f32').write_bytes(Path(f'release/native-c512/amd/{name}.f32').read_bytes())
