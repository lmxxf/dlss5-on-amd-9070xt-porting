from pathlib import Path
import numpy as np,json
p=Path(__file__).parent;pay=p/'payload';pay.mkdir(exist_ok=True);v=np.fromfile(p.parent/'aco-hotspot-phase4-20261029/payload/original1088-valid.rgba32f',dtype='<f4').reshape(1080,1920,4);base=json.loads((p.parent/'backfeed-20261028/final-default.json').read_text())
for name,w,ph,vw,vh in [('small1080',1920,1152,1920,1080),('small900',1600,1024,1600,900),('small720',1344,768,1296,720),('smallwindow',896,512,854,480),('original1088',1920,1088,1920,1080)]:
 a=v[(np.arange(vh)*1080//vh)[:,None],(np.arange(vw)*1920//vw)[None,:]];a=np.pad(a,((0,ph-vh),(0,w-vw),(0,0)),mode='reflect');a.astype('<f4').tofile(pay/(name+'-input.rgba32f'));j=next(j for j in base['jobs']if'final-'+name+'-candidate' in j['label']);(pay/(name+'.flags')).write_text(j['flags'])
