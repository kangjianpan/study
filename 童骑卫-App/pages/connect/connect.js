var ble = require('../../utils/ble.js');

Page({
  data: {
    scanning: false,
    connected: false,
    connecting: false,
    devices: [],
    deviceName: '',
    info: '',
    error: ''
  },

  onLoad: function () {
    this._offState = ble.onState(this._sync.bind(this));
    this._sync();
  },

  onShow: function () {
    this._sync();
  },

  onUnload: function () {
    if (this._offState) { this._offState(); }
    ble.stopScan();
  },

  onHide: function () {
    ble.stopScan();
    this.setData({ scanning: false });
  },

  _sync: function () {
    var s = ble.getState();
    this.setData({
      connected: s.connected,
      deviceName: s.name,
      info: s.info
    });
    if (s.connected) {
      this.setData({ scanning: false, devices: [] });
    }
  },

  toggleScan: function () {
    if (this.data.scanning) {
      this._stopScan();
      return;
    }

    var self = this;
    this.setData({ devices: [], error: '', scanning: true });

    ble.startScan(function (d) {
      var list = self.data.devices.slice();
      for (var i = 0; i < list.length; i++) {
        if (list[i].deviceId === d.deviceId) {
          return;
        }
      }
      list.push(d);
      /* 强的排前面：现场三辆车同时在场时，先看到的是最近的那台 */
      list.sort(function (a, b) { return (b.rssi || -127) - (a.rssi || -127); });
      self.setData({ devices: list });
    }).catch(function (e) {
      self.setData({
        scanning: false,
        error: self._explain(e)
      });
    });
  },

  _stopScan: function () {
    var self = this;
    ble.stopScan().then(function () {
      self.setData({ scanning: false });
    });
  },

  tapDevice: function (e) {
    var d = e.currentTarget.dataset;
    var self = this;

    if (this.data.connecting) {
      return;
    }
    this.setData({ connecting: true, error: '' });

    ble.connect(d.id, d.name)
      .then(function () {
        self.setData({ connecting: false, connected: true });
        wx.showToast({ title: '已连接', icon: 'success' });
      })
      .catch(function (err) {
        self.setData({
          connecting: false,
          error: self._explain(err)
        });
        /* 连失败了把扫描恢复起来，省得用户再点一次 */
        ble.stopScan();
      });
  },

  disconnect: function () {
    ble.disconnect();
    this.setData({ connected: false, devices: [] });
  },

  goDashboard: function () {
    wx.switchTab({ url: '/pages/dashboard/dashboard' });
  },

  _explain: function (e) {
    var msg = (e && e.message) || (e && e.errMsg) || String(e);

    if (msg.indexOf('10001') >= 0 || msg.indexOf('not available') >= 0) {
      return '蓝牙没打开。请在系统设置里打开蓝牙，安卓还要允许微信使用「附近设备」。';
    }
    if (msg.indexOf('10012') >= 0 || msg.indexOf('timeout') >= 0) {
      return '连接超时。靠近车一点再试；如果板子刚重启，等 2 秒。';
    }
    if (msg.indexOf('10006') >= 0) {
      return '连接已断开，重新扫描一次。';
    }
    if (msg.indexOf('FFF0') >= 0) {
      return msg;
    }
    return msg;
  }
});
