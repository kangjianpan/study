"""Reopen the delivered archive, reconstruct connectivity and render its actual records."""
from pathlib import Path
import collections
import html
import json
import math
import zipfile
from PIL import Image, ImageDraw, ImageFont

BASE=Path(__file__).parent
OUT=BASE.parents[1]/'交付-底板v2'
archive=OUT/'童骑卫-底板v2-原理图修订与PCB网表.epro2'
docs=[]
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    raw=z.read(next(n for n in z.namelist() if n.endswith('.epru'))).decode('utf-8')
for line in raw.splitlines():
    a,b=line.split('||',1);h=json.loads(a);b=json.loads(b[:-1])
    if h['type']=='DOCHEAD':docs.append({'head':b,'records':[]})
    docs[-1]['records'].append({'h':h,'b':b})
byid={d['head']['uuid']:d for d in docs}
assert len(byid)==len(docs)

def attrs(d):
    a=collections.defaultdict(dict)
    for r in d['records']:
        if r['h']['type']=='ATTR':a[r['b'].get('parentId','')][r['b']['key']]=r['b']
    return a
def value(a,parent,key):return a[parent][key]['value']
def meta(d):return next(r['b'] for r in d['records'] if r['h']['type']=='META')

parts={};netpins=collections.defaultdict(list);pages=[];lines_count=0
for page in (d for d in docs if d['head']['docType']=='SCH_PAGE'):
    a=attrs(page);pins=[];parent={};groups=collections.defaultdict(list);segments=[]
    def find(x):
        if x not in parent:parent[x]=x
        if parent[x]!=x:parent[x]=find(parent[x])
        return parent[x]
    def union(x,y):parent[find(x)]=find(y)
    for r in page['records']:
        if r['h']['type']!='LINE':continue
        b=r['b'];p1=(b['startX'],b['startY']);p2=(b['endX'],b['endY'])
        groups[b['lineGroup']].extend([p1,p2]);segments.append((p1,p2));union(p1,p2)
    for r in page['records']:
        if r['h']['type']!='COMPONENT':continue
        cid=r['h']['id'];c=r['b'];ref=value(a,cid,'Designator')
        assert ref not in parts
        dev=byid[value(a,cid,'Device')];pr=meta(dev)['attributes']
        symbol=byid[value(a,cid,'Symbol')];sa=attrs(symbol)
        assert pr['Symbol']==symbol['head']['uuid']
        fp=byid[pr['Footprint']]
        pn={};positions={}
        for pin in (r for r in symbol['records'] if r['h']['type']=='PIN'):
            pb=pin['b'];pid=pin['h']['id'];num=value(sa,pid,'Pin Number')
            rad=math.radians(c['rotation']);x=-pb['x'] if c['isMirror'] else pb['x'];y=pb['y']
            xy=(round(c['x']+x*math.cos(rad)+y*math.sin(rad),5),round(c['y']-x*math.sin(rad)+y*math.cos(rad),5))
            nc=a[cid+'-'+pid].get('NO_CONNECT',{}).get('value')=='yes'
            pins.append((ref,num,xy,nc));find(xy);positions[num]=xy
        pads=[r for r in fp['records'] if r['h']['type']=='PAD']
        assert {r['b']['num'] for r in pads}==set(positions),(ref,'pad numbers')
        parts[ref]={'cid':cid,'props':pr,'nets':pn,'fp':fp,'positions':positions,'page':meta(page)['title']}
    # Connect all pin endpoints and line endpoints that physically lie on each segment.
    for pt in list(parent):
        for p1,p2 in segments:
            if min(p1[0],p2[0])<=pt[0]<=max(p1[0],p2[0]) and min(p1[1],p2[1])<=pt[1]<=max(p1[1],p2[1]):
                if abs((pt[0]-p1[0])*(p2[1]-p1[1])-(pt[1]-p1[1])*(p2[0]-p1[0]))<1e-5:union(pt,p1)
    labels=collections.defaultdict(set)
    for gid,pts in groups.items():
        name=value(a,gid,'NET');labels[find(pts[0])].add(name)
    for ref,num,xy,nc in pins:
        ns=labels[find(xy)]
        assert len(ns)==(0 if nc else 1),(ref,num,nc,ns)
        net=None if nc else next(iter(ns));parts[ref]['nets'][num]=net
        if net:netpins[net].append(ref+'.'+num)
    lines_count+=len(segments)
    pages.append(page)

pcb=next(d for d in docs if d['head']['docType']=='PCB');pa=attrs(pcb)
pc={value(pa,r['h']['id'],'Designator'):r['h']['id'] for r in pcb['records'] if r['h']['type']=='COMPONENT'}
assert set(pc)==set(parts)
padnets={}
for r in pcb['records']:
    if r['h']['type']=='PAD_NET':
        tag,cid,num,padid=json.loads(r['h']['id']);padnets[cid,num,padid]=r['b']['padNet'] or None
for ref,p in parts.items():
    assert pc[ref]==p['cid']
    assert value(pa,p['cid'],'Footprint')==p['props']['Footprint']
    for r in p['fp']['records']:
        if r['h']['type']=='PAD':assert padnets[p['cid'],r['b']['num'],r['h']['id']]==p['nets'][r['b']['num']],(ref,'PCB net mismatch')

# Independently written key interface specification, separate from the builder.
spec={
 'J2A':{1:'GND',2:'GND',3:'CORE_3V3',4:'RESET_N',5:'ESP_TX',6:'ESP_RX',7:'ADC_SEAT_R',8:'ADC_SEAT',9:'SPI_MOSI',10:'SPI_MISO',11:'SPI_SCK',12:'IMU_CS',13:'ADC_PEDAL',14:'NFC_CS',15:'ADC_VBUS',16:'ADC_CURR',17:'ARM_OK',18:None,19:None,20:None},
 'J2B':{1:'MOTOR_EN',2:'LED_HB',3:'BELT',4:'ESTOP',5:'PWM_MCU',6:'UART1_TX',7:'UART1_RX',8:None,9:None,10:'BUZZER',11:'TOF_XSHUT1',12:'HALL_L',13:'HALL_R',14:'I2C_SCL',15:'I2C_SDA',16:'NFC_RST',17:'TOF_XSHUT2',18:'CORE_5V',19:'GND',20:'CORE_3V3'},
 'J3':{1:'ESC_PWM',2:None,3:'GND'},'D11':{1:'VBAT',2:'VIN_F'},
 'U1':{1:'GND',2:'VBAT',3:'BUCK_EN',4:'BUCK_PG',5:'BUCK_FB',6:'BUCK_VCC',7:'BUCK_BOOT',8:'BUCK_SW',9:'GND'},
 'U3':{1:'GND',2:'+5V',3:'+5V_BUCK',4:'GND',5:'GND',6:'VBUS_USB',7:'+5V',8:None},
 'U8':{1:'ARM_CLK',2:'+3V3_EXT',3:None,4:'GND',5:'ARM_OK',6:'CLEAR_OK',7:'+3V3_EXT',8:'+3V3_EXT'},
 'Q3':{1:'COIL_GATE',2:'GND',3:'COIL_LOW'},'Q4':{1:'SENS_GATE',2:'+5V',3:'+5V_SENS'},
 'J18':{1:'+5V',2:'COIL_PLUS',3:'COIL_LOW'},'J6':{1:'+5V_SENS',2:'PEDAL_RAW',3:'GND'},
 'J16':{1:'+5V_SENS',2:'GND',3:'CURR_RAW'},'Q2':{1:'BUZZ_BASE',2:'GND',3:'BUZZ_COL'}}
for ref,want in spec.items():
    for n,net in want.items():assert parts[ref]['nets'][str(n)]==net,(ref,n,'specification')
for ref in ['D5','D6','D7','D8','D13']:
    assert parts[ref]['nets']['1']=='GND' and parts[ref]['nets']['2']=='CORE_3V3'
assert not any(len(v)<2 for v in netpins.values()),[k for k,v in netpins.items() if len(v)<2]
assert 'Q1' not in parts and 'D2' not in parts
assert not any(r['h']['type'] in ['TRACK','VIA','FILL','POUR','COPPER','PRIMITIVE'] for r in pcb['records'])
countpins=sum(len(p['nets']) for p in parts.values())
result={'components':len(parts),'pins':countpins,'pages':len(pages),'nets':len(netpins),
        'wires':lines_count,'PCB_pad_assignments':len(padnets),'key_spec_pins':sum(map(len,spec.values())),
        'checks':'Archive CRC, references, actual pin/wire connectivity, pin NCs, pad coverage, cross-page labels, PCB net agreement, key interface specification, multi-pin nets: PASS',
        'limits':'Custom format/connectivity check only. Not official EasyEDA import/ERC/DRC, electrical simulation or hardware test.'}
(OUT/'结构与连通检查.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')

# Schematic preview is rendered directly from archive records, including all labels.
FONT=Path('C:/Windows/Fonts/msyh.ttc');MONO=Path('C:/Windows/Fonts/consola.ttf')
scale=1.8
font=lambda n:ImageFont.truetype(str(FONT),round(n*scale))
mono=lambda n:ImageFont.truetype(str(MONO),round(n*scale))
previews=[]
for idx,page in enumerate(pages,1):
    im=Image.new('RGB',(round(1850*scale),round(2100*scale)),'#fffef8');draw=ImageDraw.Draw(im)
    def point(x,y):return (round((x+5)*scale),round((y+1880)*scale))
    def txt(x,y,t,size=7,fill='#145570',right=False):
        draw.text(point(x,y),str(t),font=mono(size),fill=fill,anchor='ra' if right else 'la')
    a=attrs(page)
    draw.text((45,35),meta(page)['title'],font=font(23),fill='#173749')
    draw.text((45,100),'7S / 29.4 V max · 引脚连接图 · 同名网络跨页相连 · 未经编辑器 ERC/DRC',font=font(11),fill='#9b521d')
    for r in page['records']:
        if r['h']['type']=='LINE':
            b=r['b'];draw.line([point(b['startX'],b['startY']),point(b['endX'],b['endY'])],fill='#176578',width=2)
        if r['h']['type']=='ATTR' and r['b']['key']=='NET':
            b=r['b'];txt(b['x'],b['y']-7,b['value'],7,right=b.get('align')=='RIGHT_BOTTOM')
    for r in page['records']:
        if r['h']['type']!='COMPONENT':continue
        b=r['b'];cid=r['h']['id'];symbol=byid[value(a,cid,'Symbol')];sa=attrs(symbol)
        box=next(r['b']['BBOX'] for r in symbol['records'] if r['h']['type']=='PART')
        draw.rectangle([point(b['x']+box[0],b['y']+box[1]),point(b['x']+box[2],b['y']+box[3])],outline='#163e55',width=2)
        txt(b['x']-40,b['y']+box[1]-18,value(a,cid,'Designator'),10,fill='#9a4421')
        model=value(a,cid,'Name');txt(b['x']-80,b['y']+box[3]+12,model,6.5,fill='#222222')
        ref=value(a,cid,'Designator')
        for pin in (r for r in symbol['records'] if r['h']['type']=='PIN'):
            pb=pin['b'];pid=pin['h']['id'];x=b['x']+pb['x'];y=b['y']+pb['y'];left=pb['x']<0
            draw.line([point(x,y),point(b['x']+(-40 if left else 40),y)],fill='#163e55',width=2)
            txt(x+8 if left else x-8,y-8,value(sa,pid,'Pin Number'),5.5,right=not left)
            txt(b['x']-37 if left else b['x']+37,y-2,value(sa,pid,'Pin Name'),5.5,fill='#333333',right=not left)
            if parts[ref]['nets'][value(sa,pid,'Pin Number')] is None:
                draw.line([point(x-3,y-3),point(x+3,y+3)],fill='#a94e4e',width=2)
                draw.line([point(x-3,y+3),point(x+3,y-3)],fill='#a94e4e',width=2)
    path=OUT/f'原理图预览-{idx:02d}.png';im.save(path);previews.append(path.name)
nav=' '.join(f'<a href="#p{i}">第 {i} 页</a>' for i in range(1,7))
body=''.join(f'<section id="p{i}"><h2>{html.escape(meta(pages[i-1])["title"])}</h2><img src="{n}"></section>' for i,n in enumerate(previews,1))
(OUT/'原理图预览.html').write_text('<!doctype html><meta charset="utf-8"><title>童骑卫 底板 v2</title><style>body{font-family:Microsoft YaHei;background:#eee;margin:24px}nav{position:sticky;top:0;background:white;padding:16px}a{margin:16px}img{width:100%;background:white}section{margin:30px 0}</style><nav>'+nav+'</nav><p>由交付工程记录生成的引脚连接预览。不是嘉立创编辑器截图。</p>'+body,encoding='utf-8')
print(json.dumps(result,ensure_ascii=False,indent=2))
