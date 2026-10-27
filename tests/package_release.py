"""Package allowlisted runtime + complete corresponding source; no GUI execution."""
from pathlib import Path
import hashlib, json, re, shutil, subprocess, zipfile

ROOT=Path(__file__).resolve().parents[1]
VERSION='0.2.1'
DIST=ROOT/'dist'
SOURCE=DIST/'corresponding-source'
RUNTIME=DIST/('MoonlightRT81-'+VERSION)
DEPENDENCIES=['mbedtls','opus','moonlight-common-c','pugixml']
FILES=['README.md','README.zh-CN.md','NOTICE.md','LICENSE','.gitignore','CMakeLists.txt','build.ps1','Get-Sources.ps1','Get-BuildTools.ps1','get-toolchain.ps1','Check-Compatibility.ps1','Start-Moonlight.cmd','Install-Moonlight.cmd','Install-Moonlight.ps1','Uninstall-Moonlight.ps1','required-imports.json','pins.json']
DIRS=['src','assets','licenses','clips','docs','tests','validation']

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def allowed(p):
    return not any(x in {'.git','.github','__pycache__','test-private'} for x in p.parts) and p.suffix not in {'.pyc','.pyo','.log','.dpapi'}
def copy(a,b):
    b.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(a,b)
def write(p,obj):
    p.write_text(json.dumps(obj,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
def zip_tree(folder,output,top):
    with zipfile.ZipFile(output,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for p in sorted(folder.rglob('*')):
            if p.is_file() and allowed(p.relative_to(folder)):z.write(p,top+'/'+p.relative_to(folder).as_posix())
    with zipfile.ZipFile(output) as z: assert z.testzip() is None
def audit(data,name):
    # Check both narrow and UTF-16 strings. Upstream author names are legitimate notices.
    text=data.decode('latin1')+'\n'+data.decode('utf-16-le',errors='ignore')
    patterns=[r'(?i)[a-z]:[\\/]Users[\\/]',r'-----BEGIN (?:RSA |EC |OPENSSH |ENCRYPTED )?PRIVATE KEY-----\s+[A-Za-z0-9+/]{64}']
    for pattern in patterns: assert not re.search(pattern,text), 'Privacy audit failed: '+name+' pattern '+pattern

def main():
    DIST.mkdir(exist_ok=True)
    for folder in [SOURCE,RUNTIME]:
        assert not folder.exists() or not any(folder.iterdir()), 'Inspect existing staging before repackaging'
        folder.mkdir(exist_ok=True)
    exe=ROOT/'work/build-armv7/MoonlightRT81.exe'
    audit(exe.read_bytes(),'MoonlightRT81.exe')
    source_hashes={p.relative_to(ROOT).as_posix():sha(p) for d in ['src','assets'] for p in sorted((ROOT/d).rglob('*')) if p.is_file()}
    for name in FILES: copy(ROOT/name,SOURCE/name)
    for directory in DIRS:
        for p in (ROOT/directory).rglob('*'):
            if p.is_file() and allowed(p.relative_to(ROOT)): copy(p,SOURCE/p.relative_to(ROOT))
    for name in DEPENDENCIES:
        for p in (ROOT/'work'/name).rglob('*'):
            rel=p.relative_to(ROOT/'work'/name)
            if not p.is_file() or not allowed(rel):continue
            if name=='mbedtls' and (rel.parts[0] in {'tests','programs','docs'} or rel.parts[:2]==('framework','tests')):continue
            copy(p,SOURCE/'work'/name/rel)
    (SOURCE/'work/opus/package_version').write_text('PACKAGE_VERSION="1.5.2"\n',encoding='ascii')
    inventory={p.relative_to(SOURCE).as_posix():sha(p) for p in sorted((SOURCE/'work').rglob('*')) if p.is_file() and allowed(p.relative_to(SOURCE))}
    assert len(inventory)>500
    write(SOURCE/'dependency-source-inventory.json',inventory)
    manifest={'version':VERSION,'target':'Windows RT 8.1 ARM32','binary_sha256':sha(exe),'dependencies':json.loads((ROOT/'pins.json').read_text()),'source_sha256':source_hashes,'owner_tested_functional_version':'0.2.1','public_build_changes':['Compiler path remapping','GPL notice and license viewer in diagnostics','Clean public documentation and packaging'],'public_rebuild_device_retest':False,'dependency_notes':'Exact source snapshots; unused Mbed TLS tests/programs/docs and Git metadata omitted; Opus package_version supplied for builds without Git.'}
    write(SOURCE/'build-manifest.json',manifest)
    source_zip=DIST/f'Moonlight-WindowsRT81-{VERSION}-source.zip'
    zip_tree(SOURCE,source_zip,'moonlight-rt81')
    for name in ['Start-Moonlight.cmd','Check-Compatibility.ps1','Install-Moonlight.cmd','Install-Moonlight.ps1','Uninstall-Moonlight.ps1','required-imports.json','LICENSE','NOTICE.md','README.md']:copy(ROOT/name,RUNTIME/name)
    copy(exe,RUNTIME/'MoonlightRT81.exe')
    for directory in ['licenses','clips','validation']:
        for p in (ROOT/directory).rglob('*'):
            if p.is_file() and allowed(p.relative_to(ROOT)):copy(p,RUNTIME/p.relative_to(ROOT))
    copy(ROOT/'assets/UPSTREAM.json',RUNTIME/'licenses/Moonlight-icon-provenance.json')
    (RUNTIME/'使用说明.txt').write_text((ROOT/'README.zh-CN.md').read_text(encoding='utf-8').replace('\n','\r\n'),encoding='utf-8-sig',newline='')
    write(RUNTIME/'build-manifest.json',{**manifest,'corresponding_source_archive':source_zip.name,'source_archive_sha256':sha(source_zip)})
    (RUNTIME/'SHA256SUMS.txt').write_text(''.join(sha(p)+'  '+p.relative_to(RUNTIME).as_posix()+'\n' for p in sorted(RUNTIME.rglob('*')) if p.is_file()),encoding='utf-8')
    runtime_zip=DIST/f'Moonlight-WindowsRT81-{VERSION}.zip'
    zip_tree(RUNTIME,runtime_zip,'MoonlightRT81-'+VERSION)
    for archive in [source_zip,runtime_zip]:
        with zipfile.ZipFile(archive) as z:
            for name in z.namelist():
                assert allowed(Path(name))
                if Path(name).suffix in {'.exe','.md','.txt','.json','.cpp','.h','.c','.ps1','.cmd','.py','.rc','.manifest'}:audit(z.read(name),name)
    (DIST/'SHA256SUMS.txt').write_text(''.join(sha(p)+'  '+p.name+'\n' for p in [runtime_zip,source_zip]),encoding='ascii')
    print(json.dumps({'runtime_bytes':runtime_zip.stat().st_size,'source_bytes':source_zip.stat().st_size,'binary_sha256':sha(exe),'runtime_sha256':sha(runtime_zip),'source_sha256':sha(source_zip),'dependency_files':len(inventory)}))

if __name__=='__main__':main()
