#!/usr/bin/env python3
"""Generate a self-contained KiCad 9 schematic, BOM and explicit net manifest.
All IC pin numbers are package pins, not logical gate indices.
"""
from pathlib import Path
import uuid,json,csv
P=Path(__file__).resolve().parent
NS=uuid.UUID('94c81d38-d081-f233-c4a7-cb53cd8175f0')
def uid(s): return str(uuid.uuid5(NS,s))
def q(s): return json.dumps(str(s),ensure_ascii=False)
def fmt(v): return f'{v:.4f}'.rstrip('0').rstrip('.') if v else '0'
def eff(size=1,justify='',hide=False): return f'(effects (font (size {size} {size})) '+(f'(justify {justify}) ' if justify else '')+('hide' if hide else '')+')'
parts={}; sheets={}; comps=[]; pin_manifest=[]
ROOT=uid('root'); PROJECT='superpico_interface_v2'
FP20='Package_SO:TSSOP-20_4.4x6.5mm_P0.65mm'
FP16='Package_SO:TSSOP-16_4.4x5mm_P0.65mm'
FP14='Package_SO:TSSOP-14_4.4x5mm_P0.65mm'
def part(name,left,right,footprint='',url='',value=None,width=25.4,pitch=2.54):
    pins=[]
    n=max(len(left),len(right)); h=max(10.16,(n+1)*pitch)
    for side,seq in [('L',left),('R',right)]:
        for i,p in enumerate(seq):
            num,label,typ=p; pins.append(dict(num=str(num),name=label,type=typ,x=(-width/2-5.08 if side=='L' else width/2+5.08),y=h/2-pitch*(i+1),side=side))
    parts[name]=dict(pins=pins,w=width,h=h,fp=footprint,url=url,value=value or name)
part('LVC245',[(i+2,f'A{i+1}','input') for i in range(8)]+[(1,'DIR','input'),(19,'~{OE}','input'),(20,'VCC','power_in'),(10,'GND','power_in')],[(18-i,f'B{i+1}','tri_state') for i in range(8)],FP20,'https://www.ti.com/lit/ds/symlink/sn74lvc245a.pdf','SN74LVC245APWR')
part('LVC138',[(1,'A','input'),(2,'B','input'),(3,'C','input'),(4,'~{G2A}','input'),(5,'~{G2B}','input'),(6,'G1','input'),(16,'VCC','power_in'),(8,'GND','power_in')],[(p,f'~{{Y{i}}}','output') for i,p in enumerate([15,14,13,12,11,10,9,7])],FP16,'https://www.ti.com/lit/ds/symlink/sn74lvc138a.pdf','SN74LVC138APWR')
D=[3,4,7,8,13,14,17,18]; Q=[2,5,6,9,12,15,16,19]
part('LVC273',[(p,f'D{i}','input') for i,p in enumerate(D)]+[(11,'CP','input'),(1,'~{MR}','input'),(20,'VCC','power_in'),(10,'GND','power_in')],[(p,f'Q{i}','output') for i,p in enumerate(Q)],FP20,'https://assets.nexperia.com/documents/data-sheet/74LVC273.pdf','74LVC273PW,118')
for typ,mpn in [('OR','SN74LVC32APWR'),('AND','SN74LVC08APWR'),('XOR','SN74LVC86APWR')]:
    part(typ,[(p,n,'input') for p,n in [(1,'1A'),(2,'1B'),(4,'2A'),(5,'2B'),(9,'3A'),(10,'3B'),(12,'4A'),(13,'4B')]]+[(14,'VCC','power_in'),(7,'GND','power_in')],[(p,f'{i+1}Y','output') for i,p in enumerate([3,6,8,11])],FP14,'https://www.ti.com/lit/ds/symlink/'+mpn.lower().replace('pwr','')+'.pdf',mpn)
part('INV',[(p,f'{i+1}A','input') for i,p in enumerate([1,3,5,9,11,13])]+[(14,'VCC','power_in'),(7,'GND','power_in')],[(p,f'{i+1}Y','output') for i,p in enumerate([2,4,6,8,10,12])],FP14,'https://www.ti.com/lit/ds/symlink/sn74lvc04a.pdf','SN74LVC04APWR')
part('AHCT125',[(p,n,'input') for p,n in [(1,'~{1OE}'),(2,'1A'),(4,'~{2OE}'),(5,'2A'),(10,'~{3OE}'),(9,'3A'),(13,'~{4OE}'),(12,'4A')]]+[(14,'VCC','power_in'),(7,'GND','power_in')],[(p,f'{i+1}Y','tri_state') for i,p in enumerate([3,6,8,11])],FP14,'https://www.ti.com/lit/ds/symlink/sn74ahct125.pdf','SN74AHCT125PWR')
part('OD_INV',[(2,'A','input'),(5,'VCC','power_in'),(3,'GND','power_in'),(1,'NC','no_connect')],[(4,'Y_OD','open_collector')],'Package_TO_SOT_SMD:SOT-23-5','https://www.ti.com/lit/ds/symlink/sn74lvc1g06.pdf','SN74LVC1G06DBVR')
part('SUPERVISOR',[(6,'VDD','power_in'),(5,'SENSE','input'),(3,'~{MR}','input'),(4,'CT','input'),(2,'GND','power_in')],[(1,'~{RESET}','open_collector')],'Package_TO_SOT_SMD:SOT-23-6','https://www.ti.com/lit/ds/symlink/tps3808.pdf','TPS3808G33DBVR')
part('R',[(1,'1','passive')],[(2,'2','passive')],'Resistor_SMD:R_0603_1608Metric',width=5.08)
parts['R']['h']=5.08
for pin in parts['R']['pins']:pin['y']=0
part('C',[(1,'1','passive')],[(2,'2','passive')],'Capacitor_SMD:C_0603_1608Metric',width=5.08)
parts['C']['h']=5.08
for pin in parts['C']['pins']:pin['y']=0
part('JP',[(1,'1','passive')],[(2,'2','passive')],'Jumper:SolderJumper-2_P1.3mm_Open_Pad1.0x1.5mm',width=5.08,value='OPEN - commissioning only')
parts['JP']['h']=5.08
for pin in parts['JP']['pins']:pin['y']=0
# Four isolated resistors in standard convex 4x0603 array. Pairing per CAY16-J4.
part('RN4',[(i+1,f'R{i+1}A','passive') for i in range(4)],[(8-i,f'R{i+1}B','passive') for i in range(4)],'Resistor_SMD:R_Array_Convex_4x0603','https://www.bourns.com/docs/product-datasheets/CATCAY.pdf',width=20.32)

def sheet(name,title):
    sheets[name]={'title':title,'items':[],'used':set(),'uuid':ROOT if name=='root' else uid('page:'+name),'path':'/'+ROOT if name=='root' else '/'+ROOT+'/'+uid('sheet:'+name)}
def text(sh,s,x,y,size=1.27):
    sheets[sh]['items'].append(f'(text {q(s)} (at {x} {y} 0) {eff(size,"left top")} (uuid {q(uid(sh+"text"+s))}))')
def symbol_def(name,qualified=True):
    d=parts[name]; w=d['w'];h=d['h']; libname=('IFV2:'+name) if qualified else name
    props=''.join(f'(property {q(k)} {q(v)} (at 0 0 0) {eff(1,hide=True)})' for k,v in [('Reference','U'),('Value',d['value']),('Footprint',d['fp']),('Datasheet',d['url'])])
    body=f'(symbol {q(name+"_0_1")} (rectangle (start {-w/2} {h/2}) (end {w/2} {-h/2}) (stroke (width 0.254) (type default)) (fill (type background))))'
    if name=='C':
        lines=[(-2.54,0,-0.762,0),(-0.762,-2.54,-0.762,2.54),(0.762,-2.54,0.762,2.54),(0.762,0,2.54,0)]
        body=f'(symbol {q(name+"_0_1")} '+''.join(f'(polyline (pts (xy {a} {b}) (xy {c} {e})) (stroke (width 0.254) (type default)) (fill (type none)))' for a,b,c,e in lines)+')'
    pins=''
    for p in d['pins']:
        pins+=f'(pin {p["type"]} line (at {fmt(p["x"])} {fmt(p["y"])} {0 if p["side"]=="L" else 180}) (length 5.08) (name {q(p["name"])} {eff(0.95)}) (number {q(p["num"])} {eff(0.85)}))'
    return f'(symbol {q(libname)} (pin_names (offset 0.8)) (in_bom yes) (on_board yes) {props} {body} (symbol {q(name+"_1_1")} {pins}))'

def label(sh,net,x,y,side,key):
    angle=0 if side=='L' else 180
    return f'(global_label {q(net)} (shape bidirectional) (at {fmt(x)} {fmt(y)} {angle}) {eff(0.9,"right" if side=="L" else "left")} (uuid {q(uid(key+"label"))}) (property "Intersheetrefs" "${{INTERSHEET_REFS}}" (at {fmt(x)} {fmt(y)} {angle}) {eff(1,hide=True)}))'

def add(sh,name,ref,x,y,nets,value=None):
    x=round(x/1.27)*1.27; y=round(y/1.27)*1.27
    d=parts[name];s=sheets[sh];s['used'].add(name);v=value or d['value']; nets={str(k):v for k,v in nets.items()}
    assert set(nets)=={p['num'] for p in d['pins']},(ref,set(nets),d['pins'])
    obj=f'(symbol (lib_id "IFV2:{name}") (at {x} {y} 0) (unit 1) (in_bom yes) (on_board yes) (dnp no) (uuid {q(uid(ref))})'
    for k,val,yy in [('Reference',ref,y-d['h']/2-5),('Value',v,y-d['h']/2-2.5),('Footprint',d['fp'],y),('Datasheet',d['url'],y)]:
        obj+=f'(property {q(k)} {q(val)} (at {x} {fmt(yy)} 0) {eff(1.1 if k=="Reference" else 0.9,hide=k in ["Footprint","Datasheet"])})'
    for p in d['pins']:obj+=f'(pin {q(p["num"])} (uuid {q(uid(ref+":"+p["num"]))}))'
    obj+=f'(instances (project {q(PROJECT)} (path {q(s["path"])} (reference {q(ref)}) (unit 1)))))';s['items'].append(obj)
    for p in d['pins']:
        xx=x+p['x']; yy=y-p['y'];net=nets[p['num']];key=ref+':'+p['num']
        pin_manifest.append(dict(ref=ref,pin=p['num'],pin_name=p['name'],net=net,kind=name,sheet=sh))
        if net is None:s['items'].append(f'(no_connect (at {fmt(xx)} {fmt(yy)}) (uuid {q(uid(key+"nc"))}))');continue
        ex=xx+(-5.08 if p['side']=='L' else 5.08)
        s['items'].append(f'(wire (pts (xy {fmt(xx)} {fmt(yy)}) (xy {fmt(ex)} {fmt(yy)})) (stroke (width 0) (type default)) (uuid {q(uid(key+"wire"))}))')
        s['items'].append(label(sh,net,ex,yy,p['side'],key))
    comps.append(dict(reference=ref,value=v,footprint=d['fp'],datasheet=d['url'],sheet=sh))

for n,t in [('root','00 | Versorgung, Reset und Projektuebersicht'),('video','01 | RGB und PPU-Timing: 5 V auf 3,3 V'),('bus','02 | Audio und PPU-Schreibbus: 5 V auf 3,3 V'),('decode','03 | Registeradressierung und Schreibstrobes'),('latches','04 | Registerspeicher: INIDISP / BGMODE / M7SEL / SETINI'),('logic','05 | Betriebsart, Blanking und sicherer 5-V-Rueckkanal'),('host','06 | RP2350B-Schnittstelle und Erweiterung'),('pulls','07 | Eingangs-Bias und Ausgangsterminierung')]:sheet(n,t)

# Buffers: same one-stage translator class on RGB and timing.
banks=[['B4','B3','B2','B1','B0','G4','G3','G2'],['G1','G0','R4','R3','R2','R1','R0','PCLK'],['VBLANK','HBLANK','FIELD','OVER','TOUMEI','RESET_N','DCK','CSYNC'],['SDATA','LRCK','BCLK','PAWR_N','PARD_N','MCLK',None,None],['PA'+str(i) for i in range(8)],['D'+str(i) for i in range(8)]]
for b,sigs in enumerate(banks):
    sh='video' if b<3 else 'bus'; x=[80,210,340][b%3]
    nets={1:'V3V3',19:'GND',20:'V3V3',10:'GND'}
    for i,sig in enumerate(sigs):nets[i+2]='SN_'+sig if sig else 'GND';nets[18-i]='LV_'+sig if sig else None
    add(sh,'LVC245','U'+str(b+1),x,65,nets)
    # Logical harness connector, not a PPU package footprint.
    nm='J_IN_'+str(b+1)
    pins=[(i+1,(sig or 'NC'),'output' if sig else 'no_connect') for i,sig in enumerate(sigs)]+[(9,'GND','passive'),(10,'GND','passive')]
    part(nm,[],pins,'Connector_PinHeader_2.54mm:PinHeader_2x05_P2.54mm_Vertical',value='SNES harness '+str(b+1),width=25.4)
    add(sh,nm,'J'+str(b+1),x,135,{**{i+1:'SN_'+sig if sig else None for i,sig in enumerate(sigs)},9:'GND',10:'GND'})
    add(sh,'C','C'+str(b+1),x,185,{1:'V3V3',2:'GND'},'100n X7R 16V')
text('video','Alle A-Pins: SNES-Seite (bis 5,5 V). Alle B-Pins: 3,3 V. DIR fest HIGH, /OE fest LOW.\nSteckverbinder sind logische Harness-Pinouts, KEINE ungepruefte PPU-Beinzuordnung.\nRGB/PCLK gleichartige Puffer und kurze, laengenangepasste Leitungen verwenden.',25,225)
text('bus','Keine direkte SNES-Verbindung zu RP2350-GPIOs. PA[7:0] und D[7:0] bleiben am SNES reine Messeingaenge.\nPARD/MCLK/CSYNC sind Diagnose-/Erweiterungssignale; nicht als bereits unterstuetzte Modi verstehen.\n100-nF-Abblockung je IC direkt an VCC/GND; siehe Bias-Netzwerke auf Blatt 07.',25,225)

# Decode high group then low bits; only $00, $05, $1A, $33 receive strobes.
base={1:'LV_PA3',2:'LV_PA4',3:'LV_PA5',4:'LV_PA6',5:'LV_PA7',6:'V3V3',16:'V3V3',8:'GND'}
base.update({p:('BANK00_N' if i==0 else 'BANK18_N' if i==3 else 'BANK30_N' if i==6 else None) for i,p in enumerate([15,14,13,12,11,10,9,7])})
add('decode','LVC138','U7',80,65,base)
for ref,x,y,bank,outs in [('U8',210,65,'BANK00_N',{0:'SEL00_N',5:'SEL05_N'}),('U9',340,65,'BANK18_N',{2:'SEL1A_N'}),('U10',80,155,'BANK30_N',{3:'SEL33_N'})]:
    ns={1:'LV_PA0',2:'LV_PA1',3:'LV_PA2',4:bank,5:'GND',6:'V3V3',16:'V3V3',8:'GND'}
    ns.update({p:outs.get(i) for i,p in enumerate([15,14,13,12,11,10,9,7])});add('decode','LVC138',ref,x,y,ns)
def quad(sh,typ,ref,x,y,gates):
    n={14:'V3V3',7:'GND'}
    for pins,signals in zip([(1,2,3),(4,5,6),(9,10,8),(12,13,11)],gates):n.update(dict(zip(pins,signals)))
    add(sh,typ,ref,x,y,n)
quad('decode','OR','U11',225,155,[(f'SEL{r}_N','LV_PAWR_N',f'CLK{r}') for r in ['00','05','1A','33']])
for i in range(7,12):add('decode','C','C'+str(i),40+(i-7)*75,222,{1:'V3V3',2:'GND'},'100n X7R 16V')
text('decode','CLKxx = /SELxx OR /PAWR. Steigende Flanke am Schreibende uebernimmt Daten.\nAdress-Stabilitaet, Decoder-Glitches und Daten-Hold relativ zur verzoegerten CLK-Flanke messen.\nKeine RC-Verzoegerung im Schreibpfad als Ersatz fuer einen Timing-Nachweis verwenden.',25,248)

# Latch all required context, with INIDISP bit 4 repurposed as write-valid.
for i,(reg,x,y,prefix) in enumerate([('00',90,65,'IN'),('05',280,65,'BG'),('1A',90,160,'M7'),('33',280,160,'SI')]):
    ns={20:'V3V3',10:'GND',1:'REG_RESET_N',11:'CLK'+reg}
    ns.update({p:('V3V3' if reg=='00' and j==4 else 'GND' if reg=='00' and j in [5,6] else 'LV_D'+str(j)) for j,p in enumerate(D)})
    used={'IN':{0,1,2,3,4,7},'BG':{0,1,2},'M7':{6,7},'SI':{0,1,2,3,6,7}}[prefix]
    ns.update({p:prefix+str(j) if j in used else None for j,p in enumerate(Q)})
    add('latches','LVC273','U'+str(12+i),x,y,ns)
    add('latches','C','C'+str(12+i),x+65,y,{1:'V3V3',2:'GND'},'100n X7R 16V')
text('latches','U12: D4 ist absichtlich HIGH (Write-valid), nicht INIDISP.D4. Vor dem ersten $2100-Write bleibt FORCE_BLANK aktiv.\nU15 liefert Interlace, Overscan, Pseudo-HiRes und EXTBG. Extra-Bits gehen an Diagnoseanschluss.\nFuer Rastereffekte Registerzustand beim CAPTURE zuordnen, niemals live auf ein aelteres HDMI-Frame anwenden.',25,235)

inv={14:'V3V3',7:'GND'}
for pins,ns in zip([(1,2),(3,4),(5,6),(9,8),(11,10),(13,12)],[('IN4','NOT_VALID'),('M76','NOT_M76'),('LV_VBLANK','TST_ACTIVE'),('GND',None),('GND',None),('GND',None)]):inv.update(dict(zip(pins,ns)))
add('logic','INV','U16',70,65,inv)
quad('logic','AND','U17',205,65,[('BG0','BG1','MODE01'),('MODE01','BG2','MODE7'),('M77','NOT_M76','M7_OVERMODE'),('MODE7','M7_OVERMODE','MODE7_CTX')])
quad('logic','XOR','U18',340,65,[('BG0','BG1','MODE_XOR'),('GND','GND',None),('GND','GND',None),('GND','GND',None)])
quad('logic','AND','U19',70,155,[('BG2','MODE_XOR','HIRES_MODE'),('PGOOD','LV_RESET_N','REG_RESET_N'),('PGOOD','LV_RESET_N','RETURN_ENABLE'),('GND','GND',None)])
quad('logic','OR','U20',205,155,[('IN7','NOT_VALID','FORCE_BLANK'),('HIRES_MODE','SI3','HIRES'),('GND','GND',None),('GND','GND',None)])
for i in range(16,21):add('logic','C','C'+str(i),40+(i-16)*75,222,{1:'V3V3',2:'GND'},'100n X7R 16V')
text('logic','MODE7_CTX = (BGMODE[2:0] == 7) AND M7SEL[7] AND NOT M7SEL[6]. KEINE pauschale Schwarzmaske!\nHIRES = (Mode 5 OR Mode 6 OR SETINI[3]). Interlace/EXTBG bleiben getrennte Informationen.\nTOUMEI und OVER werden separat gemessen. Die bestehende SNES-/OVER-Verbindung bleibt unveraendert.',25,248)

# Host headers with explicit GPIO labels. Core preserves existing capture/audio mapping.
host={27:'LV_VBLANK',28:'LV_PCLK',44:'LV_HBLANK',22:'LV_SDATA',23:'LV_LRCK',24:'LV_BCLK',6:'LV_DCK',7:'LV_RESET_N'}
for b,base in [('B',29),('G',34),('R',39)]:
    for i in range(5):host[base+i]='LV_'+b+str(4-i)
ext={45:'IN0',1:'IN1',2:'IN2',3:'IN3',4:'FORCE_BLANK',5:'MODE7_CTX',8:'LV_OVER',9:'LV_TOUMEI',10:'SI2',11:'SI0',20:'HIRES',21:'SI6',46:'LV_FIELD',47:'LV_MCLK'}
series=[]
for gpio,src in host.items():series.append((src,'GP'+str(gpio)))
for gpio,src in ext.items():series.append((src,'GP'+str(gpio)))
for name,ref,gpios,x,y in [('HOST_VIDEO','J7',list(range(27,45)),85,75),('HOST_AUDIO','J8',[6,7,22,23,24],85,175),('HOST_EXT','J9',list(ext),270,75)]:
    pins=[(i+1,'GP'+str(g),'input') for i,g in enumerate(gpios)]+[(len(gpios)+1,'GND','passive'),(len(gpios)+2,'GND','passive')]
    part(name,pins,[],('Connector_PinHeader_2.54mm:PinHeader_1x07_P2.54mm_Vertical' if name=='HOST_AUDIO' else 'Connector_PinHeader_2.54mm:PinHeader_2x%02d_P2.54mm_Vertical'%((len(pins)+1)//2)),value=name,width=25.4)
    add('host',name,ref,x,y,{**{i+1:'GP'+str(g) for i,g in enumerate(gpios)},len(gpios)+1:'GND',len(gpios)+2:'GND'})
# Seven-pin audio connector footprint is intentionally 1x07, not an eight-pad substitute.
parts['HOST_AUDIO']['fp']='Connector_PinHeader_2.54mm:PinHeader_1x07_P2.54mm_Vertical'
# Extra diagnostic header (no assigned MCU GPIOs).
diag=['LV_CSYNC','LV_PARD_N','LV_MCLK','SI1','SI2','SI7','MODE7','IN4']
part('DIAG',[(i+1,n,'passive') for i,n in enumerate(diag)]+[(9,'GND','passive'),(10,'V3V3','passive')],[],'Connector_PinHeader_2.54mm:PinHeader_2x05_P2.54mm_Vertical',value='3V3 LOGIC ANALYZER',width=25.4)
add('host','DIAG','J10',270,175,{**{i+1:n for i,n in enumerate(diag)},9:'GND',10:'V3V3'})
text('host','3,3-V-Hostanschluesse: NIE an SNES-5-V-Netze anschliessen. GPIO0 bleibt frei (WeAct-PSRAM-CS).\nMENU/BACK GPIO25/26 und HDMI GPIO12-19 bleiben auf dem Host; hier keine neue HDMI-Buchse.\nWeAct: Onboard-Taster GP23 nicht betaetigen/ggf. entfernen (LRCK); GP25-LED/Menu-Konflikt pruefen.\nAktuelle Firmware nutzt nur die Kernsignale; J9 benoetigt neue Firmware und PIO-Planung.',25,240)

# Series arrays and bias arrays, laid out as two compact columns on separate page.
for k in range(0,len(series),4):
    ns={};chunk=series[k:k+4]
    for i in range(4):ns[i+1]=chunk[i][0] if i<len(chunk) else 'GND';ns[8-i]=chunk[i][1] if i<len(chunk) else None
    col=(k//4)%3; row=(k//4)//3
    add('pulls','RN4','RN'+str(1+k//4),65+140*col,48+44*row,ns,'CAY16-330J4LF / 4x33R')
# Input bias split to separate ninth page, avoiding illegible density.
sheet('bias','08 | 5-V-seitige Eingangsbeschaltung')
raw=[s for bank in banks for s in bank if s]
for k in range(0,len(raw),4):
    ns={};chunk=raw[k:k+4]
    for i in range(4):ns[i+1]='SN_'+chunk[i] if i<len(chunk) else 'GND';ns[8-i]='GND'
    col=(k//4)%3;row=(k//4)//3
    add('bias','RN4','RN'+str(20+k//4),65+140*col,48+48*row,ns,'CAY16-104J4LF / 4x100k')
text('pulls','RN1-RN10: 33 Ohm Ausgangs-Serienterminierung, direkt an Puffer-/Logik-Ausgaengen platzieren.\nKeine Spannungsteiler. Werte nach Ueberschwingen/Flanken und Leitungskapazitaet am Prototyp pruefen.\nDie 33-Ohm-Widerstaende ersetzen nicht die aktiven Level-Shifter.',25,220)
text('bias','RN20-RN31: 100-kOhm-Pulldowns auf der SNES-Seite, ca. 50 uA pro HIGH-Signal bei 5 V.\nKeine Pull-ups vom Host in eine ausgeschaltete SNES. Unbenutzte Puffereingaenge sind direkt GND.\nAlle Signalverbinder nur im ausgeschalteten Zustand stecken; keine ESD-Hotplug-Schnittstelle.',25,250)

# Root: power source connectors and hierarchy. Separate 5V return-driver supply.
part('POWER',[(1,'3V3_HOST','power_out'),(2,'GND','power_out'),(3,'5V_SNES','power_out'),(4,'GND','passive')],[],'Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical',value='POWER - one 3V3 source',width=25.4)
add('root','POWER','J11',65,62,{1:'V3V3',2:'GND',3:'V5_SNES',4:'GND'})
add('root','SUPERVISOR','U21',200,62,{6:'V3V3',5:'V3V3',3:'V3V3',4:'POR_CT',2:'GND',1:'PGOOD'})
add('root','R','R1',305,45,{1:'V3V3',2:'PGOOD'},'10k')
add('root','C','C21',305,65,{1:'POR_CT',2:'GND'},'100p C0G 50V')
add('root','C','C22',65,105,{1:'V3V3',2:'GND'},'10u X7R 10V')
add('root','C','C26',330,105,{1:'V5_SNES',2:'GND'},'1u X7R 16V')
add('root','C','C23',200,105,{1:'V3V3',2:'GND'},'100n X7R 16V')
text('root','REV A - ENTWURF ZUR PRUEFUNG / NICHT FERTIGUNGSFREIGEGEBEN\nAdapter fuer ein separates RP2350B-Modul; keine PPU-QFP-Footprints, kein PCB-Layout.\n3V3_HOST muss dieselbe Versorgung wie RP2350-IOVDD sein. Keine zweite 3,3-V-Quelle verbinden.\n5V_SNES wird nur fuer den optionalen Rueckkanal benutzt; keine Verbindung zu 3V3_HOST.\nPGOOD-Verzoegerung: fruehe Registerwrites koennen fehlen. Beide Systeme zusammen starten;\nStartup-Sequenz/Resetdauer vor Freigabe messen. Erstes gueltiges $2100-Write hebt Bildsperre auf.',25,128,1.27)

# Optional SNES return path: separate child.
sheet('return','09 | Optionaler TST15-Rueckkanal - standardmaessig offen')
add('return','AHCT125','U22',90,70,{1:'RETURN_OE_N',2:'TST_ACTIVE',3:'TST_EN_5V',4:'V5_SNES',5:'GND',6:None,10:'V5_SNES',9:'GND',8:None,13:'V5_SNES',12:'GND',11:None,14:'V5_SNES',7:'GND'})
add('return','OD_INV','U23',260,65,{2:'RETURN_ENABLE',5:'V3V3',3:'GND',1:None,4:'RETURN_OE_N'})
add('return','R','R2',260,105,{1:'V5_SNES',2:'RETURN_OE_N'},'10k')
add('return','R','R3',90,135,{1:'TST_EN_5V',2:'TST_LIMIT'},'1k')
add('return','JP','JP1',220,135,{1:'TST_LIMIT',2:'SN_TST15'},'OPEN - enable only after validation')
add('return','R','R4',330,135,{1:'SN_TST15',2:'GND'},'100k')
add('return','R','R5',260,165,{1:'TST_ACTIVE',2:'GND'},'100k')
part('TSTPORT',[(1,'PPU2_TST15','passive'),(2,'GND','passive')],[],'Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical',value='TST15 CONTROL',width=25.4)
add('return','TSTPORT','J12',90,195,{1:'SN_TST15',2:'GND'})
add('return','C','C24',220,205,{1:'V5_SNES',2:'GND'},'100n X7R 16V')
add('return','C','C25',330,205,{1:'V3V3',2:'GND'},'100n X7R 16V')
text('return','JP1 OFFEN ausliefern. TST15-Pegel und Aktivierung fuer die konkrete PPU-Revision pruefen.\nU22: 5-V-Versorgung, TTL-kompatibler 3,3-V-Eingang. U23: Open-Drain mit 5-V-tolerantem Ausgang.\nBei Host-Ausfall wird U22 ueber R2 gesperrt. Keine direkte 5-V-OE-Verbindung zu einem RP2350-Pin.\nDieser Rueckkanal ist KEIN Mode-7-Patch. /OVER bleibt am SNES unangetastet.',25,243)

# Root navigation sheets: compact list at bottom-left; real hierarchy and global labels.
for i,n in enumerate(k for k in sheets if k!='root'):
    x=20+(i%3)*125; y=180+(i//3)*24
    sheets['root']['items'].append(f'(sheet (at {x} {y}) (size 112 17) (stroke (width 0.254) (type default)) (fill (color 0 0 0 0)) (uuid {q(uid("sheet:"+n))}) (property "Sheetname" {q(sheets[n]["title"])} (at {x} {y-1} 0) {eff(0.95,"left bottom")}) (property "Sheetfile" {q(n+".kicad_sch")} (at {x} {y+18} 0) {eff(0.85,"left top")}) (instances (project {q(PROJECT)} (path {q("/"+ROOT)} (page {q(str(i+2))})))))')

for name,s in sheets.items():
    filename=PROJECT+'.kicad_sch' if name=='root' else name+'.kicad_sch'
    header=f'(kicad_sch (version 20250114) (generator "eeschema") (uuid {q(s["uuid"])}) (paper "A3") (title_block (title {q("SuperPico Interface V2 / "+name.upper())}) (date "2026-11-12") (rev "A - REVIEW") (company "scenz-dev / SuperPico Digital") (comment 1 "Prototype schematic; board revision and timing validation required"))'
    lib='(lib_symbols '+''.join(symbol_def(k) for k in sorted(s['used']))+')'
    tail='(sheet_instances (path "/" (page "1")))' if name=='root' else ''
    (P/filename).write_text(header+lib+''.join(s['items'])+tail+')\n')
(P/'InterfaceV2.kicad_sym').write_text('(kicad_symbol_lib (version 20231120) (generator "kicad_symbol_editor") '+''.join(symbol_def(k,False) for k in parts)+')\n')
(P/'sym-lib-table').write_text('(sym_lib_table (version 7) (lib (name "IFV2") (type "KiCad") (uri "${KIPRJMOD}/InterfaceV2.kicad_sym") (options "") (descr "Verified package-pin interface symbols")))\n')
(P/'superpico_interface_v2.kicad_pro').write_text(json.dumps({'meta':{'filename':'superpico_interface_v2.kicad_pro','version':1}},indent=2))
(P/'pin_manifest.json').write_text(json.dumps(pin_manifest,indent=2))
with (P/'BOM.csv').open('w') as f:
    w=csv.DictWriter(f,fieldnames=['reference','value','footprint','datasheet','sheet']);w.writeheader();w.writerows(comps)
with (P/'GPIO.csv').open('w') as f:
    w=csv.writer(f);w.writerow(['GPIO','source_3V3','status']);
    for g,n in sorted({**host,**ext}.items()):w.writerow([g,n,'existing core' if g in host else 'new firmware required'])
print(f'Generated {len(sheets)} sheets, {len(comps)} components, {len(pin_manifest)} pins')
