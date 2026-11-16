var cfg = require('../../utils/appconfig.js');
var aliyun = require('../../utils/aliyun.js');
var mqtt = require('../../utils/mqtt.js');
var proto = require('../../utils/protocol.js');

/*  远程只读页。
 *
 *  这一页的存在本身就是在解释一句话：**云上看得到，但管不着。**
 *  整个文件里没有任何一处把云端数据转成 CMD_*。没有按钮，
 *  没有写入口，连 mqtt.publish 都没有调用过。
 *
 *  数据是降采样过的（方案 §6.6.3）：网关只发变化的属性，
 *  所以收到的 params 经常只有一两个字段。页面必须**合并**，
 *  不能每帧整体替换——否则速度会在一秒内闪回 "—" 好几次。
 */

var STALE_MS = 30000;

Page({
  data: {
    connecting: false,
    connected: false,
    status: '',
    error: '',
    host: '',
    topic: '',

    speed: '—',
    limit: '—',
    state: '—',
    faults: '—',
    battery: '—',
    wear: '—',
    trip: '—',
    rssi: '—',
    age: '—',
    lastRaw: ''
  },

  onLoad: function () {
    var sub = cfg.VIEW_TOPIC
      .replace('{pk}', cfg.PRODUCT_KEY)
      .replace('{dn}', cfg.VIEWER_DEVICE);

    this.setData({
      host: aliyun.mqttHost(cfg.PRODUCT_KEY, cfg.REGION),
      topic: sub
    });

    this._sub = sub;
    this._lastAt = 0;
    this._state = {};        /* 合并后的属性表 */

    var self = this;
    this._watch = setInterval(function () {
      if (!self._lastAt) {
        return;
      }
      var age = Date.now() - self._lastAt;
      self.setData({ age: Math.round(age / 1000) + ' s' });
    }, 1000);
  },

  onUnload: function () {
    clearInterval(this._watch);
    this._close();
  },

  connect: function () {
    if (this.data.connecting || this.data.connected) {
      return;
    }
    if (cfg.PRODUCT_KEY === 'YOUR_PK') {
      this.setData({
        error: '还没填阿里云三元组。打开 utils/appconfig.js，把 PRODUCT_KEY / ' +
               'VIEWER_DEVICE / VIEWER_SECRET 换成控制台里的值。'
      });
      return;
    }

    var self = this;
    var cred;
    try {
      cred = aliyun.buildCredentials({
        productKey: cfg.PRODUCT_KEY,
        deviceName: cfg.VIEWER_DEVICE,
        deviceSecret: cfg.VIEWER_SECRET
      });
    } catch (e) {
      this.setData({ error: e.message });
      return;
    }

    this.setData({ connecting: true, error: '', status: '准备连接…' });
    this._signInfo = cred;

    var client = new mqtt.MqttClient({
      url: aliyun.mqttUrl(cfg.PRODUCT_KEY, cfg.REGION, cfg.WS_PATH),
      clientId: cred.clientId,
      username: cred.username,
      password: cred.password,
      keepalive: 60,
      onStatus: function (s) { self.setData({ status: s }); },
      onConnect: function () {
        client.subscribe(self._sub).then(function (ok) {
          self.setData({
            connecting: false,
            connected: true,
            status: ok ? ('已订阅 ' + self._sub) : '订阅被拒（Topic 权限没配？）'
          });
        });
      },
      onMessage: function (topic, payload) { self._onMessage(topic, payload); }
    });

    this._client = client;
    client.connect();
  },

  reconnect: function () {
    this._close();
    this.setData({ connected: false, connecting: false });
    this.connect();
  },

  _close: function () {
    if (this._client) {
      try { this._client.disconnect(); } catch (e) { /* ignore */ }
      this._client = null;
    }
  },

  /*  payload 可能是三种形状：
   *   a) 规则引擎原样转发： {"id":"1","version":"1.0","method":"...","params":{...}}
   *   b) 转发时套了一层：   {"topic":"...","payload":{"id":...}}
   *   c) 套了一层且 payload 是字符串
   *  三种都认，因为控制台上换个转发模板形状就变了。 */
  _extractParams: function (text) {
    var obj;
    try {
      obj = JSON.parse(text);
    } catch (e) {
      return null;
    }
    if (!obj) {
      return null;
    }
    if (typeof obj.payload === 'string') {
      try { obj = JSON.parse(obj.payload); } catch (e) { return null; }
    } else if (obj.payload && typeof obj.payload === 'object') {
      obj = obj.payload;
    }
    return (obj && obj.params) ? obj.params : null;
  },

  _onMessage: function (topic, payload) {
    var params = this._extractParams(payload);
    if (!params) {
      return;
    }

    this._lastAt = Date.now();

    /* 合并，不替换 */
    for (var k in params) {
      if (Object.prototype.hasOwnProperty.call(params, k)) {
        this._state[k] = params[k];
      }
    }

    var s = this._state;
    var wearBits = (typeof s.wear === 'number') ? proto.decodeWear(s.wear) : null;

    this.setData({
      speed: (typeof s.speed === 'number') ? s.speed.toFixed(1) : this.data.speed,
      limit: (typeof s.limit === 'number') ? String(s.limit) : this.data.limit,
      state: (typeof s.state === 'number')
        ? (proto.STATE_NAMES[s.state] || ('未知(' + s.state + ')'))
        : this.data.state,
      faults: (typeof s.faults === 'number') ? faultText(s.faults) : this.data.faults,
      battery: (typeof s.battery === 'number') ? s.battery.toFixed(1) : this.data.battery,
      wear: wearBits ? wearText(wearBits) : this.data.wear,
      trip: (typeof s.trip_m === 'number') ? (s.trip_m + ' m') : this.data.trip,
      rssi: (typeof s.rssi === 'number') ? (s.rssi + ' dBm') : this.data.rssi,
      age: '0 s',
      lastRaw: payload.length > 300 ? payload.slice(0, 300) + '…' : payload
    });
  }
});

function faultText(bits) {
  var list = proto.decodeFaults(bits);
  if (list.length === 0) {
    return '无';
  }
  return list.map(function (f) { return f.name; }).join('、');
}

function wearText(w) {
  return (w.seat ? '坐垫✓ ' : '坐垫✗ ') +
         (w.belt ? '安全带✓ ' : '安全带✗ ') +
         (w.helmet ? '头盔✓' : '头盔✗');
}
