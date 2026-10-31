"""Generate runtime-ready MathHelper data from the owned Minecraft source."""
import argparse,hashlib,struct,subprocess,tempfile,zlib
from pathlib import Path
def run(source,output):
 with tempfile.TemporaryDirectory(prefix='mc10-math-') as tmp:
  tmp=Path(tmp);payload=tmp/'sine.bin'
  subprocess.run(['javac','-d',str(tmp),str(source),str(Path(__file__).with_name('MathTableExport.java'))],check=True,capture_output=True)
  subprocess.run(['java','-cp',str(tmp),'MathTableExport',str(payload)],check=True,capture_output=True)
  data=payload.read_bytes();assert len(data)==65536*4
  output.write_bytes(b'MCMT'+struct.pack('<III',1,65536,zlib.crc32(data))+data)
  print('Original MathHelper sine table: PASS',len(data),'bytes; source SHA256',hashlib.sha256(source.read_bytes()).hexdigest())
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--source',required=True,type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args();run(a.source,a.output)
