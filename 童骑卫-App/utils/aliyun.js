/*  阿里云 IoT 一机一密凭据生成
 *
 *  与 童骑卫-Gateway/src/uplink_wifi.cpp 里的 aliyunClientId/Username/Password
 *  是同一套算法，两边必须算出一模一样的三元组。控制台的
 *  「MQTT 连接参数生成」工具是第三方裁判——对不上先查这里。
 *
 *      clientId = "${deviceName}|securemode=2,signmethod=hmacsha256,timestamp=${ts}|"
 *      username = "${deviceName}&${productKey}"
 *      password = hex(hmac_sha256("clientId"+clientId +
 *                                 "deviceName"+deviceName +
 *                                 "productKey"+productKey +
 *                                 "timestamp"+ts, deviceSecret))
 *
 *  注意 signContent 里用的是 **去掉竖线后缀的裸 clientId**，
 *  而 MQTT 报文里发的是带后缀的完整串。这两者搞混是这一步最经典的错。
 *
 *  securemode：2 = TLS，3 = 裸 TCP。网关走 WiFiClientSecure，小程序走
 *  wss://，两者都是 TLS，所以这里固定 2。
 */

var sha = require('./sha256.js');

var SECURE_MODE_TLS = 2;
var SECURE_MODE_TCP = 3;

function signContent(productKey, deviceName, clientId, ts) {
  return 'clientId' + clientId +
         'deviceName' + deviceName +
         'productKey' + productKey +
         'timestamp' + ts;
}

/*  opts: { productKey, deviceName, deviceSecret, timestamp, secureMode }
 *  timestamp 是秒级 Unix 时间。不给就用本机时间——
 *  手机时钟偏几个小时也能连上（阿里云只验签，不校验时间窗），
 *  但偏差太大时抓包会对不上，所以页面上把它显示出来。 */
function buildCredentials(opts) {
  var pk = opts.productKey;
  var dn = opts.deviceName;
  var ds = opts.deviceSecret;
  var ts = opts.timestamp || Math.floor(Date.now() / 1000);
  var mode = (opts.secureMode === undefined) ? SECURE_MODE_TLS : opts.secureMode;

  var clientId = dn + '|securemode=' + mode +
                 ',signmethod=hmacsha256,timestamp=' + ts + '|';
  var username = dn + '&' + pk;
  var password = sha.hmacSha256Hex(ds, signContent(pk, dn, dn, ts));

  return {
    clientId: clientId,
    username: username,
    password: password,
    timestamp: ts,
    /* 签名串留一份，签名对不上时直接贴进控制台生成器比对 */
    signContent: signContent(pk, dn, dn, ts)
  };
}

function mqttHost(productKey, region) {
  return productKey + '.iot-as-mqtt.' + (region || 'cn-shanghai') + '.aliyuncs.com';
}

/*  小程序只能用 wss（wx.connectSocket 不允许 ws:// 连外网域名）。 */
function mqttUrl(productKey, region, path) {
  return 'wss://' + mqttHost(productKey, region) + ':443' + (path || '');
}

/*  物模型属性上报 Topic —— 与 Gateway 的 cloud.cpp 拼法一致 */
function propertyPostTopic(productKey, deviceName) {
  return '/sys/' + pkOrThrow(productKey) + '/' + deviceName +
         '/thing/event/property/post';
}

function pkOrThrow(pk) {
  if (!pk) {
    throw new Error('productKey 为空');
  }
  return pk;
}

/*  小程序拿到的 Alink 报文本身没有设备名，主题里有。
 *  远程页只订阅一个设备，所以解析主题就够了。 */
function deviceFromTopic(topic) {
  var m = /^\/sys\/([^/]+)\/([^/]+)\//.exec(topic || '');
  return m ? { productKey: m[1], deviceName: m[2] } : null;
}

module.exports = {
  SECURE_MODE_TLS: SECURE_MODE_TLS,
  SECURE_MODE_TCP: SECURE_MODE_TCP,
  buildCredentials: buildCredentials,
  signContent: signContent,
  mqttHost: mqttHost,
  mqttUrl: mqttUrl,
  propertyPostTopic: propertyPostTopic,
  deviceFromTopic: deviceFromTopic
};
