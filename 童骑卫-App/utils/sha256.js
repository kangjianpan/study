/*  SHA-256 + HMAC-SHA256（纯 JS）
 *
 *  为什么要自己写：小程序没有 Node 的 crypto，也没有稳定可用的
 *  SubtleCrypto（wx 只在部分基础库、部分环境暴露它）。阿里云一机一密
 *  的 password 是 HMAC-SHA256 的十六进制串，缺了它连不上云。
 *
 *  这份实现只处理字节数组，不做流式，够用且好对拍。
 *  正确性由 tools/test_node.js 对着 Node 的 crypto 跑随机向量验证。
 */

var K = [
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
];

function rotr(x, n) {
  return ((x >>> n) | (x << (32 - n))) >>> 0;
}

/*  bytes: Uint8Array -> Uint8Array(32) */
function sha256(bytes) {
  var msg = bytes;
  var len = msg.length;
  var bitLenHi = Math.floor((len * 8) / 4294967296);
  var bitLenLo = (len * 8) >>> 0;

  /* 0x80 + 至少 8 字节长度，补齐到 64 的倍数 */
  var paddedLen = (((len + 9) + 63) >> 6) << 6;
  var padded = new Uint8Array(paddedLen);
  padded.set(msg);
  padded[len] = 0x80;

  var dv = new DataView(padded.buffer);
  dv.setUint32(paddedLen - 8, bitLenHi, false);
  dv.setUint32(paddedLen - 4, bitLenLo, false);

  var h0 = 0x6a09e667, h1 = 0xbb67ae85, h2 = 0x3c6ef372, h3 = 0xa54ff53a;
  var h4 = 0x510e527f, h5 = 0x9b05688c, h6 = 0x1f83d9ab, h7 = 0x5be0cd19;

  var w = new Uint32Array(64);

  for (var off = 0; off < paddedLen; off += 64) {
    for (var t = 0; t < 16; t++) {
      w[t] = dv.getUint32(off + t * 4, false);
    }
    for (t = 16; t < 64; t++) {
      var s0 = rotr(w[t - 15], 7) ^ rotr(w[t - 15], 18) ^ (w[t - 15] >>> 3);
      var s1 = rotr(w[t - 2], 17) ^ rotr(w[t - 2], 19) ^ (w[t - 2] >>> 10);
      w[t] = (w[t - 16] + s0 + w[t - 7] + s1) >>> 0;
    }

    var a = h0, b = h1, c = h2, d = h3, e = h4, f = h5, g = h6, h = h7;

    for (t = 0; t < 64; t++) {
      var S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      var ch = (e & f) ^ (~e & g);
      var t1 = (h + S1 + ch + K[t] + w[t]) >>> 0;
      var S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      var maj = (a & b) ^ (a & c) ^ (b & c);
      var t2 = (S0 + maj) >>> 0;

      h = g; g = f; f = e;
      e = (d + t1) >>> 0;
      d = c; c = b; b = a;
      a = (t1 + t2) >>> 0;
    }

    h0 = (h0 + a) >>> 0; h1 = (h1 + b) >>> 0; h2 = (h2 + c) >>> 0; h3 = (h3 + d) >>> 0;
    h4 = (h4 + e) >>> 0; h5 = (h5 + f) >>> 0; h6 = (h6 + g) >>> 0; h7 = (h7 + h) >>> 0;
  }

  var out = new Uint8Array(32);
  var odv = new DataView(out.buffer);
  odv.setUint32(0, h0, false); odv.setUint32(4, h1, false);
  odv.setUint32(8, h2, false); odv.setUint32(12, h3, false);
  odv.setUint32(16, h4, false); odv.setUint32(20, h5, false);
  odv.setUint32(24, h6, false); odv.setUint32(28, h7, false);
  return out;
}

function hmacSha256(key, msg) {
  var BLOCK = 64;

  var k = key;
  if (k.length > BLOCK) {
    k = sha256(k);
  }

  var pad = new Uint8Array(BLOCK);
  pad.set(k);

  var ipad = new Uint8Array(BLOCK + msg.length);
  var opad = new Uint8Array(BLOCK + 32);
  for (var i = 0; i < BLOCK; i++) {
    ipad[i] = pad[i] ^ 0x36;
    opad[i] = pad[i] ^ 0x5c;
  }
  ipad.set(msg, BLOCK);

  var inner = sha256(ipad);
  opad.set(inner, BLOCK);
  return sha256(opad);
}

/*  UTF-8 编码。小程序的 TextEncoder 不可靠，自己写 30 行更省心。 */
function utf8Bytes(str) {
  var out = [];
  for (var i = 0; i < str.length; i++) {
    var c = str.charCodeAt(i);
    if (c < 0x80) {
      out.push(c);
    } else if (c < 0x800) {
      out.push(0xc0 | (c >> 6), 0x80 | (c & 0x3f));
    } else if (c >= 0xd800 && c <= 0xdbff) {
      /* 代理对 */
      var c2 = str.charCodeAt(++i);
      var cp = 0x10000 + ((c - 0xd800) << 10) + (c2 - 0xdc00);
      out.push(0xf0 | (cp >> 18), 0x80 | ((cp >> 12) & 0x3f),
               0x80 | ((cp >> 6) & 0x3f), 0x80 | (cp & 0x3f));
    } else {
      out.push(0xe0 | (c >> 12), 0x80 | ((c >> 6) & 0x3f), 0x80 | (c & 0x3f));
    }
  }
  return new Uint8Array(out);
}

function toHex(bytes) {
  var s = '';
  for (var i = 0; i < bytes.length; i++) {
    s += ('0' + (bytes[i] & 0xff).toString(16)).slice(-2);
  }
  return s;
}

function sha256Hex(str) {
  return toHex(sha256(utf8Bytes(str)));
}

function hmacSha256Hex(keyStr, msgStr) {
  return toHex(hmacSha256(utf8Bytes(keyStr), utf8Bytes(msgStr)));
}

module.exports = {
  sha256: sha256,
  hmacSha256: hmacSha256,
  sha256Hex: sha256Hex,
  hmacSha256Hex: hmacSha256Hex,
  utf8Bytes: utf8Bytes,
  toHex: toHex
};
