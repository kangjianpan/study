var ble = require('../../utils/ble.js');
var proto = require('../../utils/protocol.js');

var CMD = proto.CMD;

Page({
  data: {
    connected: false,
    limit: 12,
    limitMin: proto.LIMIT_MIN_KMH,
    limitMax: proto.LIMIT_MAX_KMH,
    effective: '--',
    stateName: '--',
    busy: false
  },

  onLoad: function () {
    this._offTelemetry = ble.onTelemetry(this._onFrame.bind(this));
    this._offState = ble.onState(this._syncConn.bind(this));
    this._syncConn();
  },

  onShow: function () {
    this._syncConn();
    /* 切回来时把滑杆对齐到车上真实的限速，
     * 否则页面上显示的和车上跑的不是一回事 */
    var t = getApp().globalData.lastTelemetry;
    if (t && t.valid) {
      this.setData({ limit: t.limit, effective: String(t.limit) });
    }
  },

  onUnload: function () {
    if (this._offTelemetry) { this._offTelemetry(); }
    if (this._offState) { this._offState(); }
  },

  _syncConn: function () {
    this.setData({ connected: ble.isConnected() });
  },

  _onFrame: function (t) {
    /* 滑杆只在用户拖动时变；这里只更新"车上实际生效"的那个数。
     * 两者不一致说明命令丢了或还没到，正好让用户看见。 */
    this.setData({
      effective: String(t.limit),
      stateName: t.stateName
    });
  },

  onLimitChanging: function (e) {
    this.setData({ limit: e.detail.value });
  },

  onLimitChange: function (e) {
    var self = this;
    var v = e.detail.value;
    this.setData({ limit: v });

    this._send(CMD.PARENT_LIMIT, [v], '家长限速已下发 ' + v + ' km/h')
      .then(function (ok) {
        if (ok) {
          return;
        }
        /* 失败了把滑杆拉回车上的真实值，避免显示一个从没生效过的数字 */
        var t = getApp().globalData.lastTelemetry;
        if (t && t.valid) {
          self.setData({ limit: t.limit });
        }
      });
  },

  onVenueLimit: function () {
    /* 场地限速是「场地经营者」那个角色，演示时用和家长限速同一个值下发，
     * 让 STM32 侧的限速仲裁有一条独立输入可取最小值。 */
    this._send(CMD.VENUE_LIMIT, [this.data.limit], '场地限速已下发');
  },

  onStop: function () {
    var self = this;
    wx.showModal({
      title: '请求停车',
      content: '车辆会减速到停。物理急停按钮仍然是第一手段，这不是替代品。',
      confirmText: '确认停车',
      confirmColor: '#c62828',
      success: function (res) {
        if (res.confirm) {
          self._send(CMD.STOP, [], '停车请求已发送');
        }
      }
    });
  },

  onResume: function () {
    this._send(CMD.RESUME, [], '已请求解除停车');
  },

  /*  返回 Promise<boolean>，**不 reject**。
   *  命令发出去只要写进了 BLE 就算成功，协议里没有 ACK——
   *  真正的"有没有生效"要看下一帧遥测里的 limit 变没变，
   *  所以这里不承诺"已生效"，只说"已下发"。 */
  _send: function (cmd, payload, okText) {
    var self = this;
    if (this.data.busy) {
      return Promise.resolve(false);
    }
    this.setData({ busy: true });

    return ble.send(cmd, payload)
      .then(function () {
        wx.showToast({ title: okText, icon: 'none' });
        return true;
      })
      .catch(function (e) {
        wx.showToast({ title: e.message || '发送失败', icon: 'none' });
        return false;
      })
      .then(function (ok) {
        self.setData({ busy: false });
        return ok;
      });
  }
});
