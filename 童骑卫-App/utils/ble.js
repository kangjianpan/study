/*  BLE 连接管理（单例）
 *
 *  与 Gateway 的 ble_link.cpp 对应：
 *      FFF0  服务
 *      FFF1  Notify   14 字节遥测，10 Hz
 *      FFF2  Write    [CMD][LEN][PAYLOAD]
 *      FFF3  Read     设备名|固件版本|MAC
 *
 *  页面不要自己调 wx.createBLEConnection，统一走这里，
 *  否则会出现两个页面抢同一个连接、回调互相覆盖的问题。
 *
 *  三个必然踩的坑，代码里都处理了：
 *   1. 微信开发者工具**模拟器不支持蓝牙**，只能真机预览。
 *   2. 不同平台返回的 UUID 大小写不一致（iOS 大写、部分安卓小写），
 *      所以一律 toUpperCase 再比较。
 *   3. 安卓连上之前最好先停扫描，否则部分机型 createBLEConnection 直接失败。
 */

var proto = require('./protocol.js');

var SERVICE_UUID = '0000FFF0-0000-1000-8000-00805F9B34FB';
var TELEM_UUID = '0000FFF1-0000-1000-8000-00805F9B34FB';
var CMD_UUID = '0000FFF2-0000-1000-8000-00805F9B34FB';
var INFO_UUID = '0000FFF3-0000-1000-8000-00805F9B34FB';

var state = {
  adapterReady: false,
  scanning: false,
  connected: false,
  deviceId: '',
  name: '',
  serviceId: '',
  telemId: '',
  cmdId: '',
  infoId: '',
  info: ''
};

var telemListeners = [];
var stateListeners = [];
var foundListener = null;

/* ---- 回调注册 -------------------------------------------------------- */

function onTelemetry(fn) {
  telemListeners.push(fn);
  return function () {
    telemListeners = telemListeners.filter(function (f) { return f !== fn; });
  };
}

function onState(fn) {
  stateListeners.push(fn);
  return function () {
    stateListeners = stateListeners.filter(function (f) { return f !== fn; });
  };
}

function emitState() {
  for (var i = 0; i < stateListeners.length; i++) {
    try { stateListeners[i](state); } catch (e) { /* 单个页面报错不影响别人 */ }
  }
}

/* ---- Promise 化的 wx API --------------------------------------------- */

function wxCall(fn, opts) {
  return new Promise(function (resolve, reject) {
    fn(Object.assign({}, opts, {
      success: resolve,
      fail: function (e) {
        reject(new Error((e && e.errMsg) || 'wx api failed'));
      }
    }));
  });
}

/* ---- 适配器与扫描 ---------------------------------------------------- */

function openAdapter() {
  if (state.adapterReady) {
    return Promise.resolve();
  }
  return wxCall(wx.openBluetoothAdapter, {}).then(function () {
    wx.onBluetoothAdapterStateChange(function (res) {
      if (!res.available) {
        state.adapterReady = false;
        state.connected = false;
        emitState();
      }
    });
    state.adapterReady = true;
    emitState();
  });
}

function startScan(onFound) {
  foundListener = onFound || function () {};

  return openAdapter().then(function () {
    return wxCall(wx.startBluetoothDevicesDiscovery, {
      /* 按服务过滤：周围蓝牙设备很多时，不过滤会让列表刷满电灯泡 */
      services: [SERVICE_UUID],
      allowDuplicatesKey: false
    });
  }).then(function () {
    state.scanning = true;
    emitState();

    wx.onBluetoothDeviceFound(function (res) {
      var list = res.devices || [];
      for (var i = 0; i < list.length; i++) {
        var d = list[i];
        if (!d.name && !d.localName) {
          continue;
        }
        foundListener({
          deviceId: d.deviceId,
          name: d.name || d.localName || '(未命名)',
          rssi: d.RSSI
        });
      }
    });
  });
}

function stopScan() {
  if (!state.scanning) {
    return Promise.resolve();
  }
  state.scanning = false;
  emitState();
  return wxCall(wx.stopBluetoothDevicesDiscovery, {}).catch(function () {});
}

/* ---- 连接 ------------------------------------------------------------ */

function connect(deviceId, name) {
  return openAdapter()
    .then(function () { return stopScan(); })   /* 安卓不先停扫描会连不上 */
    .then(function () {
      return wxCall(wx.createBLEConnection, { deviceId: deviceId, timeout: 10000 });
    })
    .then(function () { return wxCall(wx.getBLEDeviceServices, { deviceId: deviceId }); })
    .then(function (res) {
      var svc = null;
      for (var i = 0; i < res.services.length; i++) {
        if (res.services[i].uuid.toUpperCase() === SERVICE_UUID) {
          svc = res.services[i];
          break;
        }
      }
      if (!svc) {
        throw new Error('没找到 FFF0 服务——这多半不是童骑卫的板子');
      }
      state.serviceId = svc.uuid;
      return wxCall(wx.getBLEDeviceCharacteristics, {
        deviceId: deviceId,
        serviceId: svc.uuid
      });
    })
    .then(function (res) {
      var chars = res.characteristics || [];
      for (var i = 0; i < chars.length; i++) {
        var u = chars[i].uuid.toUpperCase();
        if (u === TELEM_UUID) { state.telemId = chars[i].uuid; }
        if (u === CMD_UUID) { state.cmdId = chars[i].uuid; }
        if (u === INFO_UUID) { state.infoId = chars[i].uuid; }
      }
      if (!state.telemId || !state.cmdId) {
        throw new Error('FFF1/FFF2 特征缺失，固件版本可能不对');
      }
      return wxCall(wx.notifyBLECharacteristicValueChange, {
        state: true,
        deviceId: deviceId,
        serviceId: state.serviceId,
        characteristicId: state.telemId
      });
    })
    .then(function () {
      state.connected = true;
      state.deviceId = deviceId;
      state.name = name || deviceId;
      emitState();

      /* 连接可能不是我们主动断的（走远了、板子重启），这里兜底 */
      wx.onBLEConnectionStateChange(function (res) {
        if (res.deviceId === state.deviceId && !res.connected) {
          state.connected = false;
          state.deviceId = '';
          emitState();
        }
      });

      /* 先把回调装好再读 FFF3，否则读回来的值可能早于回调注册被丢掉 */
      wx.onBLECharacteristicValueChange(function (res) {
        if (res.deviceId !== state.deviceId) {
          return;
        }
        var uuid = res.characteristicId.toUpperCase();

        if (uuid === TELEM_UUID) {
          var t = proto.decodeTelemetry(res.value);
          if (!t.valid) {
            return;   /* 不是 14 字节的东西不往上送，页面只认一种形状 */
          }
          for (var i = 0; i < telemListeners.length; i++) {
            try { telemListeners[i](t); } catch (e) { /* ignore */ }
          }
          return;
        }

        if (uuid === INFO_UUID) {
          state.info = ascii(res.value);
          emitState();
        }
      });

      readInfo();
    });
}

/*  FFF3 的内容是纯 ASCII（设备名|fw=…|mac=…），
 *  用 charCode 直接拼比 TextDecoder 稳。 */
function ascii(buf) {
  var b = new Uint8Array(buf);
  var s = '';
  for (var i = 0; i < b.length; i++) {
    s += String.fromCharCode(b[i]);
  }
  return s;
}

function readInfo() {
  if (!state.infoId) {
    return;
  }
  wx.readBLECharacteristicValue({
    deviceId: state.deviceId,
    serviceId: state.serviceId,
    characteristicId: state.infoId,
    fail: function () { /* 不致命，只是看不到固件版本 */ }
  });
}

function disconnect() {
  var id = state.deviceId;
  state.connected = false;
  state.deviceId = '';
  emitState();
  if (id) {
    wxCall(wx.closeBLEConnection, { deviceId: id }).catch(function () {});
  }
  return Promise.resolve();
}

/* ---- 发命令 ---------------------------------------------------------- */

function send(cmd, payload) {
  if (!state.connected) {
    return Promise.reject(new Error('未连接'));
  }
  var ab;
  try {
    ab = proto.buildCommand(cmd, payload);
  } catch (e) {
    return Promise.reject(e);
  }
  return wxCall(wx.writeBLECharacteristicValue, {
    deviceId: state.deviceId,
    serviceId: state.serviceId,
    characteristicId: state.cmdId,
    value: ab
  });
}

function isConnected() { return state.connected; }
function getState() { return state; }

module.exports = {
  SERVICE_UUID: SERVICE_UUID,
  TELEM_UUID: TELEM_UUID,
  CMD_UUID: CMD_UUID,
  INFO_UUID: INFO_UUID,
  onTelemetry: onTelemetry,
  onState: onState,
  openAdapter: openAdapter,
  startScan: startScan,
  stopScan: stopScan,
  connect: connect,
  disconnect: disconnect,
  send: send,
  isConnected: isConnected,
  getState: getState
};
