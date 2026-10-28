from pathlib import Path
import csv,json
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
P=Path(__file__).parent;data=json.loads((P/'liveness-summary.json').read_text());fig,axs=plt.subplots(3,1,figsize=(11,8))
for ax,(name,d) in zip(axs,data.items()):
 rows=list(csv.DictReader((P/'work'/(name+'-liveness.csv')).open()));ax.plot([int(r['line']) for r in rows],[int(r['live_in_count']) for r in rows],linewidth=.8)
 for x in d['barriers']:ax.axvline(x,color='red',alpha=.25,linewidth=.6)
 ax.axhline(192,color='orange',linestyle='--',linewidth=.7);ax.set_title(name+' | CFG may-live peak '+str(d['peak_registers'])+' / max physical '+str(d['allocated_max_register']),fontsize=9);ax.set_ylabel('VGPR count');ax.set_xlabel('Disassembly line')
fig.tight_layout();fig.savefig(P/'vgpr-liveness.svg');plt.close(fig)
