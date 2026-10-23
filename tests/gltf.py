#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import base64,json,math,pathlib,struct,subprocess,sys,tempfile

def load(path):
    data=path.read_bytes()
    if path.suffix=='.glb':
        magic,version,length=struct.unpack_from('<3I',data)
        assert magic==0x46546c67 and version==2 and length==len(data)
        cursor=12;chunks=[]
        while cursor<len(data):
            size,kind=struct.unpack_from('<2I',data,cursor);cursor+=8
            assert size%4==0 and cursor+size<=len(data)
            chunks.append((kind,data[cursor:cursor+size]));cursor+=size
        assert chunks[0][0]==0x4e4f534a
        root=json.loads(chunks[0][1]);binary=b''
        if 'buffers' in root:
            assert len(chunks)==2 and chunks[1][0]==0x004e4942
            binary=chunks[1][1];assert 'uri' not in root['buffers'][0]
    else:
        root=json.loads(data);binary=b''
        if 'buffers' in root:
            uri=root['buffers'][0]['uri'];assert uri.startswith('data:application/octet-stream;base64,')
            binary=base64.b64decode(uri.split(',',1)[1],validate=True)
    assert root['asset']['version']=='2.0'
    if not root.get('meshes'):
        assert not root.get('buffers') and not root['scenes'][0].get('nodes')
        return root,[],b''
    length=root['buffers'][0]['byteLength'];assert length<=len(binary)<=length+3
    def accessor(index):
        a=root['accessors'][index];v=root['bufferViews'][a['bufferView']]
        width={'SCALAR':1,'VEC3':3,'VEC4':4}[a['type']]
        fmt='<'+('f' if a['componentType']==5126 else 'I')*width
        step=v.get('byteStride',4*width);offset=v.get('byteOffset',0)+a.get('byteOffset',0)
        assert offset%4==0 and v.get('byteOffset',0)+v['byteLength']<=length
        assert a.get('byteOffset',0)+(a['count']-1)*step+width*4<=v['byteLength']
        return [struct.unpack_from(fmt,binary,offset+i*step) for i in range(a['count'])]
    decoded=[]
    for m in root['meshes']:
        p=m['primitives'][0];assert p['mode']==4
        pos=accessor(p['attributes']['POSITION']);normal=accessor(p['attributes']['NORMAL']);color=accessor(p['attributes']['COLOR_0']);indices=accessor(p['indices'])
        assert len(pos)==len(normal)==len(color) and len(indices)%3==0
        assert all(0<=i[0]<len(pos) for i in indices)
        assert all(math.isfinite(x) for row in pos+normal+color for x in row)
        assert all(abs(sum(x*x for x in row)-1)<1e-4 for row in normal)
        assert all(0<=x<=1 for row in color for x in row)
        a=root['accessors'][p['attributes']['POSITION']]
        for axis in range(3):
            assert abs(min(v[axis] for v in pos)-a['min'][axis])<1e-6
            assert abs(max(v[axis] for v in pos)-a['max'][axis])<1e-6
        material=root['materials'][p['material']]
        assert material['doubleSided'] and material['pbrMetallicRoughness']['metallicFactor']==0
        assert (material.get('alphaMode')=='BLEND')==any(c[3]<1 for c in color)
        decoded.append((pos,normal,color,indices))
    return root,decoded,binary[:length]

with tempfile.TemporaryDirectory() as tmp:
    directory=pathlib.Path(tmp)
    subprocess.run([sys.argv[1],str(directory)],check=True)
    a=load(directory/'brushes.gltf');b=load(directory/'brushes.glb')
    assert a[1:]==b[1:] and len(a[1])==9
    root=a[0];assert 'KHR_materials_unlit' in root['extensionsUsed']
    assert len(root['scenes'][0]['nodes'])==3
    for layer in root['scenes'][0]['nodes']:
        node=root['nodes'][layer];assert len(node['children'])==3
        for child in node['children']:
            extra=root['nodes'][child]['extras'];assert extra['layer']==node['extras']['layer'] and extra['group']==42
    assert min(p[2] for mesh in a[1] for p in mesh[0])<-.9 # RH meters, no Unity conversion or viewer transform
    assert {tuple(round(x,3) for x in c[:3]) for c in a[1][0][2]}=={(1,0,0),(0,1,0),(0,0,1)}
    assert any('KHR_materials_unlit' in m.get('extensions',{}) for m in root['materials'])
    load(directory/'empty.gltf');load(directory/'empty.glb')
    subprocess.run([sys.argv[2],'--export-gltf',sys.argv[4],str(directory/'fixture.glb')],check=True)
    assert len(load(directory/'fixture.glb')[1])==1
    subprocess.run([sys.argv[3],'--demo','--export-gltf',str(directory/'demo.gltf')],check=True)
    assert len(load(directory/'demo.gltf')[1])==3
print('glTF/GLB: binary layout, indices, bounds, normals, colors, alpha, layers, unlit, empty scenes, atomic failure and both CLIs passed')
