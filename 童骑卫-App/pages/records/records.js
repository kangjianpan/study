var ble = require('../../utils/ble.js');
var proto = require('../../utils/protocol.js');

/*  车上的圈速和里程两个字段，固件目前固定填 0（app_telemetry.c 里
 *  p[4..5]、p[8..9] 写着 "not implemented"）。所以这个页面分两半：
 *
 *    上半：车上给的 → 现在一定是空的，如实显示"未实现"，不编数据
 *    下半：手机自己数的 → 只要连着就有内容，演示时不至于空屏
 *
 *  手机这一份是"观察值"，不是车辆仪表：断链期间的车速它不知道，
 *  所以断链次数和链路可用率一起显示，让数字自己带着可信度。
 */

var EXPECTED_HZ = 10;
var GAP_MS = 200;          /* 两帧间隔超过 200 ms 算一次断链（正常 100 ms） */

function fmtLap(ms) {
  return ms > 0 ? (ms / 1000).toFixed(2) + ' s' : '—';
}

function fmtTrip(m) {
  if (m <= 0) {
    return '—';
  }
  return m >= 1000 ? (m / 1000).toFixed(2) + ' km' : m + ' m';
}

Page({
  data: {
    connected: false,
    lapMs: 0,
    tripM: 0,
    lapText: '—',
    tripText: '—',
    maxSpeed: '--',
    observedSec: 0,
    frames: 0,
    drops: 0,
    linkPct: '--'
  },

  onLoad: function () {
    this._offTelemetry = ble.onTelemetry(this._onFrame.bind(this));
    this._offState = ble.onState(this._syncConn.bind(this));
    this._reset();
    this._syncConn();
  },

  onShow: function () {
    this._syncConn();
  },

  onUnload: function () {
    if (this._offTelemetry) { this._offTelemetry(); }
    if (this._offState) { this._offState(); }
  },

  _reset: function () {
    this._startAt = 0;
    this._lastAt = 0;
    this._maxX100 = 0;
    this._frames = 0;
    this._drops = 0;
  },

  _syncConn: function () {
    this.setData({ connected: ble.isConnected() });
  },

  _onFrame: function (t) {
    var now = Date.now();

    if (!this._startAt) {
      this._startAt = now;
    }
    if (this._lastAt && (now - this._lastAt) > GAP_MS) {
      this._drops++;
    }
    this._lastAt = now;
    this._frames++;

    if (t.speedX100 > this._maxX100) {
      this._maxX100 = t.speedX100;
    }

    var sec = (now - this._startAt) / 1000;
    var expected = Math.max(1, Math.round(sec * EXPECTED_HZ));
    var pct = Math.min(100, Math.round((this._frames / expected) * 100));

    this.setData({
      connected: true,
      lapMs: t.lapMs,
      tripM: t.tripM,
      lapText: fmtLap(t.lapMs),
      tripText: fmtTrip(t.tripM),
      maxSpeed: (this._maxX100 / 100).toFixed(1),
      observedSec: Math.round(sec),
      frames: this._frames,
      drops: this._drops,
      linkPct: String(pct)
    });
  },

  resetStats: function () {
    this._reset();
    this.setData({
      maxSpeed: '--', observedSec: 0, frames: 0, drops: 0, linkPct: '--'
    });
  },

  goConnect: function () {
    wx.switchTab({ url: '/pages/connect/connect' });
  }
});
