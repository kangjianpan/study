/*  童骑卫 14 字节遥测解码 + 命令打包
 *
 *  这份文件的每一个偏移都对着 STM32 固件抄，不是照着方案文档写的：
 *      Core/Src/app_telemetry.c   —— 组包
 *      Core/Inc/app_types.h       —— 故障位 / 穿戴位
 *      Core/Inc/app_state.h       —— 状态枚举
 *  文档与代码不一致时以代码为准，文中标注了不一致的地方。
 *
 *  纯 CommonJS，没有任何 wx.* 依赖，所以能直接在 Node 里跑测试
 *  （tools/test_node.js）。
 */

/* ---- 状态机 ---------------------------------------------------------- */
/*  app_state.h: SELF_TEST=0 STANDBY=1 READY=2 RUN=3 DECEL=4 FAULT_LOCK=5 */
var STATE_NAMES = ['自检', '待机', '就绪', '行驶', '减速', '故障锁定'];
var STATE_SELF_TEST = 0;
var STATE_STANDBY = 1;
var STATE_READY = 2;
var STATE_RUN = 3;
var STATE_DECEL = 4;
var STATE_FAULT_LOCK = 5;

/* ---- 故障位 ---------------------------------------------------------- */
/*  app_types.h: FAULT_* 每个占 1 位，bit0..bit7 */
var FAULTS = [
  { bit: 0, name: '翻车', short: '翻车' },
  { bit: 1, name: '碰撞', short: '碰撞' },
  { bit: 2, name: '过流', short: '过流' },
  { bit: 3, name: '互锁', short: '互锁' },
  { bit: 4, name: '链路丢失', short: '链路' },
  { bit: 5, name: '超速', short: '超速' },
  { bit: 6, name: '欠压', short: '欠压' },
  { bit: 7, name: '急停', short: '急停' },
  { bit: 8, name: '远程停车', short: '远停' },
  { bit: 9, name: '故障注入', short: '注入' },
  { bit: 10, name: '传感器失效', short: '传感器' },
  { bit: 11, name: '踏板异常', short: '踏板' },
  { bit: 12, name: '等待硬件联调', short: '待联调' },
  { bit: 13, name: '手动使能反馈异常', short: '使能反馈' }
];

/* ---- 命令字 ---------------------------------------------------------- */
/*  与 app_protocol.h 一致；标 blocked 的四个是网关白名单里被拒收的 */
var CMD = {
  TELEMETRY: 0x10,
  PARENT_LIMIT: 0x20,
  STOP: 0x21,
  RESUME: 0x22,
  VENUE_LIMIT: 0x23,
  CHILD_LIMIT: 0x24,
  FORCE_THROTTLE: 0x25,
  FORCE_SPEED: 0x26,
  SIM_WEAR_FAIL: 0x27,
  HEARTBEAT: 0x30,
  FAULT_INJECT: 0x40
};

/*  小程序只能发这四个。0x25/0x26/0x27/0x40 能伪造车辆自身状态，
 *  ESP32 侧 cmd_allowed() 会丢，STM32 侧调试固件之外也不响应。
 *  这里再挡一次，是为了在 UI 上就别给按钮。 */
var CMD_ALLOWED = [CMD.PARENT_LIMIT, CMD.STOP, CMD.RESUME, CMD.VENUE_LIMIT, CMD.CHILD_LIMIT];

var LIMIT_MIN_KMH = 6;
var LIMIT_MAX_KMH = 20;

/* ---- 遥测解码 -------------------------------------------------------- */

function u16le(b, i) {
  return (b[i] | (b[i + 1] << 8)) & 0xffff;
}

/*  输入 ArrayBuffer / Uint8Array / 普通数组，输出结构化对象。
 *  长度不是 14 就返回 valid:false —— 网关只会转发 LEN==14 的帧，
 *  走到这个分支说明链路上有别的东西在说话。 */
function decodeTelemetry(buf) {
  var b = toBytes(buf);

  if (b.length !== 14) {
    return { valid: false, reason: '长度应为 14，实际 ' + b.length };
  }

  var speedX100 = u16le(b, 0);
  var wear = b[7];

  return {
    valid: true,
    speedX100: speedX100,
    speed: speedX100 / 100,
    limit: b[2],
    faults: b[3] | (b[11] << 8),
    faultList: decodeFaults(b[3] | (b[11] << 8)),
    lapMs: u16le(b, 4) * 10,
    volts: u16le(b, 12) ? u16le(b, 12) / 1000 : b[6] / 10,
    wear: wear,
    wearState: decodeWear(wear),
    tripM: u16le(b, 8),
    state: b[10],
    stateName: STATE_NAMES[b[10]] || ('未知(' + b[10] + ')')
  };
}

function decodeFaults(bits) {
  var out = [];
  for (var i = 0; i < FAULTS.length; i++) {
    if (bits & (1 << FAULTS[i].bit)) {
      out.push(FAULTS[i]);
    }
  }
  return out;
}

/*  穿戴字节：每个传感器只占 1 位，位置是 0 / 2 / 4。
 *
 *  这是 app_types.h 的实际实现：
 *      #define WEAR_SEAT_SHIFT   0
 *      #define WEAR_BELT_SHIFT   2
 *      #define WEAR_HELMET_SHIFT 4
 *  方案文档 §6.2 早期写成「bit0-1 / bit2-3 / bit4-5 两位字段」，
 *  描述的是预留布局，代码里每个字段只用了低位。两种读法在
 *  「1=合格」这一位上结果相同，这里按代码读。
 *  以后固件若启用高位（例如区分「未系」与「系错」），改这一处即可。 */
function decodeWear(wear) {
  return {
    seat: ((wear >> 0) & 1) === 1,
    belt: ((wear >> 2) & 1) === 1,
    helmet: ((wear >> 4) & 1) === 1
  };
}

/* ---- 命令打包 -------------------------------------------------------- */

/*  小程序写 FFF2 时不需要 AA55 帧头和 CRC，只发：
 *      [CMD] [LEN] [PAYLOAD...]
 *  ESP32 补帧头、配 SEQ、算 CRC8。这里返回 ArrayBuffer，
 *  因为 wx.writeBLECharacteristicValue 只收 ArrayBuffer。
 */
function buildCommand(cmd, payload) {
  var p = payload ? toBytes(payload) : new Uint8Array(0);

  if (CMD_ALLOWED.indexOf(cmd) < 0) {
    throw new Error('命令 0x' + hex2(cmd) + ' 不在小程序可发白名单内');
  }
  if (p.length > 16) {
    throw new Error('payload 过长：' + p.length + ' 字节');
  }

  var out = new Uint8Array(2 + p.length);
  out[0] = cmd;
  out[1] = p.length;
  out.set(p, 2);
  return out.buffer;
}

/*  限速命令的 payload 是 1 字节，单位 km/h。
 *  夹在 6..20 之间：低于 6 车子动不了，高于 20 场地根本不允许。 */
function buildLimitCommand(cmd, kmh) {
  var v = Math.round(kmh);
  if (v < LIMIT_MIN_KMH) { v = LIMIT_MIN_KMH; }
  if (v > LIMIT_MAX_KMH) { v = LIMIT_MAX_KMH; }
  return buildCommand(cmd, new Uint8Array([v]));
}

/* ---- 小工具 ---------------------------------------------------------- */

function toBytes(buf) {
  if (!buf) {
    return new Uint8Array(0);
  }
  if (buf instanceof Uint8Array) {
    return buf;
  }
  if (typeof ArrayBuffer !== 'undefined' && buf instanceof ArrayBuffer) {
    return new Uint8Array(buf);
  }
  if (Array.isArray(buf)) {
    return new Uint8Array(buf);
  }
  /* wx 在个别基础库版本上把二进制当字符串给，这里兜一层 */
  if (typeof buf === 'string') {
    var s = new Uint8Array(buf.length);
    for (var i = 0; i < buf.length; i++) {
      s[i] = buf.charCodeAt(i) & 0xff;
    }
    return s;
  }
  return new Uint8Array(0);
}

function hex2(v) {
  return ('0' + (v & 0xff).toString(16)).slice(-2).toUpperCase();
}

function toHex(buf) {
  var b = toBytes(buf);
  var s = [];
  for (var i = 0; i < b.length; i++) {
    s.push(hex2(b[i]));
  }
  return s.join(' ');
}

/*  圈速与里程：目前固件 p[4..5] 和 p[8..9] 固定填 0，
 *  UI 需要据此显示「未实现」而不是显示一个漂亮的 0。 */
function hasLapData(t) { return t.valid && t.lapMs > 0; }
function hasTripData(t) { return t.valid && t.tripM > 0; }

module.exports = {
  STATE_NAMES: STATE_NAMES,
  STATE_SELF_TEST: STATE_SELF_TEST,
  STATE_STANDBY: STATE_STANDBY,
  STATE_READY: STATE_READY,
  STATE_RUN: STATE_RUN,
  STATE_DECEL: STATE_DECEL,
  STATE_FAULT_LOCK: STATE_FAULT_LOCK,
  FAULTS: FAULTS,
  CMD: CMD,
  CMD_ALLOWED: CMD_ALLOWED,
  LIMIT_MIN_KMH: LIMIT_MIN_KMH,
  LIMIT_MAX_KMH: LIMIT_MAX_KMH,
  decodeTelemetry: decodeTelemetry,
  decodeFaults: decodeFaults,
  decodeWear: decodeWear,
  buildCommand: buildCommand,
  buildLimitCommand: buildLimitCommand,
  hasLapData: hasLapData,
  hasTripData: hasTripData,
  toBytes: toBytes,
  toHex: toHex
};
