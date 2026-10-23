from pathlib import Path
import json,re,shutil,tarfile,subprocess,hashlib
R=Path(__file__).resolve().parents[3];O=Path(__file__).parent;W=Path('/home/lmxxf/work/dlss5-release-045-build');W.mkdir(exist_ok=True)
if (O/'FINAL-SCRIPTS-FROZEN.json').exists():raise SystemExit('Final packaging scripts are frozen; do not regenerate from the legacy template.')
receipt=json.loads((R/'Development/releases/0.44/receipt.json').read_text());final=json.loads((R/'Development/results/gap-phase6-20261023/final-build/receipt.json').read_text());payload=Path(final['payload'])
s=(R/'Development/releases/0.44/f9-hotfix-20261018/package-044.ps1').read_text(encoding='utf-8-sig').replace("$Version='0.44'","$Version='0.45'").replace('release-044-f9fix','release-045').replace("base='0.43'","base='0.44'").replace('0.43 baseline','0.44 baseline')
for var,file in [('regularSha','dlss5-amd.addon64'),('runtimeSha','LmxxfNrRuntime.dll')]:s=re.sub(r"\$"+var+r"='[a-f0-9]+'",'$'+var+"='"+hashlib.sha256((payload/file).read_bytes()).hexdigest()+"'",s)
line='$baselineHashes=@{'+ ';'.join("'"+x['name'].removesuffix('-0.44')+"'='"+x['sha256']+"'" for x in receipt['packages'])+'}'
s=re.sub(r'\$baselineHashes=@\{[^\n]+',lambda _:line,s)
s=s.replace('  Copy-Item "$lab\\small" "$assets\\small" -Recurse -Force','  # Small weights remain from the fully verified 0.44 base; validate all 93 below.')
s=s.replace('  Copy-Item "$lab\\post70-history-head.f16" "$assets\\post70-history-head.f16" -Force','  # Preserve and verify the unchanged post70 weights from the complete base.')
s=s.replace("payload_validation='Development/results/small-complete-20261018'","payload_validation='Development/results/gap-phase6-20261023/final-build'")
s=s.replace("note='310.9.16 Small default; 1080/900/720/free geometry; Small AE and MODE1 0x3A23; F9 mode changes rebuild buffers and suspend history in 2x/3x; 93 Small weights coexist with Original assets'","note='Final phase6 payload; limited QK three shapes; Up pair candidates STOP; preserves Small default and F9 behavior'")
s=s.replace("  & 'D:\\DLSSNR-Lab\\compile_fit_shaders.exe' $assets\n  if($LASTEXITCODE){throw 'Staged shader compile validation failed'}",'''  $shaderOutput=@(& 'D:\\DLSSNR-Lab\\compile_fit_shaders.exe' $assets 2>&1);$shaderExit=$LASTEXITCODE
  $shaderOutput|Set-Content "$lab\\$name-shaders.log"
  if($shaderExit -or !(($shaderOutput -join "`n") -match 'PASS 44 shader variants compiled with production D3DCompiler')){throw 'Expected 44 successful shader validations'}''')
s=s.replace('# 0.44 reuses verified 10982cf4 payload/80 modules and verified complete 0.43 baselines; no module rebuild.','# 0.45 uses frozen phase6 payload/HIP80 and pinned complete 0.44 F9 final packages. No rebuild.')
# Include refreshed corresponding-source archive in all three packages, not only RE9.
pos='  foreach($lang in \'zh\',\'en\')'
s=s.replace(pos,'''  New-Item -ItemType Directory "$stage\\sources" -Force|Out-Null
  Copy-Item "$lab\\re9-presr-source.tar.gz" "$stage\\sources\\re9-presr-source.tar.gz" -Force
  Copy-Item "$lab\\SOURCE-README.txt" "$stage\\sources\\README.txt" -Force
'''+pos)
s=re.sub(r'^# 0\.44 reuses.*$', '# 0.45 final phase6 frozen payload; pinned complete 0.44 F9-final baselines; no rebuild.',s,flags=re.M)
s=s.replace('source_commit=$SourceCommit;', 'git_base_commit=$SourceCommit;source_dirty=$true;source_archive_sha256=(Get-FileHash "$lab\\re9-presr-source.tar.gz").Hash.ToLower();')
s=s.replace('source_commit=$SourceCommit`r`n', 'git_base_commit=$SourceCommit`r`nsource_dirty=true`r`nsource_archive_sha256=$((Get-FileHash "$lab\\re9-presr-source.tar.gz").Hash.ToLower())`r`n')
s=s.replace('modules_reused=$true','modules_reused=$false').replace("payload_validation='Development/results/f9-hotfix-20261018';hotfix='20261018-F9'", "payload_validation='Development/results/gap-phase6-20261023/final-build'")
s=re.sub(r"bit_exact_scope='[^']*'", "bit_exact_scope='See final phase6 validation; no blanket equivalence across modes or historical releases'",s)
s=re.sub(r";note='[^']*'", ";note='Frozen phase6 payload; limited QK three shapes; Up pair STOP; Small/default/F9 semantics preserved'",s)
(O/'package-045.ps1').write_text(s,encoding='utf-8-sig');shutil.copy2(R/'Development/tools/stage-config-layers.ps1',O/'stage-config-layers.ps1')
shutil.copy2(R/'hip/SHA256SUMS',O/'HIP-SHA256SUMS');shutil.copy2(R/'LICENSE',O/'CORE-LICENSE.txt')
base=Path('/home/lmxxf/work/dlss5-release-044-f9fix-build/re9-presr-source.tar.gz')
with tarfile.open(base)as t:
 candidates=[m for m in t if m.isfile()and m.name=='OptiScaler-DLSSNR-PreSR-Multipass-main/LICENSE'];assert candidates; (O/'HOST-LICENSE.txt').write_bytes(t.extractfile(candidates[0]).read())
(O/'SOURCE-README.txt').write_text('Version 0.45 includes the corresponding source snapshot identified by SOURCE_COMMIT.txt and source-refresh.json. GPL host provenance and build instructions remain in the source archive. MIT core, current HIP recipes and final production sources are refreshed; phase6 rejected Up-pair candidates are not production. Packaging validates HIP80, Small93, 44 shaders per package, runtime smoke and every ZIP file.\n')
for f in O.iterdir():
 if f.is_file():shutil.copy2(f,W/f.name)
print('MATERIALS',W)
