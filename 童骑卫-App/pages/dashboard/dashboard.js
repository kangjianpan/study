var ble = require('../../utils/ble.js');
var proto = require('../../utils/protocol.js');

/*  超过这个时间没收到帧就认为链路断了。
 *  网关 10 Hz 推，1.5 s = 丢掉 15 帧，足够确认不是抖动。 */
var STALE_MS = 1500;

Page({
  data: {
    connected: false,
    stale: true,
    speed: '0.0',
    speedPct: 0,
    limit: '--',
    stateName: '--',
    stateClass: 'pill-idle',
    faults: [],
    wear: { seat: false, belt: false, helmet: false },
    volts: '--',
    frameAge: '--'
  },

  onLoad: function () {
    this._offTelemetry = ble.onTelemetry(this._onFrame.bind(this));
    this._offState = ble.onState(this._syncConn.bind(this));
    this._lastAt = 0;

    var self = this;
    /* 看门狗：链路断了之后没有"事件"，只能靠定时器发现 */
    this._watch = setInterval(function () {
      if (!self._lastAt) {
        return;
      }
      var age = Date.now() - self._lastAt;
      self.setData({
        stale: age > STALE_MS,
        frameAge: age > 9999 ? '>10 s' : (age / 1000).toFixed(1) + ' s'
      });
    }, 500);

    this._syncConn();
  },

  onShow: function () {
    this._syncConn();
  },

  onUnload: function () {
    if (this._offTelemetry) { this._offTelemetry(); }
    if (this._offState) { this._offState(); }
    clearInterval(this._watch);
  },

  _syncConn: function () {
    this.setData({ connected: ble.isConnected() });
  },

  _onFrame: function (t) {
    this._lastAt = Date.now();

    /*  限速上限用来画速度条。0 表示还没收到过有效限速，
     *  除零会让 WXML 里的百分比变成 NaN，所以先兜住。 */
    var pct = 0;
    if (t.limit > 0) {
      pct = Math.round((t.speed / t.limit) * 100);
      if (pct > 100) { pct = 100; }
    }

    this.setData({
      stale: false,
      speed: t.speed.toFixed(1),
      speedPct: pct,
      limit: String(t.limit),
      stateName: t.stateName,
      stateClass: this._stateClass(t.state),
      faults: t.faultList.map(function (f) { return f.name; }),
      wear: t.wearState,
      volts: t.volts.toFixed(1),
      frameAge: '0.0 s'
    });
  },

  _stateClass: function (s) {
    if (s === proto.STATE_FAULT_LOCK) { return 'pill-bad'; }
    if (s === proto.STATE_RUN) { return 'pill-ok'; }
    if (s === proto.STATE_DECEL || s === proto.STATE_SELF_TEST) { return 'pill-warn'; }
    return 'pill-idle';
  },

  goConnect: function () {
    wx.switchTab({ url: '/pages/connect/connect' });
  },

  goRemote: function () {
    wx.navigateTo({ url: '/pages/remote/remote' });
  }
});
