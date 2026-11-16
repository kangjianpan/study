/*  BLE 连接的持有者其实是 utils/ble.js 那个单例。
 *  App 只做两件事：攒最近一帧遥测给所有页面共享，以及在被切到后台时
 *  把扫描停掉（iOS 后台继续扫描会被系统掐断，还会耗电）。 */

var ble = require('./utils/ble.js');

App({
  globalData: {
    lastTelemetry: null,
    lastTelemetryAt: 0
  },

  onLaunch: function () {
    var self = this;
    ble.onTelemetry(function (t) {
      self.globalData.lastTelemetry = t;
      self.globalData.lastTelemetryAt = Date.now();
    });
  },

  onHide: function () {
    ble.stopScan();
  },

  onShow: function () {
    /* 从后台回来时不自动重连——BLE 可能已经断了，
     * 让「连接」页自己决定，比悄悄重连更不容易误导用户。 */
  }
});
