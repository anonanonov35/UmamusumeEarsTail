"""Verify reviewed source files and inspect DLL bytes without loading a DLL."""
import argparse,hashlib,json,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def sha(data):return hashlib.sha256(data).hexdigest()

def pe_info(path):
    data=path.read_bytes();assert data[:2]==b'MZ'
    at=struct.unpack_from('<I',data,60)[0];assert data[at:at+4]==b'PE\0\0'
    machine,count,timestamp=struct.unpack_from('<HHI',data,at+4)
    optional_size=struct.unpack_from('<H',data,at+20)[0];optional=at+24
    assert machine==0x8664 and struct.unpack_from('<H',data,optional)[0]==0x20b
    entry=struct.unpack_from('<I',data,optional+16)[0]
    table=optional+optional_size;sections={}
    for i in range(count):
        row=table+i*40;name=data[row:row+8].split(b'\0')[0].decode('ascii')
        virtual_size,rva,size,offset=struct.unpack_from('<4I',data,row+8)
        assert offset+size<=len(data)
        sections[name]={'rva':rva,'virtual_size':virtual_size,'raw_size':size,'sha256':sha(data[offset:offset+size])}
    return {'machine':machine,'timestamp':timestamp,'entry_point':entry,'sha256':sha(data),'sections':sections}

def main():
    p=argparse.ArgumentParser();p.add_argument('--dll',type=Path);p.add_argument('--rebuilt',type=Path);p.add_argument('--report',type=Path);p.add_argument('--mod-root',type=Path);a=p.parse_args()
    m=json.loads((ROOT/'release-manifest.json').read_text(encoding='utf-8'))
    for name,digest in m['source_files_sha256'].items():
        path=ROOT/name;assert path.is_file() and sha(path.read_bytes())==digest,('Source file mismatch',name)
    report={'source_status':'verified','source_files':len(m['source_files_sha256'])}
    if a.dll:
        actual=sha(a.dll.read_bytes());assert actual==m['distributed_runtime']['sha256'],'DLL does not match the recorded distributed release'
        report['distributed_dll_status']='exact hash match';report['dll_sha256']=actual
    if a.mod_root:
        resources=json.loads((ROOT/'resources-manifest.json').read_text(encoding='utf-8'))
        base=a.mod_root.resolve()
        for item in resources['resources']+resources['runtime_and_loader_files']:
            path=(base/item['path']).resolve()
            assert base in path.parents,('Path outside mod root',item['path'])
            assert path.is_file() and path.stat().st_size==item['bytes'] and sha(path.read_bytes())==item['sha256'],('Mod file mismatch',item['path'])
        report['mod_resources_status']='exact size and hash match'
        report['mod_resources_verified']=len(resources['resources'])
        report['runtime_and_loader_files_verified']=len(resources['runtime_and_loader_files'])
    if a.rebuilt:
        actual=pe_info(a.rebuilt);reference=m['distributed_runtime']['pe']
        match=[name for name,expected in reference['sections'].items() if actual['sections'].get(name)==expected]
        mismatch=[name for name in reference['sections'] if name not in match]
        functional=['.text','.rdata','.data','.pdata','.tls','.reloc']
        report['rebuilt']={'sha256':actual['sha256'],'exact_distributed_hash':actual['sha256']==m['distributed_runtime']['sha256'],'entry_point_matches':actual['entry_point']==reference['entry_point'],'byte_exact_sections':match,'different_sections':mismatch,'functional_sections_byte_exact':all(name in match for name in functional),'linker_timestamp':actual['timestamp'],'reference_linker_timestamp':reference['timestamp']}
    if a.report:a.report.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
