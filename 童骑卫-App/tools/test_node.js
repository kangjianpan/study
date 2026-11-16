/*  小程序工具层的 Node 侧回归测试
 *
 *      node tools/test_node.js
 *
 *  这些模块（protocol / sha256 / aliyun / mqtt）故意不碰 wx.*，
 *  所以能脱离微信开发者工具直接跑。跑不了的那些（BLE、页面）
 *  本来也只能真机验，模拟器没有蓝牙。
 *
 *  重点不在"覆盖率高"，在**跨端对齐**：协议字节、阿里云签名
 *  这两件事一旦两边算得不一样，现象是"连不上""数据不对"，
 *  在手机上排查极其费时间。
 */

var crypto = require('crypto');

var proto = require('../utils/protocol.js');
var sha = require('../utils/sha256.js');
var aliyun = require('../utils/aliyun.js');
var mqtt = require('../utils/mqtt.js');

var fails = 0;
var total = 0;

function check(name, cond, extra) {
  total++;
  if (cond) {
    console.log(pad(name, 44) + 'ok');
  } else {
    fails++;
    console.log(pad(name, 44) + 'FAIL' + (extra ? '  ' + extra : ''));
  }
}

function eq(name, actual, expected) {
  check(name, actual === expected,
        'got ' + JSON.stringify(actual) + ' want ' + JSON.stringify(expected));
}

function pad(s, n) {
  while (s.length < n) { s += ' '; }
  return s;
}

/* ---- 1. 遥测解码 ------------------------------------------------------ */

/*  造一帧 14 字节：速度 12.34 km/h、限速 15、故障 bit1(碰撞)+bit7(急停)、
 *  圈速 45670 ms、电压 118、穿带=坐垫+头盔、里程 1234 m、状态 RUN(3) */
var payload = new Uint8Array([
  0xD2, 0x04,      /* 1234 = 12.34 km/h */
  15,
  0x82,            /* bit1 | bit7 */
  0xD7, 0x11,      /* 4567 * 10 = 45670 ms */
  118,             /* 11.8 V */
  0x11,            /* seat(bit0) | helmet(bit4)，安全带没扣 */
  0xD2, 0x04,      /* 1234 m */
  proto.STATE_RUN,
  0, 0, 0
]);

var t = proto.decodeTelemetry(payload);
check('decode valid', t.valid);
eq('decode speed', t.speed, 12.34);
eq('decode limit', t.limit, 15);
eq('decode lapMs', t.lapMs, 45670);
eq('decode volts', t.volts, 11.8);
eq('decode tripM', t.tripM, 1234);
eq('decode stateName', t.stateName, '行驶');
eq('decode fault count', t.faultList.length, 2);
eq('decode fault names', t.faultList.map(function (f) { return f.name; }).join(','), '碰撞,急停');

var v2Payload = new Uint8Array(payload);
v2Payload[6] = 255; /* Legacy field saturates above 25.5V. */
v2Payload[11] = 0x1c; /* Sensor, pedal and commissioning faults. */
v2Payload[12] = 0xd8;
v2Payload[13] = 0x72; /* 29400mV */
var v2 = proto.decodeTelemetry(v2Payload);
eq('v2 7S full-charge voltage', v2.volts, 29.4);
eq('v2 full fault word', v2.faults, 0x1c82);
eq('v2 fault count', v2.faultList.length, 5);

/*  穿戴位：每个传感器只占 1 位，位置 0/2/4 —— 对着 app_types.h 抄的。
 *  0x11 = 0001 0001b → seat 位0=1、belt 位2=0、helmet 位4=1。 */
check('wear seat', t.wearState.seat === true);
check('wear belt', t.wearState.belt === false);
check('wear helmet', t.wearState.helmet === true);

/*  长度不对必须拒，而不是"尽量解" */
check('reject 13 bytes', proto.decodeTelemetry(new Uint8Array(13)).valid === false);
check('reject 15 bytes', proto.decodeTelemetry(new Uint8Array(15)).valid === false);

/*  所有位都置 1 时三件装备都判为合格 */
var allWear = proto.decodeWear(0x1F);
check('wear all set', allWear.seat && allWear.belt && allWear.helmet);

/* ---- 2. 命令打包 ------------------------------------------------------ */

/*  已知帧（STM32 README 4.1）：AA 55 01 20 01 08 78
 *  小程序负责的正是中间那段 20 01 08，ESP32 补头补尾。 */
var cmdLimit8 = proto.buildCommand(proto.CMD.PARENT_LIMIT, [8]);
eq('build parent-limit 8', proto.toHex(cmdLimit8), '20 01 08');

var cmdStop = proto.buildCommand(proto.CMD.STOP, []);
eq('build stop', proto.toHex(cmdStop), '21 00');

/*  白名单：0x25/0x26/0x27/0x40 在 App 侧就该发不出去 */
var blockedOk = true;
[0x25, 0x26, 0x27, 0x40].forEach(function (c) {
  var threw = false;
  try { proto.buildCommand(c, []); } catch (e) { threw = true; }
  if (!threw) { blockedOk = false; }
});
check('whitelist blocks 0x25/26/27/40', blockedOk);

/*  限速夹在 6..20 */
eq('limit clamps low', proto.toHex(proto.buildLimitCommand(proto.CMD.PARENT_LIMIT, 2)), '20 01 06');
eq('limit clamps high', proto.toHex(proto.buildLimitCommand(proto.CMD.PARENT_LIMIT, 99)), '20 01 14');

/* ---- 3. SHA-256 / HMAC ------------------------------------------------ */

var shaCases = ['', 'abc', 'GuardKart', repeat('x', 55), repeat('y', 56),
                repeat('z', 64), repeat('w', 1000), '童骑卫'];
for (var i = 0; i < shaCases.length; i++) {
  if (shaCases[i].length > 20) { continue; }
  eq('sha256 ' + JSON.stringify(shaCases[i]).slice(0, 16),
     sha.sha256Hex(shaCases[i]),
     crypto.createHash('sha256').update(shaCases[i], 'utf8').digest('hex'));
}

/*  分块边界：56/64/1000 字节是补齐规则的三个坎 */
[55, 56, 63, 64, 65, 1000].forEach(function (n) {
  var s = repeat('a', n);
  eq('sha256 len ' + n,
     sha.sha256Hex(s),
     crypto.createHash('sha256').update(s, 'utf8').digest('hex'));
});

/*  HMAC：随机 100 组，对着 Node 的 crypto 比 */
var hmacBad = 0;
for (var k = 0; k < 100; k++) {
  var key = crypto.randomBytes(k % 80).toString('hex');
  var msg = crypto.randomBytes(k % 200).toString('base64');
  var mine = sha.hmacSha256Hex(key, msg);
  var ref = crypto.createHmac('sha256', Buffer.from(key, 'utf8'))
                  .update(msg, 'utf8').digest('hex');
  if (mine !== ref) { hmacBad++; }
}
eq('hmac-sha256 x100 random', hmacBad, 0);

/* ---- 4. 阿里云一机一密 ------------------------------------------------ */

var cred = aliyun.buildCredentials({
  productKey: 'a1B2c3D4e5F',
  deviceName: 'GuardKart-01-viewer',
  deviceSecret: 'SECRET0123456789',
  timestamp: 1760000000
});

eq('clientId', cred.clientId,
   'GuardKart-01-viewer|securemode=2,signmethod=hmacsha256,timestamp=1760000000|');
eq('username', cred.username, 'GuardKart-01-viewer&a1B2c3D4e5F');

/*  签名串必须是**裸 clientId**，不带竖线后缀 */
eq('signContent', cred.signContent,
   'clientIdGuardKart-01-viewerdeviceNameGuardKart-01-viewer' +
   'productKeya1B2c3D4e5Ftimestamp1760000000');

var refPwd = crypto.createHmac('sha256', 'SECRET0123456789')
                   .update(cred.signContent, 'utf8').digest('hex');
eq('password', cred.password, refPwd);
eq('password is 64 hex', cred.password.length, 64);

eq('mqtt host', aliyun.mqttHost('a1B2c3D4e5F', 'cn-shanghai'),
   'a1B2c3D4e5F.iot-as-mqtt.cn-shanghai.aliyuncs.com');

eq('property post topic',
   aliyun.propertyPostTopic('a1B2c3D4e5F', 'GuardKart-01'),
   '/sys/a1B2c3D4e5F/GuardKart-01/thing/event/property/post');

var parsedTopic = aliyun.deviceFromTopic('/sys/a1B2c3D4e5F/GuardKart-01/thing/event/property/post');
check('deviceFromTopic', parsedTopic && parsedTopic.deviceName === 'GuardKart-01');

/* ---- 5. MQTT 编码 / 分帧 ---------------------------------------------- */

eq('remaining 0', mqtt.encRemaining(0).join(','), '0');
eq('remaining 127', mqtt.encRemaining(127).join(','), '127');
eq('remaining 128', mqtt.encRemaining(128).join(','), '128,1');
eq('remaining 16383', mqtt.encRemaining(16383).join(','), '255,127');
eq('remaining 16384', mqtt.encRemaining(16384).join(','), '128,128,1');

/*  CONNECT 报文逐字节检查：这是最容易写错、又最难在手机上看出错的地方 */
var c = new mqtt.MqttClient({
  url: 'wss://x', clientId: 'cid', username: 'user', password: 'pass', keepalive: 60
});
var cp = c._connectPacket();
var head = parseMqtt(cp);

eq('connect type', head.type, 1);
eq('connect protocol name', head.protocol, 'MQTT');
eq('connect level', head.level, 4);
eq('connect flags', head.flags, 0xC2);       /* clean | user | pass */
eq('connect keepalive', head.keepalive, 60);
eq('connect clientId', head.clientId, 'cid');
eq('connect username', head.username, 'user');
eq('connect password', head.password, 'pass');

/*  没有用户名密码时 flags 只有 clean session */
var c2 = new mqtt.MqttClient({ url: 'wss://x', clientId: 'cid2' });
eq('connect flags anon', parseMqtt(c2._connectPacket()).flags, 0x02);

/*  分帧：一包拆成两半送进来，必须等到齐了才回调，且只回调一次 */
var topic = '/sys/pk/dn/user/view';
var body = mqtt.concat([mqtt.encStr(topic), sha.utf8Bytes('{"params":{"speed":1.5}}')]);
var pkt = mqtt.packet(3, 0, body);

var got = [];
var rc = makeRecv();
rc.onMessage = function (tt, pp) { got.push([tt, pp]); };
rc._feed(pkt.slice(0, 5));
eq('partial packet no callback', got.length, 0);
rc._feed(pkt.slice(5));
eq('full packet one callback', got.length, 1);
eq('publish topic', got[0][0], topic);
eq('publish payload', got[0][1], '{"params":{"speed":1.5}}');

/*  一次送进两包，必须两包都解出来 */
var rc2 = makeRecv();
var got2 = [];
rc2.onMessage = function (tt, pp) { got2.push(pp); };
rc2._feed(mqtt.concat([pkt, pkt]));
eq('two packets in one chunk', got2.length, 2);

/*  超过 127 字节的报文走两字节剩余长度，分帧逻辑要能过 127 这个坎 */
var bigBody = mqtt.concat([mqtt.encStr(topic), sha.utf8Bytes(repeat('q', 200))]);
var bigPkt = mqtt.packet(3, 0, bigBody);
check('big packet >127 bytes', bigPkt.length > 129);
var rc3 = makeRecv();
var big = null;
rc3.onMessage = function (tt, pp) { big = pp; };
rc3._feed(bigPkt.slice(0, 10));
rc3._feed(bigPkt.slice(10));
eq('big packet payload len', big ? big.length : -1, 200);

/*  CONNACK 0 必须把 connected 置上 */
var rc4 = makeRecv();
var connectedFired = false;
rc4.onConnect = function () { connectedFired = true; };
rc4._startPing = function () {};           /* 测试里不开定时器 */
rc4._feed(new Uint8Array([0x20, 0x02, 0x00, 0x00]));
check('connack ok', rc4.connected === true && connectedFired);

/*  CONNACK 5 = 用户名密码错，不能置 connected */
var rc5 = makeRecv();
rc5._startPing = function () {};
rc5.onConnect = function () { throw new Error('should not fire'); };
rc5._feed(new Uint8Array([0x20, 0x02, 0x00, 0x05]));
check('connack reject 5', !rc5.connected);

/* ---- 结果 ------------------------------------------------------------- */

console.log('');
console.log(fails ? (fails + ' / ' + total + ' FAILED') : ('all ' + total + ' good'));
process.exit(fails ? 1 : 0);

/* ---- 小工具 ----------------------------------------------------------- */

function repeat(s, n) {
  var out = '';
  while (out.length < n) { out += s; }
  return out.slice(0, n);
}

/*  一个只有分帧能力的假客户端：不碰 wx，不连网 */
function makeRecv() {
  var o = Object.create(mqtt.MqttClient.prototype);
  o.buf = new Uint8Array(0);
  o._subWaiters = {};
  o.onMessage = function () {};
  o.onStatus = function () {};
  o.onConnect = function () {};
  return o;
}

/*  手写一个 CONNECT 解析器。故意不复用被测代码，
 *  否则同一个错误会在两边同时"通过"。 */
function parseMqtt(b) {
  var i = 1;
  var mult = 1, len = 0, d;
  do {
    d = b[i++];
    len += (d & 0x7f) * mult;
    mult *= 128;
  } while (d & 0x80);

  var out = { type: b[0] >> 4, remaining: len };
  var end = i + len;

  function str() {
    var n = (b[i] << 8) | b[i + 1];
    i += 2;
    var s = '';
    for (var j = 0; j < n; j++) { s += String.fromCharCode(b[i + j]); }
    i += n;
    return s;
  }

  out.protocol = str();
  out.level = b[i++];
  out.flags = b[i++];
  out.keepalive = (b[i] << 8) | b[i + 1];
  i += 2;
  out.clientId = str();
  if (out.flags & 0x80) { out.username = str(); }
  if (out.flags & 0x40) { out.password = str(); }
  out.end = end;
  out.consumed = i;
  return out;
}
