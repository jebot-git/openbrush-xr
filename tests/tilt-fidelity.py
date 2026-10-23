#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Independent original-format writer checks exact source sketch/header retention."""
import json,pathlib,struct,subprocess,sys,tempfile,zipfile

def sketch(version):
    # Duplicate and unused brush table entries must retain their indices.
    data=bytearray(struct.pack('<4I',0xc576a5cd,version,0,4)+b'HEAD'+struct.pack('<I',1))
    sm=(1<<2)|(1<<8)|(1<<17);pm=(1<<7) # Flags/scale/seed/layer/pressure/timestamp absent.
    data+=struct.pack('<I5f2I',2,1.5,-.125,.25,.7,.031415927,sm,pm)
    data+=struct.pack('<I',43)+b'WORD'+struct.pack('<I',5)+b'EXTRA'
    data+=struct.pack('<I',2)
    for xyz in [(1234.5678,-432.12345,.000012345),(-.00009876,9876.543,77.7777)]:
        data+=struct.pack('<7fI',*xyz,.125,-.25,.375,.875,0xdeadbeef)
    return bytes(data)

with tempfile.TemporaryDirectory() as tmp:
    root=pathlib.Path(tmp)
    metadata={'SchemaVersion':3,'BrushIndex':['unused','same','same'],'Unknown':{'x':[1,2,3]},
      'SceneTransformInRoomSpace':[[10,20,-30],[0,.70710677,0,.70710677],.125],
      'ModelIndex':[{'FilePath':'Media Library/Models/external.glb','AssetId':'original-asset','Transforms':[[[1,2,3],[0,0,0,1],1]]}],
      'ImageIndex':[{'FileName':'external.png','FilePath':'references/external.png','Transforms':[[[4,5,6],[0,0,0,1],2]],'LayerIds':[0]}],
      'Videos':[{'FilePath':'external.mp4','Volume':.75,'Pinned':True,'LayerId':0}],
      'Layers':[{'Name':'Original layer α','Visible':True,'Transform':[[0,0,0],[0,0,0,1],0]}]}
    header=struct.pack('<IHH2I',0x546c6974,20,1,0x11223344,0x55667788)+b'PLUS'
    files={'metadata.json':json.dumps(metadata).encode(),'thumbnail.png':b'opaque-original-thumbnail',
      'nested/unknown.dat':b'unknown attachment','main.json':b'{"unrelated":true}'}
    for version in (5,6):
        files['data.sketch']=sketch(version)
        source=root/f'original-{version}.tilt';source.write_bytes(header)
        with zipfile.ZipFile(source,'a',compression=zipfile.ZIP_DEFLATED) as z:
            z.writestr('nested/',b'') # Harmless explicit ZIP directory entry.
            for name,data in files.items():z.writestr(name,data)
        output=root/f'export-{version}.tilt'
        subprocess.run([sys.argv[1],str(source),str(output)],check=True,stdout=subprocess.DEVNULL)
        assert output.read_bytes()[:20]==header
        with zipfile.ZipFile(output) as z:
            assert z.read('data.sketch')==files['data.sketch']
            assert json.loads(z.read('metadata.json'))==metadata
            for name,data in files.items():
                if name!='metadata.json':assert z.read(name)==data
        # Further load/save cycles cannot accumulate coordinate conversion drift.
        for _ in range(3):
            subprocess.run([sys.argv[1],str(output),str(output)],check=True,stdout=subprocess.DEVNULL)
        with zipfile.ZipFile(output) as z:assert z.read('data.sketch')==files['data.sketch']
    directory=root/'loose.tilt';directory.mkdir()
    for name,data in files.items():
        path=directory/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
    subprocess.run([sys.argv[1],str(directory),str(directory)],check=True,stdout=subprocess.DEVNULL)
    assert (directory/'data.sketch').read_bytes()==files['data.sketch']
    assert (directory/'nested/unknown.dat').read_bytes()==files['nested/unknown.dat']
    assert json.loads((directory/'metadata.json').read_bytes())==metadata
    assert not list(root.glob('loose.tilt.*'))
print('Original .tilt fidelity: exact v5/v6 sketch bytes, absent seeds, extension masks, duplicate/unused brush indices, HDR colors, headers, nested assets and atomic loose-directory saves passed')
