from pathlib import Path
import json,re,csv
r=Path(__file__).resolve().parent;e=r/'evidence';result=[]
for h in [1152,1088,960]:
 for scenario in ['default','fast0','mp2','hot-mp','missing-contract','missing-projection','missing-bstream','missing-bout']:
  pair=[]
  for role in ['baseline','candidate']:
   d=e/f'compat-{h}-{scenario}-{role}';s=(d/'stdout.log').read_text(encoding='utf-8-sig');err=(d/'stderr.log').read_text(encoding='utf-8-sig');raw=json.loads((d/'raw-hashes.json').read_text(encoding='utf-8-sig'));raw={x['name']:x['sha256'] for x in raw};assert len(raw)==3
   assert 'finite=1 repeat_bitdiff=0' in s and 'UNMAPPED' not in s
   profiles=[dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in s.splitlines() if line.startswith('GAP_PROFILE')];assert len(profiles)==3
   counts=[int(x) for x in re.findall(r'GAP_COUNT frame=\d+ dispatches=(\d+)',s)];assert len(counts)==3
   if scenario in ['fast0','mp2']:assert all(x['compact_contract']=='0' and x['compact_projection']=='0' for x in profiles)
   if scenario=='hot-mp':assert [x['model'] for x in profiles]==['1','2','1'] and raw['frame-0.rgb32f']==raw['frame-2.rgb32f']
   if scenario in ['missing-bstream','missing-bout']:assert all(x['VitEntryExitByteOk']=='0' for x in profiles)
   pair.append(dict(role=role,profiles=profiles,counts=counts,raw=raw,injection_logged=('GAP_GETFUNCTION_INJECT' in err)))
  assert pair[0]['raw']==pair[1]['raw'],(h,scenario,pair[0]['raw'],pair[1]['raw'])
  result.append(dict(height=h,scenario=scenario,arms=pair,bit_equal=True))
for h in [1152,1088,960]:
 d=e/f'final-gold-{h}';rows=list(csv.DictReader((d/'out.csv').open()));assert len(rows)==3 and all(x['checked']=='1' and int(x['invalid'])==0 and int(x['changed_half_values_from_first'])==0 for x in rows)
 raw=json.loads((d/'raw-hashes.json').read_text(encoding='utf-8-sig'));ref=json.loads((e/f'gold-{h}-p0-baseline'/'raw-hashes.json').read_text(encoding='utf-8-sig'))[0]['sha256'];assert all(x['sha256']==ref for x in raw)
(r/'compat-results.json').write_text(json.dumps(dict(cases=result,paired_cases=24,trace_frames=144,final_frame_gold_frames=9,pass_all=True),indent=2));print('COMPAT_PASS paired_cases=24 trace_frames=144 final_frame_gold_frames=9')
