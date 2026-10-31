"""Owned 1.0 grass/foliage RGB888 lookup tables and grayscale source tiles."""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import io
import zipfile
from PIL import Image

TILES=(0,38,39,56,52,53,132,133,143)

def convert(resource_root:Path,output:Path):
    if resource_root.is_file():
        with zipfile.ZipFile(resource_root)as jar:sources={n:jar.read(n)for n in('misc/grasscolor.png','misc/foliagecolor.png','terrain.png')}
    else:sources={n:(resource_root/n).read_bytes()for n in('misc/grasscolor.png','misc/foliagecolor.png','terrain.png')}
    grass=Image.open(io.BytesIO(sources['misc/grasscolor.png'])).convert('RGB')
    foliage=Image.open(io.BytesIO(sources['misc/foliagecolor.png'])).convert('RGB')
    terrain=Image.open(io.BytesIO(sources['terrain.png'])).convert('RGBA')
    if grass.size!=(256,256)or foliage.size!=(256,256)or terrain.size!=(256,256):raise ValueError('original 1.0 256x256 atlas expected')
    colors=bytearray()
    for g,f in zip(grass.getdata(),foliage.getdata()):colors.extend(g);colors.extend(f)
    tiles=bytearray()
    for tile in TILES:
        for y in range(16):
            for x in range(16):tiles.extend(terrain.getpixel(((tile&15)*16+x,(tile>>4)*16+y)))
    output.parent.mkdir(parents=True,exist_ok=True)
    data=struct.pack('<8s6I',b'MCBC10\0\0',1,65536,6,32,len(TILES),32+len(colors))+colors+tiles
    output.write_bytes(data)
    audit={'version':1,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'grass_source_sha256':hashlib.sha256(sources['misc/grasscolor.png']).hexdigest(),'foliage_source_sha256':hashlib.sha256(sources['misc/foliagecolor.png']).hexdigest(),'tiles':TILES,'color_record_bytes':6,'tile_record_bytes':1024,'runtime_png_decode':False}
    output.with_suffix('.json').write_text(json.dumps(audit,indent=2)+'\n')
    print(json.dumps(audit,indent=2))
    return audit
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--resources',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);a=ap.parse_args();convert(a.resources,a.output)
if __name__=='__main__':main()

