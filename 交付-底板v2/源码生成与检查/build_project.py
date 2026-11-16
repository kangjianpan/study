"""Build the v2 schematic and synchronized, unrouted PCB from explicit pin maps."""
import copy
import csv
import hashlib
import io
import json
import math
import runpy
import shutil
import sys
import time
import uuid
import zipfile
from pathlib import Path

sys.stdout.reconfigure(encoding='utf-8')
BASE = Path(__file__).resolve().parent
ROOT = BASE.parents[1]
OUT = ROOT / '交付-底板v2'
OUT.mkdir(exist_ok=True)
NOW = int(time.time() * 1000)
docs = json.loads((ROOT/'tmp/schematic-review/input-fix/parsed-project.json').read_text(encoding='utf-8'))
byid = {d['head']['uuid']: d for d in docs}
old_connect = json.loads((ROOT/'tmp/schematic-review/input-fix/connectivity.json').read_text(encoding='utf-8'))
original_parts = {c['ref']: c for c in old_connect['components']}


def uid(): return uuid.uuid4().hex[:16]
def meta(d): return next(r['b'] for r in d['records'] if r['h']['type']=='META')
def add(d,t,b,i=None):
    r={'h':{'type':t,'id':i or uid(),'ticket':len(d['records'])+1},'b':b}
    d['records'].append(r)
    return r
def newdoc(t,title):
    h={'docType':t,'client':'local-v2','uuid':uid(),'updateTime':NOW,'version':str(NOW),
       'editVersion':'3.2.91'}
    d={'head':h,'records':[{'h':{'type':'DOCHEAD','ticket':1},'b':h}]}
    add(d,'META',{'title':title,'source':''},'META')
    docs.append(d); byid[h['uuid']]=d
    return d


def clone(d,title):
    v=copy.deepcopy(d); ident=uid();v['head'].update(uuid=ident,updateTime=NOW,version=str(NOW))
    next(r for r in v['records'] if r['h']['type']=='DOCHEAD')['b']=v['head']
    meta(v).update(title=title,source='')
    docs.append(v);byid[ident]=v
    return v


def rectangle(d,x1,y1,x2,y2,part):
    add(d,'RECT',{'partId':part,'groupId':'','locked':False,'zIndex':1,
                 'dotX1':x1,'dotY1':y1,'dotX2':x2,'dotY2':y2,'radiusX':0,'radiusY':0,
                 'rotation':0,'strokeColor':None,'strokeStyle':'SOLID','fillColor':None,
                 'strokeWidth':1,'fillStyle':'NONE'})


def attribute(d,parent,key,value,x=None,y=None,visible=False,part=None):
    b={'parentId':parent,'key':key,'value':value,'x':x,'y':y,'rotation':0,
       'keyVisible':False,'valueVisible':visible,'fontSize':6.75,'align':'LEFT_BOTTOM',
       'color':None,'fillColor':None,'fontFamily':None,'zIndex':2}
    if part: b['partId']=part
    return add(d,'ATTR',b)


mil=1000/25.4
pad_template=copy.deepcopy(next(r['b'] for r in byid['eae2b129729e2ee6']['records'] if r['h']['type']=='PAD'))
fp_template=byid['eae2b129729e2ee6']


def footprint(title,pads,body):
    d=clone(fp_template,title)
    d['records']=[r for r in d['records'] if r['h']['type'] in ['DOCHEAD','LAYER','CANVAS','ACTIVE_LAYER','META']]
    meta(d)['description']='Local footprint; dimensions in the v2 design notes. Review before fabrication.'
    for n,x,y,w,h,drill in pads:
        b=copy.deepcopy(pad_template)
        b.update(num=str(n),centerX=x*mil,centerY=y*mil,padAngle=0,padOffsetX=0,padOffsetY=0,
                 layerId=12 if drill else 1,relativeAngle=0,
                 hole={'holeType':'ROUND','width':drill*mil,'height':drill*mil} if drill else None,
                 defaultPad={'padType':'ELLIPSE' if drill else 'RECT','width':w*mil,'height':h*mil,'radius':0})
        add(d,'PAD',b)
    x,y=body
    for layer in [3,48]:
        add(d,'POLY',{'groupId':0,'netName':'','layerId':layer,'width':6 if layer==3 else 2,
          'path':[-x/2*mil,-y/2*mil,'L',x/2*mil,-y/2*mil,x/2*mil,y/2*mil,-x/2*mil,y/2*mil,-x/2*mil,-y/2*mil],
          'locked':False,'zIndex':1,'polyType':'NORMAL'})
    return d


def smd2(title,w,h,padw,padh,pitch):
    return footprint(title,[('1',-pitch/2,0,padw,padh,0),('2',pitch/2,0,padw,padh,0)],(w,h))


FPS={
 '0603':byid[original_parts['R4']['props']['Footprint']],
 'C0603':byid[original_parts['C21']['props']['Footprint']],
 '0805':smd2('C0805_LOCAL',2.0,1.25,1.0,1.5,1.9),
 '1210':smd2('C1210_LOCAL',3.2,2.5,1.4,2.7,3.0),
 'SMA':byid[original_parts['D9']['props']['Footprint']],
 'SMB':smd2('SMB_LOCAL',4.6,3.6,2.2,2.6,4.2),
 'F2410':smd2('FUSE2410_LOCAL',6.1,2.69,2.2,3.2,5.0),
 'L8040':footprint('SRN8040TA_LOCAL',[('1',-2.85,0,2.8,6.6,0),('2',2.85,0,2.8,6.6,0)],(8.0,8.0)),
 'CAP10P5':byid[original_parts['C1']['props']['Footprint']],
 'DDA':byid[original_parts['U1']['props']['Footprint']],
 'SOT23':byid[original_parts['Q2']['props']['Footprint']],
}
# SOT-23-5 and -6 numbering: counterclockwise, pin 1 at lower left in top view.
for n in [5,6]:
    rows=[('1',-0.95,1.0,.65,1.1,0),('2',0,1.0,.65,1.1,0),('3',.95,1.0,.65,1.1,0)]
    rows += ([('4',.95,-1.0,.65,1.1,0),('5',-.95,-1.0,.65,1.1,0)] if n==5 else
             [('4',.95,-1.0,.65,1.1,0),('5',0,-1.0,.65,1.1,0),('6',-.95,-1.0,.65,1.1,0)])
    FPS['SOT'+str(n)]=footprint('SOT23_'+str(n)+'_LOCAL',rows,(2.9,1.6))
FPS['VSSOP8']=footprint('DCU_VSSOP8_LOCAL',[(str(i+1),-1.55,(-.75+i*.5),.85,.3,0) for i in range(4)]+
    [(str(8-i),1.55,(-.75+i*.5),.85,.3,0) for i in range(4)],(2.0,2.3))
FPS['SO8']=footprint('SO8_LOCAL',[(str(i+1),-2.4975,-1.905+i*1.27,1.505,.802,0) for i in range(4)]+
    [(str(8-i),2.4975,-1.905+i*1.27,1.505,.802,0) for i in range(4)],(3.9,4.9))
FPS['DRL8']=footprint('DRL8_SOT583_LOCAL',[(str(i+1),-.74,-.75+i*.5,.67,.3,0) for i in range(4)]+
    [(str(8-i),.74,-.75+i*.5,.67,.3,0) for i in range(4)],(1.2,2.1))

# PCB libraries use Cartesian Y-up. Flip the authored Y-down lead patterns so
# numbering is counterclockwise in top view, matching the original libraries.
for key in ['SOT5','SOT6','SO8','DRL8','VSSOP8']:
    for record in FPS[key]['records']:
        if record['h']['type']=='PAD':record['b']['centerY'] *= -1

# Exact DDA0008B land pattern from the LMR36520 datasheet.
FPS['DDA']=footprint('DDA0008B_LMR36520_LOCAL',
    [(str(i+1),-2.7,1.905-i*1.27,1.55,.6,0) for i in range(4)]+
    [(str(8-i),2.7,1.905-i*1.27,1.55,.6,0) for i in range(4)]+
    [('9',0,0,2.71,3.4,0)],(3.9,4.9))

parts={}
for c in old_connect['components']:
    nets={p['num']:(p['nets'][0] if p['nets'] else ('N_'+hashlib.sha1(p['root'].encode()).hexdigest()[:8] if not p['nc'] else None)) for p in c['pins']}
    parts[c['ref']]={'ref':c['ref'],'model':c['props'].get('Manufacturer Part') or c['model'],
        'code':c['props'].get('Supplier Part',''),'brand':c['props'].get('Manufacturer',''),
        'value':c['props'].get('Value') or c['props'].get('Manufacturer Part') or c['model'],
        'fp':byid[c['props']['Footprint']], 'dev':byid[c['props']['Device']],
        'sym':byid[c['props']['Symbol']], 'pins':{p['num']:p['name'] for p in c['pins']},
        'nets':nets,'notes':'','page':None}


def generic_symbol(model,pins):
    d=newdoc('SYMBOL',model)
    part=model+'.1'
    rows=math.ceil(len(pins)/2)
    height=max(30,(rows+1)*12)
    add(d,'PART',{'BBOX':[-40,-height/2,40,height/2],'title':part},part)
    rectangle(d,-40,-height/2,40,height/2,part)
    attribute(d,'','Symbol',model,part=part)
    attribute(d,'','Designator','U?',part=part)
    for i,(num,name) in enumerate(pins.items()):
        left=i<rows
        row=i if left else i-rows
        x=-60 if left else 60
        y=-height/2+12+row*12
        pin=add(d,'PIN',{'partId':part,'groupId':'','locked':False,'zIndex':3,'display':True,
                        'x':x,'y':y,'length':20,'rotation':0 if left else 180,'color':None,'pinShape':'NONE'})
        pid=pin['h']['id']
        attribute(d,pid,'Pin Number',num,x,y-2,True,part)
        nameattr=attribute(d,pid,'Pin Name',name,-37 if left else 37,y+2,True,part)
        nameattr['b']['align']='LEFT_BOTTOM' if left else 'RIGHT_BOTTOM'
        attribute(d,pid,'Pin Type','Passive',part=part)
    return d


def define(ref,model,code,brand,value,fp,pins,nets,notes='',props=None,template=None):
    sym=generic_symbol(model,pins)
    dev=newdoc('DEVICE',model)
    a={'Manufacturer Part':model,'Supplier Part':code,'Manufacturer':brand,'Value':value,
       'Symbol':sym['head']['uuid'],'Footprint':fp['head']['uuid'],'Supplier':'LCSC',
       'Supplier Footprint':meta(fp)['title'],'Add into BOM':'yes','Convert to PCB':'yes',
       'Name':'={Value}','Designator':ref.rstrip('0123456789')+'?',
       'Datasheet':('https://www.lcsc.com/product-detail/'+code+'.html') if code else '',
       'Description':notes,'Local Library':'yes'}
    if props:a.update(props)
    meta(dev)['attributes']=a
    p={'ref':ref,'model':model,'code':code,'brand':brand,'value':value,'fp':fp,'dev':dev,'sym':sym,
       'pins':pins,'nets':nets,'notes':notes,'page':None}
    parts[ref]=p
    return p


def resistor(ref,value,code,net1,net2,model=None):
    return define(ref,model or 'RES_'+value,code,'UNI-ROYAL',value,FPS['0603'],{'1':'1','2':'2'},
                  {'1':net1,'2':net2},props={'Tolerance':'1%'})


def cap(ref,value,code,net1,net2,voltage='50V',fp='C0603',model=None):
    return define(ref,model or 'CAP_'+value+'_'+voltage,code,'',value+'/'+voltage,FPS[fp],{'1':'1','2':'2'},
                  {'1':net1,'2':net2},props={'Voltage Rating':voltage,'Temperature Coefficient':'X7R'})


def conn(ref,count,nets,title='',terminal=False):
    source=original_parts['J4' if count==2 and terminal else 'J5' if count==3 and terminal else
                          'J3' if count==3 else 'J10' if count==4 else 'J12' if count==6 else 'J13']
    if count not in [2,3,4,6]:
        fp=footprint('HDR_1X'+str(count)+'_P2.54_LOCAL',[(str(i+1),0,i*2.54,1.8,1.8,1.0) for i in range(count)],(2.54,count*2.54))
        code='';model='Header_1x'+str(count)+'_2.54mm'
    else:
        fp=byid[source['props']['Footprint']];code=source['props']['Supplier Part'];model=source['props']['Manufacturer Part']
    return define(ref,model,code,'',title or model,fp,{str(i+1):str(i+1) for i in range(count)},
                  {str(i+1):n for i,n in enumerate(nets)},notes=title)


# 01: seven-series battery input and coordinated, higher-voltage 5 V conversion.
for ref in ['Q1','R1','D10','D2']:
    parts.pop(ref,None)
define('F1','0451001.MRL','C3099','Littelfuse','1A/125V',FPS['F2410'],{'1':'IN','2':'OUT'},
       {'1':'VIN_IN','2':'VIN_F'},'Board branch fuse; battery needs a separate high-breaking-capacity branch fuse.')
define('D1','SMBJ30CA','C83847','Littelfuse','SMBJ30CA bidirectional',FPS['SMB'],{'1':'1','2':'2'},
       {'1':'VIN_F','2':'GND'},'Bidirectional TVS before series reverse diode; 48.4V rated clamp, transient test required.')
define('D11','SS310','C15874','MDD','SS310 100V/3A',FPS['SMA'],{'1':'K','2':'A'},
       {'1':'VBAT','2':'VIN_F'},'Series reverse-polarity blocking; reserve copper for heat and verify hot-plug inrush.')
define('C1','EEUFR1J101','C407952','PANASONIC','100uF/63V',FPS['CAP10P5'],{'1':'+','2':'-'},
       {'1':'VBAT','2':'GND'},'Input damping capacitor, D10 H12.5mm P5mm; replaces earlier 470uF polymer to reduce inrush.',
       {'Voltage Rating':'63V','Height - Seated (Max)':'12.5mm','Pin Spacing':'5mm'})
cap('C2','220nF','C513710','VBAT','GND','100V','0805','CC0805KKX7R0BB224')
define('U1','LMR36520ADDAR','C2879422','TI','LMR36520ADDAR',FPS['DDA'],
       {'1':'PGND','2':'VIN','3':'EN','4':'PG','5':'FB','6':'VCC','7':'BOOT','8':'SW','9':'EP'},
       {'1':'GND','2':'VBAT','3':'BUCK_EN','4':'BUCK_PG','5':'BUCK_FB','6':'BUCK_VCC','7':'BUCK_BOOT','8':'BUCK_SW','9':'GND'},
       '65V input synchronous 2A buck; thermal pad to ground; TI typical 5V network.')
define('L1','SRN8040TA-100M','C2044906','BOURNS','10uH / Isat 5A',FPS['L8040'],{'1':'1','2':'2'},
       {'1':'BUCK_SW','2':'+5V_BUCK'},'10uH +/-20%; IRMS 4.6A and ISAT 5A typical, thermal verification required.')
cap('C3','100nF','C14663','BUCK_BOOT','BUCK_SW','50V',model='0603B104K500NT')
for ref in ['C4','C23']:
    cap(ref,'4.7uF','C17266784','VBAT','GND','100V','1210','CL32B475KCVZW6E')
for ref in ['C5','C6']:
    cap(ref,'22uF','C21397','+5V_BUCK','GND','25V','1210','GRM32ER71E226KE15L')
cap('C24','1uF','C97890','BUCK_VCC','GND','16V',model='GRM188R71C105KA12D')
resistor('R2','100k','C25803','+5V_BUCK','BUCK_FB','0603WAF1003T5E')
resistor('R3','24.9k','C25962','BUCK_FB','GND','0603WAF2492T5E')
resistor('R21','180k','C22827','VBAT','BUCK_EN','0603WAF1803T5E')
resistor('R22','10k','C25804','BUCK_EN','GND','0603WAF1002T5E')
resistor('R31','100k','C25803','BUCK_VCC','BUCK_PG')

# 02: reverse-blocking USB/buck power OR, core-board power isolation and sensor rail.
parts.pop('D9',None)
define('U3','LM66200DRLR','C3235556','TI','LM66200 dual ideal diode',FPS['DRL8'],
 {'1':'GND','2':'OUT','3':'IN1','4':'ON_N','5':'GND','6':'IN2','7':'OUT','8':'ST'},
 {'1':'GND','2':'+5V','3':'+5V_BUCK','4':'GND','5':'GND','6':'VBUS_USB','7':'+5V','8':None},
 'Blocks reverse current to both USB VBUS and buck. This does not isolate the core Micro-USB; use JP1.')
cap('C25','10uF','C14860','+5V','GND','25V','0805','CL31B106KAHNNNE')
# Use a 1206 pad layout for this exact Samsung part.
FPS['1206']=smd2('C1206_LOCAL',3.2,1.6,1.2,1.8,3.0)
parts['C25']['fp']=FPS['1206'];meta(parts['C25']['dev'])['attributes']['Footprint']=FPS['1206']['head']['uuid']
cap('C26','1uF','C97890','VBUS_USB','GND','16V',model='GRM188R71C105KA12D')
conn('JP1',3,['+5V','CORE_5V',None],'CORE POWER: shunt 1-2 for normal operation; REMOVE before core Micro-USB')
define('U4','AP2112M-3.3TRG1','C5290219','DIODES','3.3V sensors / <=200mA',FPS['SO8'],
 {'1':'OUT','2':'NC','3':'NC','4':'NC','5':'EN','6':'GND','7':'GND','8':'IN'},
 {'1':'+3V3_EXT','2':None,'3':None,'4':None,'5':'CORE_3V3','6':'GND','7':'GND','8':'+5V'},
 'External sensor regulator enabled by core 3V3; 200mA board budget with thermal copper.')
resistor('R32','1k','C21190','CORE_3V3','GND')
cap('C27','10uF','C14860','+5V','GND','25V','1206','CL31B106KAHNNNE')
cap('C28','10uF','C14860','+3V3_EXT','GND','25V','1206','CL31B106KAHNNNE')

# Replace old generic rail names and explicitly name previously unnamed nodes.
for p in parts.values():
    for n,v in list(p['nets'].items()):
        if v=='+3V3':p['nets'][n]='CORE_3V3' if p['ref'] in ['J2A','J2B'] else '+3V3_EXT'
# Fixed physical header mapping from the TLS core-board schematic.
core_b={1:'MOTOR_EN',2:'LED_HB',3:'BELT',4:'ESTOP',5:'PWM_MCU',6:'UART1_TX',7:'UART1_RX',
        8:None,9:None,10:'BUZZER',11:'TOF_XSHUT1',12:'HALL_L',13:'HALL_R',14:'I2C_SCL',
        15:'I2C_SDA',16:'NFC_RST',17:'TOF_XSHUT2',18:'CORE_5V',19:'GND',20:'CORE_3V3'}
core_a={1:'GND',2:'GND',3:'CORE_3V3',4:'RESET_N',5:'ESP_TX',6:'ESP_RX',7:'ADC_SEAT_R',8:'ADC_SEAT',
        9:'SPI_MOSI',10:'SPI_MISO',11:'SPI_SCK',12:'IMU_CS',13:'ADC_PEDAL',14:'NFC_CS',
        15:'ADC_VBUS',16:'ADC_CURR',17:'ARM_OK',18:None,19:None,20:None}
parts['J2A']['nets']={str(k):v for k,v in core_a.items()}
parts['J2B']['nets']={str(k):v for k,v in core_b.items()}

# 03: NC stop loop, external watchdog, manual arm latch, PWM gating and relay drive.
conn('J4',2,['ESTOP_FEED','ESTOP_RETURN'],'ESTOP NC AUX: closed=healthy; open/wire-break=stop',terminal=True)
resistor('R33','1k','C21190','+3V3_EXT','ESTOP_FEED')
resistor('R34','1k','C21190','ESTOP_RETURN','ESTOP')
resistor('R5','47k','C25819','ESTOP','GND')
cap('C29','1nF','C100040','ESTOP','GND','50V',model='CC0603KRX7R9BB102')
define('U5','TPS3820-33DBVR','C130064','TI','external watchdog 200ms typ',FPS['SOT5'],
 {'1':'RESET_N','2':'GND','3':'MR_N','4':'WDI','5':'VDD'},
 {'1':'WD_OK','2':'GND','3':'RESET_N','4':'LED_HB','5':'+3V3_EXT'},
 'Independent voltage supervisor and watchdog; reset output clears ARM latch, not MCU; heartbeat must toggle.')
resistor('R35','1k','C21190','LED_HB','GND')
define('U6','SN74LVC1G11DBVR','C22046','TI','MOTOR_EN & ESTOP & WD_OK',FPS['SOT6'],
 {'1':'A','2':'GND','3':'B','4':'Y','5':'VCC','6':'C'},
 {'1':'MOTOR_EN','2':'GND','3':'ESTOP','4':'CLEAR_OK','5':'+3V3_EXT','6':'WD_OK'})
conn('J17',2,['ARM_BTN','GND'],'ARM NO momentary button: press after READY; release then press to rearm',terminal=True)
resistor('R36','10k','C25804','+3V3_EXT','ARM_BTN')
resistor('R37','1k','C21190','ARM_BTN','ARM_FILTER')
cap('C30','100nF','C14663','ARM_FILTER','GND',model='0603B104K500NT')
define('U7','SN74LVC1G14DBVR','C7835','TI','Schmitt ARM debounce',FPS['SOT5'],
 {'1':'NC','2':'A','3':'GND','4':'Y','5':'VCC'},
 {'1':None,'2':'ARM_FILTER','3':'GND','4':'ARM_CLK','5':'+3V3_EXT'})
define('U8','SN74LVC1G74DCUR','C70285','TI','manual ARM latch',FPS['VSSOP8'],
 {'1':'CLK','2':'D','3':'Q_N','4':'GND','5':'Q','6':'CLR_N','7':'PRE_N','8':'VCC'},
 {'1':'ARM_CLK','2':'+3V3_EXT','3':None,'4':'GND','5':'ARM_OK','6':'CLEAR_OK','7':'+3V3_EXT','8':'+3V3_EXT'})
define('U9','SN74LVC1G08DBVR','C7666','TI','ARM_OK & CLEAR_OK',FPS['SOT5'],
 {'1':'A','2':'B','3':'GND','4':'Y','5':'VCC'},
 {'1':'ARM_OK','2':'CLEAR_OK','3':'GND','4':'DRIVE_SAFE','5':'+3V3_EXT'})
define('U10','SN74AHCT1G08DBVR','C113521','TI','5V gated servo PWM',FPS['SOT5'],
 {'1':'A','2':'B','3':'GND','4':'Y','5':'VCC'},
 {'1':'PWM_MCU','2':'DRIVE_SAFE','3':'GND','4':'PWM_GATED','5':'+5V'},
 '5V TTL inputs accept 3.3V MCU. Low output when disabled; select ESC with tested signal-loss cutoff.')
resistor('R39','100k','C25803','PWM_MCU','GND')
resistor('R40','100k','C25803','DRIVE_SAFE','GND')
resistor('R20','220','C22962','PWM_GATED','ESC_PWM')
resistor('R41','10k','C25804','ESC_PWM','GND')
conn('J3',3,['ESC_PWM',None,'GND'],'ESC: 1=signal 2=BEC_NC 3=GND (verify plug orientation)')
define('Q3','AO3400A','C20917','AOS','5V relay coil sink <=200mA',FPS['SOT23'],
 {'1':'G','2':'S','3':'D'},{'1':'COIL_GATE','2':'GND','3':'COIL_LOW'},
 '30V logic MOSFET used ONLY on the regulated 5V coil. Coil <=200mA. No battery voltage on J18.')
resistor('R42','220','C22962','DRIVE_SAFE','COIL_GATE')
resistor('R43','47k','C25819','COIL_GATE','GND')
define('D12','SS110','C2482','MDD','coil flyback 100V',FPS['SMA'],{'1':'K','2':'A'},
 {'1':'COIL_PLUS','2':'COIL_LOW'},'Across coil AFTER physical NC switch. Release time must be measured.')
conn('J18',3,['+5V','COIL_PLUS','COIL_LOW'],
 '5V RELAY ONLY: pin1 -> ESTOP NC power -> pin2 -> coil+; coil- -> pin3',terminal=True)
# 04: five analogue channels with two independent seat inputs and a 5V Hall pedal divider.
parts['J6']['nets']={'1':'+5V','2':'PEDAL_RAW','3':'GND'}
resistor('R44','10k','C25804','PEDAL_RAW','PEDAL_DIV')
resistor('R45','15k','C22809','PEDAL_DIV','GND')
parts['R14']['nets']={'1':'PEDAL_DIV','2':'ADC_PEDAL'}
parts['D7']['nets']['2']='CORE_3V3'
for ref in ['D5','D6','D8']:
    parts[ref]['nets']['2']='CORE_3V3'
# Keep ADC clamp references on the MCU rail, not the independent sensor regulator.
conn('J20',3,['+3V3_EXT','SEAT_R_RAW','GND'],'right FSR: element between 1 and 2',terminal=True)
resistor('R46','10k','C25804','SEAT_R_RAW','GND')
resistor('R47','1k','C21190','SEAT_R_RAW','ADC_SEAT_R')
cap('C31','100nF','C14663','ADC_SEAT_R','GND',model='0603B104K500NT')
orig=parts['D8']
define('D13',orig['model'],orig['code'],orig['brand'],orig['model'],orig['fp'],
       {'1':'A','2':'K','3':'MID'},{'1':'GND','2':'CORE_3V3','3':'ADC_SEAT_R'})
# Increase battery divider headroom for clamped transients, retaining adequate 12-bit resolution.
resistor('R10','180k','C22827','VBAT','VBUS_MID')
resistor('R11','10k','C25804','VBUS_MID','GND')
resistor('R12','1k','C21190','VBUS_MID','ADC_VBUS')
# The current path used unnamed nets in the old export: make both node identities explicit.
parts['J16']['nets']={'1':'+5V','2':'GND','3':'CURR_RAW'}
parts['R28']['nets']={'2':'CURR_RAW','1':'CURR_DIV'}
parts['R29']['nets']={'2':'CURR_DIV','1':'GND'}
parts['R13']['nets']={'1':'CURR_DIV','2':'ADC_CURR'}

# 05: dedicated module connectors, debug isolation and buzzer suppression.
conn('J11',6,['+3V3_EXT','GND','I2C_SCL','I2C_SDA','TOF_XSHUT1',None],'ToF-L: 3.3V logic module with XSHUT')
conn('J21',6,['+3V3_EXT','GND','I2C_SCL','I2C_SDA','TOF_XSHUT2',None],'ToF-R: 3.3V logic module with XSHUT')
conn('J12',6,['+3V3_EXT','GND','SPI_SCK','SPI_MISO','SPI_MOSI','IMU_CS'],'ICM-42688-P SPI / 3.3V')
conn('J13',8,['+3V3_EXT','GND','SPI_SCK','SPI_MISO','SPI_MOSI','NFC_CS','NFC_RST',None],
     'MFRC522 SPI: adapter harness, not direct standard board insertion')
parts['R18']['nets']={'1':'+3V3_EXT','2':'IMU_CS'}
parts['R19']['nets']={'1':'+3V3_EXT','2':'NFC_CS'}
resistor('R48','10k','C25804','NFC_RST','GND')
parts['R9']['nets']={'1':'BUZZ_BASE','2':'BUZZER'}
parts['Q2']['nets']={'1':'BUZZ_BASE','2':'GND','3':'BUZZ_COL'}
resistor('R49','47k','C25819','BUZZ_BASE','GND')
define('D14','SS110','C2482','MDD','buzzer flyback',FPS['SMA'],{'1':'K','2':'A'},
       {'1':'+5V','2':'BUZZ_COL'})
resistor('R50','1k','C21190','UART1_TX','USB_UART_RX')
resistor('R51','1k','C21190','USB_UART_TX','UART1_RX')
parts['U2']['nets']['2']='USB_UART_TX';parts['U2']['nets']['3']='USB_UART_RX'
parts['U2']['nets']['16']='VBUS_USB'
parts['C22']['nets']={'1':'VBUS_USB','2':'GND'}
# USB TX into an unpowered MCU is avoided by switching RX through a partial-power-down buffer.
define('U11','SN74LVC1G125DBVR','C23654','TI','USB->MCU fail-safe buffer',FPS['SOT5'],
 {'1':'OE_N','2':'A','3':'GND','4':'Y','5':'VCC'},
 {'1':'GND','2':'USB_UART_TX','3':'GND','4':'USB_UART_TX_SAFE','5':'CORE_3V3'})
parts['R51']['nets']['1']='USB_UART_TX_SAFE'
# Gate MCU TX to USB when the USB bridge is powered, with Ioff on the MCU side.
define('U12','SN74LVC1G125DBVR','C23654','TI','MCU->USB fail-safe buffer',FPS['SOT5'],
 {'1':'OE_N','2':'A','3':'GND','4':'Y','5':'VCC'},
 {'1':'GND','2':'UART1_TX','3':'GND','4':'USB_UART_RX_SAFE','5':'VBUS_USB'})
parts['R50']['nets']['1']='USB_UART_RX_SAFE'
# Ensure zero floating control inputs on the bridge when USB absent.
resistor('R52','100k','C25803','USB_UART_TX','GND')

# Switch the 5V peripheral rail off when the core has no 3V3 supply.
define('Q4','AO3401A','C15127','AOS','5V sensor rail switch',FPS['SOT23'],
       {'1':'G','2':'S','3':'D'},{'1':'SENS_GATE','2':'+5V','3':'+5V_SENS'},
       '5V only; source and drain deliberately differ from the old high-voltage reverse MOS.')
define('Q5','S8050','C20069125','R+O','sensor rail gate driver',FPS['SOT23'],
       {'1':'B','2':'E','3':'C'},{'1':'SENS_BASE','2':'GND','3':'SENS_GATE'})
resistor('R53','100k','C25803','+5V','SENS_GATE')
resistor('R54','10k','C25804','CORE_3V3','SENS_BASE')
resistor('R55','47k','C25819','SENS_BASE','GND')
resistor('R56','1k','C21190','+5V_SENS','GND')
cap('C50','10uF','C14860','+5V_SENS','GND','25V','1206','CL31B106KAHNNNE')
for ref in ['J5','J6','J8','J9','J10','J14','J16','D14']:
    for num, net in parts[ref]['nets'].items():
        if net=='+5V': parts[ref]['nets'][num]='+5V_SENS'

# Restore RC522/ToF reset pull-downs, preventing undefined startup states.
resistor('R57','10k','C25804','TOF_XSHUT1','GND')
resistor('R58','10k','C25804','TOF_XSHUT2','GND')

# Arm feedback uses PC13 as INPUT. The core LED cannot be used as an indicator then.
# LVC latch output directly drives the input and the core LED's small resistor load.
parts['J13']['model']='PZ254V-11-08P';parts['J13']['code']='C492407'

# Resolve every purchase code to its exact public catalog model, including inherited
# parts whose displayed manufacturer model in the original library was corrupted.
for p in parts.values():
    lookup=BASE/(p['code']+'.json')
    if lookup.exists():
        result=json.loads(lookup.read_text(encoding='utf-8')).get('result') or {}
        detail=result.get('tipProductDetailUrlVO')
        if not detail:
            candidates=result.get('exactMatchResult') or (result.get('productSearchResultVO') or {}).get('productList') or []
            if isinstance(candidates,dict):candidates=[candidates]
            detail=next((v for v in candidates if v.get('productCode')==p['code']),None)
        if detail:
            p['model']=detail['productModel'];p['brand']=detail.get('brandNameEn','')
    if p['ref'].startswith(('J','JP')):
        p['value']=p['model']
    assert not p['model'].startswith(('RES_','CAP_')),p['ref']

# Add a local 100nF decoupler for every new digital IC and a VBUS bridge bypass.
for index,ref in enumerate(['U3','U4','U5','U6','U7','U8','U9','U10','U11','U12'],40):
    supply={'U3':'+5V','U4':'+3V3_EXT','U10':'+5V','U11':'CORE_3V3','U12':'VBUS_USB'}.get(ref,'+3V3_EXT')
    cap('C'+str(index),'100nF','C14663',supply,'GND',model='CC0603KRX7R9BB104')

PAGES=[('01 电池输入与65V降压', ['J1','F1','D1','D11','C1','C2','U1','L1','C3','C4','C23','C5','C6','C24','R2','R3','R21','R22','R31']),
 ('02 USB与核心板供电',['U3','C25','C26','JP1','U4','R32','C27','C28','Q4','Q5','R53','R54','R55','R56','C50','J2A','J2B','SW1','C40','C41']),
 ('03 急停与手动使能',['J4','R33','R34','R5','C29','U5','R35','U6','J17','R36','R37','C30','U7','U8','U9','Q3','R42','R43','D12','J18','C42','C43','C44','C45','C46']),
 ('04 电调与USB通信',['U10','R39','R40','R20','R41','J3','J15','R23','R24','U2','C21','C22','U11','U12','R50','R51','R52','C47','C48','C49']),
 ('05 模拟采集与双坐垫',['J16','R28','R29','R13','C8','D6','R10','R11','R12','C7','D5','J6','R44','R45','R14','C9','D7','J7','R30','R15','C10','D8','J20','R46','R47','C31','D13']),
 ('06 传感器与人机接口',['J5','R25','J8','R26','J9','R27','J10','J11','J21','R16','R17','J12','J13','R18','R19','R48','R57','R58','J14','R9','Q2','R49','D14','R4','R6','D4','R8','D3'])]
assert len({r for _,rr in PAGES for r in rr})==len(parts),(set(parts)-{r for _,rr in PAGES for r in rr})

# Emit a clean multi-page schematic with explicit wire labels, including unnamed old nets.
schroot=next(d for d in docs if d['head']['docType']=='SCH')
board=next(d for d in docs if d['head']['docType']=='BOARD')
oldpage=next(d for d in docs if d['head']['docType']=='SCH_PAGE')
docs.remove(oldpage)
page_docs=[]
for pi,(title,refs) in enumerate(PAGES):
    page=newdoc('SCH_PAGE',title)
    meta(page).update(schematic=schroot['head']['uuid'],zIndex=pi+1)
    page_docs.append(page)
    for i,ref in enumerate(refs):
        p=parts[ref];p['page']=title
        col=i%4;row=i//4
        x=190+col*450;y=-1700+row*240
        # Render all physical parts using a clear pin-box symbol; reused symbols are cloned into generic boxes.
        sym=generic_symbol(p['model'],p['pins'])
        dev=clone(p['dev'],p['model'])
        props=meta(dev)['attributes']
        props.update(Symbol=sym['head']['uuid'],Footprint=p['fp']['head']['uuid'],
                     **{'Supplier Part':p['code'],'Manufacturer Part':p['model'],'Value':p['value'],
                        'Name':'={Value}','Manufacturer':p['brand'],'Description':p['notes']})
        p['sym']=sym;p['dev']=dev
        part=p['model']+'.1';cid=uid();p['cid']=cid
        comp=add(page,'COMPONENT',{'partId':part,'x':x,'y':y,'rotation':0,'isMirror':False,'attrs':{},'zIndex':i+1},cid)
        for k,v in [('Device',dev['head']['uuid']),('Symbol',sym['head']['uuid']),('Unique ID',cid),('Designator',ref),('Name',p['value']),('Footprint',None)]:
            attribute(page,cid,k,v,x-40 if k in ['Designator','Name'] else None,
                      y-35 if k=='Designator' else y+52 if k=='Name' else None,k in ['Designator','Name'])
        # Move text away from the tall 20-pin core connector boxes.
        if ref in ['J2A','J2B']:
            for ar in page['records']:
                if ar['h']['type']=='ATTR' and ar['b'].get('parentId')==cid:
                    if ar['b']['key']=='Designator':ar['b']['y']=y-82
                    if ar['b']['key']=='Name':ar['b']['y']=y+87
        pinattrs={}
        for r in sym['records']:
            if r['h']['type']=='ATTR':pinattrs.setdefault(r['b'].get('parentId',''),{})[r['b']['key']]=r['b']['value']
        for r in sym['records']:
            if r['h']['type']!='PIN':continue
            b=r['b'];num=pinattrs[r['h']['id']]['Pin Number'];net=p['nets'][num]
            px=x+b['x'];py=y+b['y']
            if net is None:
                attribute(page,cid+'-'+r['h']['id'],'NO_CONNECT','yes',px,py)
                continue
            wid=uid();add(page,'WIRE',{'zIndex':i+1},wid)
            ex=px+(-15 if b['x']<0 else 15)
            add(page,'LINE',{'startX':px,'startY':py,'endX':ex,'endY':py,'lineGroup':wid,
                            'strokeColor':None,'strokeWidth':None,'strokeStyle':None})
            netattr=attribute(page,wid,'NET',net,ex,py-2,True)
            netattr['b']['align']='RIGHT_BOTTOM' if b['x']<0 else 'LEFT_BOTTOM'
    add(page,'TEXT',{'x':20,'y':-1830,'value':title+' / v2 / 7S Li-ion 29.4V max',
                    'fontSize':15,'rotation':0,'align':'LEFT_BOTTOM','zIndex':1000})

# Regenerate an accurate PCB netlist, with components deliberately unplaced and unrouted.
pcb=next(d for d in docs if d['head']['docType']=='PCB')
pcb['records']=[r for r in pcb['records'] if r['h']['type'] in
               ['DOCHEAD','META','LAYER','LAYER_PHYS','CANVAS','ACTIVE_LAYER','RULE','RULE_TEMPLATE','PREFERENCE']]
meta(pcb)['title']='PCB_v2_已同步_待布局布线'
netnames=sorted({n for p in parts.values() for n in p['nets'].values() if n})
for n in ['']+netnames:add(pcb,'NET',{'netType':None,'specialColor':None,'retLine':True,'differentialName':None,'isPositiveNet':False,'equalLengthGroupName':None},json.dumps(['NET',n],ensure_ascii=False,separators=(',',':')))
for i,p in enumerate(parts.values()):
    x=(i%10)*2500;y=(i//10)*2500
    cid=p['cid'];p['pcb_xy']=[x,y]
    add(pcb,'COMPONENT',{'partitionId':'','groupId':0,'layerId':1,'x':x,'y':y,'angle':0,
        'attrs':{'Unique ID':cid,'Channel ID':'$1'+cid,'Reuse Block':'','Group ID':''},'locked':False,'zIndex':-1},cid)
    for k,v in [('Footprint',p['fp']['head']['uuid']),('Device',p['dev']['head']['uuid']),('Designator',p['ref']),('Name',p['value'])]:
        add(pcb,'ATTR',{'partitionId':'','groupID':0,'parentId':cid,'layerId':3,'x':x,'y':y-220 if k=='Designator' else y+220,
            'key':k,'value':v,'keyVisible':False,'valueVisible':k=='Designator','fontFamily':'default',
            'fontSize':40,'strokeWidth':6,'bold':False,'italic':False,'origin':'LEFT_BOTTOM','angle':0,
            'reverse':False,'expansion':0,'mirror':False,'locked':False,'zIndex':1})
    for r in p['fp']['records']:
        if r['h']['type']!='PAD':continue
        num=r['b']['num'];assert num in p['nets'],(p['ref'],num,p['nets'])
        add(pcb,'PAD_NET',{'partitionId':'','padNet':p['nets'][num] or '', 'padLen':None,'attrsMap':{}},
            json.dumps(['PAD_NET',cid,num,r['h']['id']],separators=(',',':')))

# Keep only libraries referenced by the final devices; remove obsolete preview images.
used_dev={p['dev']['head']['uuid'] for p in parts.values()}
used_sym={p['sym']['head']['uuid'] for p in parts.values()}
used_fp={p['fp']['head']['uuid'] for p in parts.values()}
docs=[d for d in docs if d['head']['docType'] not in ['DEVICE','SYMBOL','FOOTPRINT'] or
      d['head']['uuid'] in used_dev|used_sym|used_fp]
order={'FOOTPRINT':0,'SYMBOL':1,'DEVICE':2,'BOARD':3,'SCH':4,'SCH_PAGE':5,'PCB':6,'CONFIG':7,'BLOB':8}
docs.sort(key=lambda d:order.get(d['head']['docType'],9))
for d in docs:
    d['head']['updateTime']=NOW
    for i,r in enumerate(d['records'],1):
        r['h']['ticket']=i;r['h'].pop('firstTicket',None)
    d['records'][0]['b']=d['head']
raw='\n'.join(json.dumps(r['h'],ensure_ascii=False,separators=(',',':'))+'||'+
               json.dumps(r['b'],ensure_ascii=False,separators=(',',':'))+'|'
               for d in docs for r in d['records'])+'\n'
output=OUT/'童骑卫-底板v2-原理图修订与PCB网表.epro2'
with zipfile.ZipFile(output,'w',zipfile.ZIP_DEFLATED) as z:
    z.writestr('project2.json',json.dumps({'title':'童骑卫-底板v2-7S方案','cbb_project':False,'editorVersion':'3.2.91',
        'introduction':'7S/29.4V max; schematic revision; synchronized unrouted PCB.',
        'description':'原理图修订与PCB网络同步；尚未通过编辑器ERC/DRC或实物验证。','tags':'[]'},ensure_ascii=False))
    z.writestr('童骑卫-底板v2.epru',raw)
with zipfile.ZipFile(output) as z:assert z.testzip() is None
(BASE/'parsed-project.json').write_text(json.dumps(docs,ensure_ascii=False,indent=2),encoding='utf-8')
simple=[{k:v for k,v in p.items() if k not in ['sym','dev','fp']}|{'footprint':meta(p['fp'])['title'],
    'device':p['dev']['head']['uuid'],'symbol':p['sym']['head']['uuid'],'footprint_id':p['fp']['head']['uuid']} for p in parts.values()]
(BASE/'parts.json').write_text(json.dumps(simple,ensure_ascii=False,indent=2),encoding='utf-8')
with (OUT/'底板v2-BOM.csv').open('w',encoding='utf-8-sig',newline='') as f:
    w=csv.writer(f);w.writerow(['位号','型号','立创编号','显示值','封装','页','说明'])
    for p in simple:w.writerow([p['ref'],p['model'],p['code'],p['value'],p['footprint'],p['page'],p['notes']])
with (OUT/'底板v2-逐脚网表.csv').open('w',encoding='utf-8-sig',newline='') as f:
    w=csv.writer(f);w.writerow(['位号','引脚','引脚名','网络','页'])
    for p in simple:
        for n,v in p['nets'].items():w.writerow([p['ref'],n,p['pins'][n],v or 'NC',p['page']])
print('OUTPUT',output,'PARTS',len(parts),'PAGES',len(page_docs),'NETS',len(netnames))
