from pathlib import Path
import json,hashlib,shutil,sys
p=Path(__file__).parent;arm=sys.argv[1];assert arm in ['base','norm','c256','full','minimal'];decision=json.loads((p/'final-five-decision.json').read_text());assert (decision['selected']==arm)if arm!='minimal'else(decision['selected']is None and json.loads((p/'minimal-decision.json').read_text())['qualified'])
cfg=json.loads((p/'final-configuration-verdict.json').read_text());assert cfg['arm']==arm and cfg['all_paired_bit_diff_zero']
roles={'base':'production-combined','norm':'production-combined-norm900','c256':'production-combined-c256','full':'production-combined-norm900-c256','minimal':'production-norm-only'};source=p/roles[arm]/'source/hip';old=p/'r2d-both/source/hip';sha=lambda b:hashlib.sha256(b).hexdigest();changes=[]
for name in ['build-modules.ps1','deep_fast.hip','c512_qkv_attention_compact.inc','wave_owned_c32.inc','wave_owned_mh.inc']:
 src=source/name;before=old/name;target=Path('hip')/name
 if src.read_bytes()==before.read_bytes():continue
 assert target.read_bytes()in [before.read_bytes(),src.read_bytes()],f'Concurrent user edit: {target}'
 changes.append(dict(path=str(target),before_sha256=sha(before.read_bytes()),after_sha256=sha(src.read_bytes())))
ledger=[x for x in json.loads((p/('minimal-artifact-ledger.json'if arm=='minimal'else'canonical-artifact-ledger.json')).read_text())if x['arm']==arm];assert len(ledger)==80
for row in ledger:
 f=p/('final-'+arm)/'HIP'/row['arch']/(row['module']+'.hsaco');assert sha(f.read_bytes())==row['sha256'];assert row['code_addresses_metadata_rodata_data_identical']
for c in changes:shutil.copyfile(source/Path(c['path']).name,c['path'])
out=p/'payload/HIP';shutil.copytree(p/('final-'+arm)/'HIP',out,dirs_exist_ok=True);shutil.copyfile(out/'SHA256SUMS','hip/SHA256SUMS')
result=dict(selected=arm,source_root=str(source),changed_source_files=changes,module_count=80,gfx1201_only_optimization=True,gfx1200_runtime_code_unchanged=True,payload=str(p/'payload'),deployed_to_game=False,committed=False)
(p/'final-source-integration.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
