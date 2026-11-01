// StuffIt "Arsenic" (method 15) decompressor for Heltec-Mac.
//
// This file is a JavaScript port of XADStuffItArsenicHandle.m and parts of BWT.c from
// XADMaster (The Unarchiver), Copyright (c) 2017-present MacPaw Way Ltd., and is
// distributed under the GNU Lesser General Public License, version 2.1 or later.
// See https://github.com/MacPaw/XADMaster and licenses/XADMaster-LGPL-2.1.txt.
//
// Arsenic: adaptive arithmetic coding -> zero-run/move-to-front -> inverse BWT -> RLE.
const SitArsenic = (() => {
  const RAND = [
    238,86,248,195,157,159,174,44,173,205,36,157,166,257,24,185,
    161,130,117,233,159,85,102,106,134,113,220,132,86,150,86,161,
    132,120,183,50,106,3,227,2,17,257,8,68,131,256,67,227,
    28,240,134,106,107,15,3,45,134,23,123,16,246,128,120,122,
    161,225,239,140,246,135,75,167,226,119,250,184,129,238,119,192,
    157,41,32,39,113,18,224,107,209,124,10,137,125,135,196,257,
    193,49,175,56,3,104,27,118,121,63,219,199,27,54,123,226,
    99,129,238,12,99,139,120,56,151,155,215,143,221,242,163,119,
    140,195,57,32,179,18,17,14,23,66,128,44,196,146,89,200,
    219,64,118,100,180,85,26,158,254,95,6,60,65,239,212,170,
    152,41,205,31,2,168,135,210,160,147,152,239,12,67,237,157,
    194,235,129,233,100,35,104,30,37,87,222,154,207,127,229,186,
    65,234,234,54,26,40,121,32,94,24,78,124,142,88,122,239,
    145,2,147,187,86,161,73,27,121,146,243,88,79,82,156,2,
    119,175,42,143,73,208,153,77,152,257,96,147,256,117,49,206,
    73,32,86,87,226,245,38,43,138,191,222,208,131,52,244,23
  ];
  const NUMBITS = 26, ONE = 1 << (NUMBITS - 1), HALF = 1 << (NUMBITS - 2);

  function model(first, last, increment, limit) {
    const m = { increment, limit, n: last - first + 1, sym: [], freq: [], total: 0 };
    for (let i = 0; i < m.n; i++) m.sym.push(i + first);
    reset(m);
    return m;
  }
  function reset(m) { m.total = m.increment * m.n; m.freq = new Array(m.n).fill(m.increment); }
  function bump(m, i) {
    m.freq[i] += m.increment; m.total += m.increment;
    if (m.total > m.limit) { m.total = 0; for (let k = 0; k < m.n; k++) { m.freq[k] = (m.freq[k] + 1) >> 1; m.total += m.freq[k]; } }
  }

  function decode(src, outLen) {
    let bp = 0;   // bits, most significant first
    const bit = () => { const b = bp >> 3; const v = b < src.length ? (src[b] >> (7 - (bp & 7))) & 1 : 0; bp++; return v; };
    let range = ONE, code = 0;
    for (let i = 0; i < NUMBITS; i++) code = (code * 2) + bit();
    function sym(m) {
      const r = Math.floor(range / m.total), f = Math.floor(code / r);
      let cum = 0, n;
      for (n = 0; n < m.n - 1; n++) { if (cum + m.freq[n] > f) break; cum += m.freq[n]; }
      const low = r * cum;
      code -= low;
      if (cum + m.freq[n] === m.total) range -= low; else range = m.freq[n] * r;
      while (range <= HALF) { range *= 2; code = code * 2 + bit(); }
      bump(m, n);
      return m.sym[n];
    }
    const bits = (m, k) => { let r = 0; for (let i = 0; i < k; i++) if (sym(m)) r |= 1 << i; return r >>> 0; };

    const initial = model(0, 1, 1, 256), selector = model(0, 10, 8, 1024);
    const mtfm = [model(2, 3, 8, 1024), model(4, 7, 4, 1024), model(8, 15, 4, 1024), model(16, 31, 4, 1024),
                  model(32, 63, 2, 1024), model(64, 127, 2, 1024), model(128, 255, 1, 1024)];
    if (bits(initial, 8) !== 0x41 || bits(initial, 8) !== 0x73) throw new Error('Damaged StuffIt (Arsenic) data');
    const blockBits = bits(initial, 4) + 9, blockSize = 1 << blockBits;
    let endOfBlocks = sym(initial);
    const out = new Uint8Array(outLen);
    let q = 0;
    const block = new Uint8Array(blockSize), table = new Uint8Array(256);
    let transform = new Uint32Array(blockSize);
    while (!endOfBlocks && q < outLen) {
      // ---- read one block
      for (let i = 0; i < 256; i++) table[i] = i;
      const mtf = s => { const r = table[s]; for (let i = s; i > 0; i--) table[i] = table[i - 1]; table[0] = r; return r; };
      const randomized = sym(initial);
      let index = bits(initial, blockBits), n = 0;
      for (;;) {
        let sel = sym(selector);
        if (sel === 0 || sel === 1) {
          let state = 1, zeros = 0;
          while (sel < 2) { zeros += sel === 0 ? state : 2 * state; state *= 2; sel = sym(selector); }
          if (n + zeros > blockSize) throw new Error('Damaged StuffIt (Arsenic) data');
          block.fill(mtf(0), n, n + zeros); n += zeros;
        }
        if (sel === 10) break;
        const s = sel === 2 ? 1 : sym(mtfm[sel - 3]);
        if (n >= blockSize) throw new Error('Damaged StuffIt (Arsenic) data');
        block[n++] = mtf(s);
      }
      if (index >= n) throw new Error('Damaged StuffIt (Arsenic) data');
      reset(selector); for (const m of mtfm) reset(m);
      if (sym(initial)) { bits(initial, 32); endOfBlocks = true; }   // end marker + CRC (not checked)
      // ---- inverse BWT
      const counts = new Int32Array(256), cum = new Int32Array(256);
      for (let i = 0; i < n; i++) counts[block[i]]++;
      for (let i = 0, t = 0; i < 256; i++) { cum[i] = t; t += counts[i]; counts[i] = 0; }
      for (let i = 0; i < n; i++) { transform[cum[block[i]] + counts[block[i]]] = i; counts[block[i]]++; }
      // ---- output with de-randomisation and run-length expansion
      let count = 0, last = 0, ri = 0, rc = RAND[0];
      for (let bc = 0; bc < n && q < outLen; ) {
        index = transform[index];
        let byte = block[index];
        if (randomized && rc === bc) { byte ^= 1; ri = (ri + 1) & 255; rc += RAND[ri]; }
        bc++;
        if (count === 4) {
          count = 0;
          for (let k = 0; k < byte && q < outLen; k++) out[q++] = last;
        } else {
          if (byte === last) count++; else { count = 1; last = byte; }
          out[q++] = byte;
        }
      }
    }
    return out;
  }
  return { decode };
})();
if (typeof module !== 'undefined') module.exports = SitArsenic;
