#!/usr/bin/env python3
"""Structural and static-logic checks; not an electrical/timing simulation."""
from pathlib import Path
import json,re,csv,xml.etree.ElementTree as ET
P=Path(__file__).resolve().parent
m=json.loads((P/'pin_manifest.json').read_text()); expected={(p['ref'],p['pin']):p['net'] for p in m}
x=ET.parse(P/'netlist.xml').getroot(); actual={}
for n in x.findall('./nets/net'):
 for p in n.findall('node'):actual[p.attrib['ref'],p.attrib['pin']]=n.attrib['name']
for k,n in expected.items():
 a=actual.get(k)
 assert a==n if n is not None else a is None or a.startswith('unconnected-'),(k,n,a)
assert not set(actual)-set(expected)
for c in x.findall('./components/comp'):
 ref=c.attrib['ref'];fp=c.findtext('footprint');lib,name=fp.split(':')
 f=Path('/usr/share/kicad/footprints')/(lib+'.pretty')/(name+'.kicad_mod')
 pads=set(re.findall(r'\(pad\s+"([^"]+)"',f.read_text()))
 pins={p for r,p in expected if r==ref}
 assert pins==pads,(ref,pins,pads)
for i in range(1,7):
 assert expected['U'+str(i),'20']=='V3V3'
 assert expected['U'+str(i),'1']=='V3V3'
 assert expected['U'+str(i),'19']=='GND'
 for p in range(2,10):
  a=expected['U'+str(i),str(p)];b=expected['U'+str(i),str(20-p)]
  assert (a=='GND' and b is None) or b=='LV_'+a[3:]
for p in m:
 if p['net'] and p['net'].startswith('SN_'):
  assert p['ref'].startswith(('J','RN')) or p['kind']=='LVC245' or p['ref']=='R4',p
# Evaluate actual decoder wiring, not a separately hard-coded address predicate.
def evaluate(a,wr):
 v={'GND':0,'V3V3':1,'LV_PAWR_N':wr,**{'LV_PA'+str(i):(a>>i)&1 for i in range(8)}}
 for ref in ['U7','U8','U9','U10']:
  n=lambda p:expected[ref,str(p)]
  en=not v[n(4)] and not v[n(5)] and v[n(6)]
  sel=v[n(1)]+2*v[n(2)]+4*v[n(3)]
  for i,p in enumerate([15,14,13,12,11,10,9,7]):
   if n(p):v[n(p)]=int(not(en and sel==i))
 for p,q,r in [(1,2,3),(4,5,6),(9,10,8),(12,13,11)]:
  v[expected['U11',str(r)]]=v[expected['U11',str(p)]]|v[expected['U11',str(q)]]
 return v
for a in range(256):
 for wr in [0,1]:
  v=evaluate(a,wr)
  for r in ['00','05','1A','33']:assert v['CLK'+r]==int(wr or a!=int(r,16))
g=list(csv.DictReader((P/'GPIO.csv').open()));gp=[int(r['GPIO']) for r in g]
assert len(gp)==len(set(gp))==37
assert not set(gp)&{0,12,13,14,15,16,17,18,19,25,26}
assert {int(r['GPIO']):r['source_3V3'] for r in g}[45]=='IN0'
print(f'PASS: {len(expected)} pin/net assignments; all 89 footprint pad sets; six voltage-domain buffers; 512 decoder vectors; 37 unique GPIOs.')
print('LIMIT: static connectivity only. Setup/hold, glitches, power sequencing, signal integrity and SNES compatibility require bench validation.')
