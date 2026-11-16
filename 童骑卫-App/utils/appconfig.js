/*  小程序配置
 *
 *  ⚠ 这里有一份 deviceSecret，这是本方案里最不舒服的一处，写在前面：
 *
 *  微信小程序的代码包**不是秘密**。虽然没有真正的"反编译还原源码"，
 *  但解包拿到字符串是常规操作。把设备三元组写进小程序，等于把
 *  这台设备在阿里云上的身份公开了。
 *
 *  本项目的应对是：这份凭据属于一个**只读观察者设备**（viewer），
 *  它在阿里云上只有一条自定义 Topic 的订阅权限，没有任何发布权，
 *  也没有 `thing/service/*` 的订阅权。即使被拿走，最多是"别人也能看见
 *  这辆车的速度"，拿不到控制权——因为云端本来就没有控制通路（§6.6.5）。
 *
 *  正式产品该怎么做：小程序登录 → 自己的业务服务器签发临时凭据。
 *  省赛阶段不搭服务器，这里把边界和代价写清楚，答辩时照着讲。
 */

module.exports = {
  /* ---- 车端 BLE ---- */
  BLE_NAME_PREFIX: 'GuardKart',

  /* ---- 阿里云 IoT ---- */
  PRODUCT_KEY: 'YOUR_PK',
  REGION: 'cn-shanghai',

  /* 观察者设备：只订不发布。
   * 在控制台「设备」里单独添加，名称随意，这里要和它一致。 */
  VIEWER_DEVICE: 'GuardKart-01-viewer',
  VIEWER_SECRET: 'YOUR_VIEWER_DEVICE_SECRET',

  /* WebSocket 路径。阿里云文档示例里是空字符串；
   * 如果连不上（CONNACK 一直不来），先试 '/mqtt'。 */
  WS_PATH: '',

  /* 观察者订阅的自定义 Topic。
   * 阿里云上需要两步配置，缺一步这里就是静默收不到数据：
   *   1) 产品 → Topic 类列表 → 自定义 Topic，为 viewer 加一条
   *      /{pk}/{dn}/user/view ，操作权限选「订阅」
   *   2) 规则引擎 → 数据流转：数据源 /sys/{pk}/{mainDn}/thing/event/property/post
   *      → 目标 MQTT Topic = 上面那条
   * 为什么不能直接订主流：设备的 ACL 不允许订阅别的设备的 thing Topic，
   * 这是平台限制，不是我们偷懒。
   *
   * {pk} / {dn} 会在运行时替换成 PRODUCT_KEY 与 VIEWER_DEVICE。 */
  VIEW_TOPIC: '/{pk}/{dn}/user/view',

  /* 车端主设备名（规则引擎转发时用，同时也用来在 UI 上显示） */
  MAIN_DEVICE: 'GuardKart-01',

  /* 开发期：不校验合法域名。正式版必须关掉（见 README §5）。 */
  DEV_NO_DOMAIN_CHECK: true
};
