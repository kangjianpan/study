/*  MQTT 3.1.1 over wx.connectSocket（wss）
 *
 *  只实现「远程只读」需要的部分：CONNECT / SUBSCRIBE / PUBLISH(QoS0) /
 *  PINGREQ / DISCONNECT。没有 QoS1/2、没有遗嘱、没有 TLS 证书管理
 *  （wss 由微信托管），够用且代码短到能看懂。
 *
 *  为什么不用现成的 npm 包：小程序对 npm 构建有额外步骤，
 *  而且大多数 MQTT 库会在 Node 环境下 require('net')，
 *  在小程序里直接白屏。200 行自己写，出问题看得见。
 */

var sha = require('./sha256.js');

/* ---- 编码 ------------------------------------------------------------ */

function utf8(s) {
  return sha.utf8Bytes(s);
}

function encStr(s) {
  var b = utf8(s);
  var out = new Uint8Array(2 + b.length);
  out[0] = (b.length >> 8) & 0xff;
  out[1] = b.length & 0xff;
  out.set(b, 2);
  return out;
}

/* 剩余长度变长编码，1..4 字节 */
function encRemaining(n) {
  var out = [];
  do {
    var d = n % 128;
    n = Math.floor(n / 128);
    if (n > 0) {
      d = d | 0x80;
    }
    out.push(d);
  } while (n > 0);
  return new Uint8Array(out);
}

function concat(parts) {
  var total = 0;
  for (var i = 0; i < parts.length; i++) {
    total += parts[i].length;
  }
  var out = new Uint8Array(total);
  var off = 0;
  for (i = 0; i < parts.length; i++) {
    out.set(parts[i], off);
    off += parts[i].length;
  }
  return out;
}

function packet(type, flags, body) {
  var header = new Uint8Array([(type << 4) | (flags || 0)]);
  return concat([header, encRemaining(body.length), body]);
}

function toBytes(data) {
  if (data instanceof ArrayBuffer) {
    return new Uint8Array(data);
  }
  if (data instanceof Uint8Array) {
    return data;
  }
  if (typeof data === 'string') {
    var s = new Uint8Array(data.length);
    for (var i = 0; i < data.length; i++) {
      s[i] = data.charCodeAt(i) & 0xff;
    }
    return s;
  }
  return new Uint8Array(0);
}

function strFromBytes(b, off, len) {
  var s = '';
  for (var i = 0; i < len; i++) {
    s += String.fromCharCode(b[off + i]);
  }
  return s;
}

/* ---- 客户端 ---------------------------------------------------------- */

function MqttClient(opts) {
  this.url = opts.url;
  this.clientId = opts.clientId;
  this.username = opts.username || '';
  this.password = opts.password || '';
  this.keepalive = opts.keepalive || 60;

  this.onMessage = opts.onMessage || function () {};
  this.onStatus = opts.onStatus || function () {};
  this.onConnect = opts.onConnect || function () {};

  this.sock = null;
  this.buf = new Uint8Array(0);
  this.connected = false;
  this.closedByUser = false;

  this._nextId = 1;
  this._subWaiters = {};
  this._connWaiter = null;
  this._pingTimer = null;
}

MqttClient.prototype._status = function (text) {
  this.onStatus(text);
};

MqttClient.prototype.connect = function () {
  var self = this;
  self.closedByUser = false;
  self._status('wss 连接中…');

  var sock = wx.connectSocket({
    url: self.url,
    protocols: ['mqtt'],
    /* 小程序里 header 不能带 User-Agent 之类的受限字段 */
    header: { 'Content-Type': 'application/octet-stream' },
    fail: function (e) {
      self._status('连接失败：' + (e.errMsg || JSON.stringify(e)));
    }
  });

  self.sock = sock;
  self.buf = new Uint8Array(0);

  sock.onOpen(function () {
    self._status('wss 已建立，MQTT 握手…');
    self._send(self._connectPacket());
  });

  sock.onMessage(function (res) {
    self._feed(toBytes(res.data));
  });

  sock.onError(function (e) {
    self._status('socket 错误：' + (e.errMsg || 'unknown'));
    self._teardown();
  });

  sock.onClose(function () {
    self._teardown();
    if (!self.closedByUser) {
      self._status('连接已断开');
    }
  });
};

MqttClient.prototype._connectPacket = function () {
  var flags = 0x02;                      /* clean session */
  if (this.username) { flags |= 0x80; }
  if (this.password) { flags |= 0x40; }

  var body = concat([
    encStr('MQTT'),
    new Uint8Array([0x04, flags,
                    (this.keepalive >> 8) & 0xff, this.keepalive & 0xff]),
    encStr(this.clientId),
    this.username ? encStr(this.username) : new Uint8Array(0),
    this.password ? encStr(this.password) : new Uint8Array(0)
  ]);

  return packet(1, 0, body);              /* type 1 = CONNECT */
};

MqttClient.prototype.subscribe = function (topic) {
  var id = this._nextId++;
  var self = this;
  var body = concat([
    new Uint8Array([(id >> 8) & 0xff, id & 0xff]),
    encStr(topic),
    new Uint8Array([0x00])                /* QoS 0 */
  ]);
  this._send(packet(8, 0x02, body));      /* type 8 = SUBSCRIBE, flags 0x02 */
  return new Promise(function (resolve) {
    self._subWaiters[id] = resolve;
  });
};

MqttClient.prototype.publish = function (topic, payload) {
  var body = concat([encStr(topic), utf8(payload)]);
  this._send(packet(3, 0, body));         /* type 3 = PUBLISH, QoS 0 */
};

MqttClient.prototype.disconnect = function () {
  this.closedByUser = true;
  try {
    if (this.connected) {
      this._send(packet(14, 0, new Uint8Array(0)));   /* DISCONNECT */
    }
  } catch (e) { /* socket 可能已经没了 */ }
  this._teardown();
  try {
    if (this.sock) { this.sock.close({}); }
  } catch (e) { /* ignore */ }
  this.sock = null;
};

MqttClient.prototype._send = function (bytes) {
  if (!this.sock) {
    return;
  }
  this.sock.send({ data: bytes.buffer });
};

MqttClient.prototype._teardown = function () {
  this.connected = false;
  if (this._pingTimer) {
    clearInterval(this._pingTimer);
    this._pingTimer = null;
  }
};

/* ---- 收包与分帧 ------------------------------------------------------ */

MqttClient.prototype._feed = function (chunk) {
  this.buf = concat([this.buf, chunk]);

  for (;;) {
    if (this.buf.length < 2) {
      return;
    }

    /* 解剩余长度 */
    var mult = 1;
    var len = 0;
    var i = 1;
    var ok = false;
    while (i < this.buf.length && i <= 4) {
      var d = this.buf[i++];
      len += (d & 0x7f) * mult;
      mult *= 128;
      if ((d & 0x80) === 0) {
        ok = true;
        break;
      }
    }
    if (!ok) {
      return;                              /* 长度字段还没收全 */
    }
    if (this.buf.length < i + len) {
      return;                              /* 报文体还没收全 */
    }

    var type = (this.buf[0] >> 4) & 0x0f;
    var flags = this.buf[0] & 0x0f;
    var body = this.buf.slice(i, i + len);
    this.buf = this.buf.slice(i + len);

    this._handle(type, flags, body);
  }
};

MqttClient.prototype._handle = function (type, flags, body) {
  var self = this;

  if (type === 2) {                        /* CONNACK */
    var rc = body.length > 1 ? body[1] : 0xff;
    if (rc === 0) {
      self.connected = true;
      self._status('已连接');
      self._startPing();
      self.onConnect();
    } else {
      self._status('MQTT 拒绝连接，返回码 ' + rc + (rc === 5 ? '（用户名或密码错，多半是签名对不上）' : ''));
    }
    return;
  }

  if (type === 3) {                        /* PUBLISH */
    var tlen = (body[0] << 8) | body[1];
    var topic = strFromBytes(body, 2, tlen);
    var off = 2 + tlen;
    var qos = (flags >> 1) & 0x03;
    if (qos > 0) {
      off += 2;                            /* packet id，QoS0 没有 */
    }
    var payload = strFromBytes(body, off, body.length - off);
    self.onMessage(topic, payload);
    return;
  }

  if (type === 9) {                        /* SUBACK */
    var id = (body[0] << 8) | body[1];
    var waiter = self._subWaiters[id];
    if (waiter) {
      delete self._subWaiters[id];
      waiter((body[2] === 0x80) ? false : true);
    }
    return;
  }

  if (type === 13) {                       /* PINGRESP */
    return;
  }
};

MqttClient.prototype._startPing = function () {
  var self = this;
  if (self._pingTimer) {
    clearInterval(self._pingTimer);
  }
  /* keepalive 的一半发一次，避免刚好卡在边界上被判超时 */
  self._pingTimer = setInterval(function () {
    if (self.connected) {
      self._send(packet(12, 0, new Uint8Array(0)));   /* PINGREQ */
    }
  }, Math.max(5, Math.floor(self.keepalive / 2)) * 1000);
};

module.exports = {
  MqttClient: MqttClient,
  encRemaining: encRemaining,
  encStr: encStr,
  packet: packet,
  concat: concat,
  toBytes: toBytes
};
