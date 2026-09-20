from pathlib import Path
import csv,json,sys,zipfile,shutil
archives=Path(sys.argv[1]);out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True);data=archives/'vit-adaptive-r3-data'
for archive in archives.glob('vit-adaptive-r3-*.zip'):
    tag=archive.stem.removeprefix('vit-adaptive-')
    with zipfile.ZipFile(archive) as z:z.extractall(data/tag)
result=[]
for series in ['compare-h0','compare-h1','compare-repeat','timing-h0','timing-h1']:
    root=data/f'r3-{series}';seq=0 if series.endswith('h0') else 1
    for h in [900,1080]:
        runs=[next(csv.DictReader((root/f'timing-{i}-{h}-s{seq}/summary.csv').open(encoding='utf-8-sig'))) for i in range(4)]
        b=(float(runs[0]['mean_ms'])+float(runs[3]['mean_ms']))/2;c=(float(runs[1]['mean_ms'])+float(runs[2]['mean_ms']))/2
        result.append(dict(series=series,height=h,history=bool(seq),baseline='R2 adaptive' if series.startswith('compare') else 'exact-stream',base_ms=b,candidate_ms=c,saved_ms=b-c,reduction_percent=100*(b-c)/b,baseline_drift_ms=abs(float(runs[0]['mean_ms'])-float(runs[3]['mean_ms'])),runs=runs))
(out/'timing.json').write_text(json.dumps(result,indent=2)+'\n')
root=data/'r3-quality';pairs=list(csv.DictReader((root/'r2-r3-identity.csv').open(encoding='utf-8-sig')))
assert len(pairs)==168 and all(r['identical'].lower()=='true' and r['r2_sha']==r['r3_sha'] for r in pairs)
shutil.copy2(root/'r2-r3-identity.csv',out/'r2-r3-identity.csv')
quality=[]
for seq in range(1,8):
    rows=list(csv.reader((root/f'adaptive-900-s{seq}/gate.csv').open()))
    assert len(rows)==12
    quality.append(dict(sequence=seq,frames=len(rows),reused=sum(int(r[1]) for r in rows),exact_extension=sum(int(r[3])==8 for r in rows),full_frames=[i for i,r in enumerate(rows) if not int(r[1])],reasons=[int(r[3]) for r in rows]))
(out/'quality-identity.json').write_text(json.dumps(dict(pairs_compared=168,all_identical=True,reference_quality='../vit-adaptive-20260920/r2/quality.json',sequences=quality),indent=2)+'\n')
for r in result:print(r['series'],r['height'],f"{r['base_ms']:.6f} -> {r['candidate_ms']:.6f}, saved={r['saved_ms']:.6f}ms ({r['reduction_percent']:.3f}%), drift={r['baseline_drift_ms']:.6f}")
