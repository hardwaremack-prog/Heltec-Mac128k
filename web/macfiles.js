// Heltec-Mac: open archive and disk files in the browser and find the Mac disk images inside.
// Handles .zip, .7z (LZMA/LZMA2), StuffIt 1.5-4 ("SIT!", methods 0/1/2/13), BinHex 4 (.hqx),
// MacBinary (.bin), gzip, DiskCopy 4.2 and raw disk images, nested in any combination.
// Pure JavaScript, no libraries; the board page and Node tests both use it.
// Format notes: StuffIt method 13 follows the description in The Unarchiver/XADMaster
// (only its self-describing code tables are supported; preset tables 1-5 are not).
const MacFiles = (() => {
  const td = new TextDecoder('latin1');

  // ------------------------------------------------------------ helpers
  const be16 = (d, o) => (d[o] << 8) | d[o + 1];
  const be32 = (d, o) => ((d[o] << 24) | (d[o + 1] << 16) | (d[o + 2] << 8) | d[o + 3]) >>> 0;
  const le16 = (d, o) => d[o] | (d[o + 1] << 8);
  const le32 = (d, o) => (d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (d[o + 3] << 24)) >>> 0;
  const macRoman = (d, o, n) => td.decode(d.subarray(o, o + n));
  const lower = s => s.toLowerCase();

  async function inflate(data, kind) {   // kind: 'deflate-raw' | 'gzip'
    const ds = new DecompressionStream(kind);
    const out = new Blob([data]).stream().pipeThrough(ds);
    return new Uint8Array(await new Response(out).arrayBuffer());
  }

  // ------------------------------------------------------------ what is a Mac disk?
  // Returns {raw, how} for a disk image (DiskCopy 4.2 unwrapped to raw), else null.
  function asDisk(d, name) {
    if (d.length >= 0x54 && d[0] < 64 && be16(d, 0x52) === 0x0100) {
      const len = be32(d, 0x40);
      if (len && !(len & 511) && 0x54 + len <= d.length) return { raw: d.subarray(0x54, 0x54 + len), how: 'DiskCopy 4.2' };
    }
    const sig = d.length >= 1026 ? be16(d, 1024) : 0;
    if (d.length >= 2048 && (sig === 0xD2D7 || sig === 0x4244 || (d[0] === 0x4C && d[1] === 0x4B)))
      return { raw: d.length & 511 ? d.subarray(0, d.length & ~511) : d, how: 'raw disk' };
    if (/\.(img|dsk|hfv|disk|image|dc42)$/i.test(name) && d.length >= 400 * 1024 && !(d.length % 512))
      return { raw: d, how: 'raw disk' };
    return null;
  }

  function diskInfo(raw) {
    const sig = be16(raw, 1024);
    const fs = sig === 0xD2D7 ? 'MFS' : sig === 0x4244 ? 'HFS' : '';
    let vol = '';
    if (fs) vol = macRoman(raw, 1024 + 0x25, Math.min(raw[1024 + 0x24], 27));
    return { fs, vol, bootable: raw[0] === 0x4C && raw[1] === 0x4B, size: raw.length };
  }

  // ------------------------------------------------------------ ZIP
  async function unzip(d) {
    let e = d.length - 22;
    while (e >= 0 && be32(d, e) !== 0x504B0506) e--;
    if (e < 0) throw new Error('Damaged zip file (no directory)');
    let n = le16(d, e + 10), p = le32(d, e + 16);
    const out = [];
    for (let i = 0; i < n; i++) {
      if (le32(d, p) !== 0x02014B50) break;
      const method = le16(d, p + 10), csize = le32(d, p + 20), usize = le32(d, p + 24);
      const nl = le16(d, p + 28), xl = le16(d, p + 30), cl = le16(d, p + 32), lho = le32(d, p + 42);
      const name = new TextDecoder().decode(d.subarray(p + 46, p + 46 + nl));
      p += 46 + nl + xl + cl;
      if (name.endsWith('/') || name.startsWith('__MACOSX/') || /(^|\/)\._/.test(name)) continue;
      const start = lho + 30 + le16(d, lho + 26) + le16(d, lho + 28);
      const raw = d.subarray(start, start + csize);
      out.push({ name, size: usize, data: async () => method === 0 ? raw : method === 8 ? inflate(raw, 'deflate-raw') :
        Promise.reject(new Error(name + ': unsupported zip compression ' + method)) });
    }
    return out;
  }

  // ------------------------------------------------------------ LZMA / LZMA2 (for 7z)
  function lzmaDecoder(props, dictSize, outSize, out0) {
    // out0: optional preallocated output; returns a decoder object usable for LZMA2 chunks too
    const s = { out: out0 || new Uint8Array(outSize), pos: 0 };
    let lc, lp, pb;
    function setProps(b) { lc = b % 9; b = (b / 9) | 0; lp = b % 5; pb = (b / 5) | 0; }
    setProps(props);
    let probs, rep0, rep1, rep2, rep3, state;
    function resetState() {
      const n = 1846 + (0x300 << (lc + lp));
      probs = new Uint16Array(n).fill(1024);
      rep0 = rep1 = rep2 = rep3 = 0; state = 0;
    }
    resetState();
    // probability array layout
    const IS_MATCH = 0, IS_REP = 192, IS_REP_G0 = 204, IS_REP_G1 = 216, IS_REP_G2 = 228, IS_REP0_LONG = 240,
      POS_SLOT = 432, SPEC_POS = 688, ALIGN = 802, LEN = 818, REP_LEN = 1332, LITERAL = 1846;
    let src, sp, range, code;
    function rcInit(buf, at) { src = buf; sp = at + 1; range = 0xFFFFFFFF; code = 0; for (let i = 0; i < 4; i++) code = ((code << 8) | src[sp++]) >>> 0; }
    function norm() { if (range < 0x1000000) { range = (range << 8) >>> 0; code = ((code << 8) | src[sp++]) >>> 0; } }
    function bit(i) {
      norm();
      const p = probs[i], bound = (range >>> 11) * p;
      if (code < bound) { range = bound; probs[i] = p + ((2048 - p) >>> 5); return 0; }
      range = (range - bound) >>> 0; code = (code - bound) >>> 0; probs[i] = p - (p >>> 5); return 1;
    }
    function direct(n) { let r = 0; while (n--) { norm(); range >>>= 1; let t = (code - range) >>> 31; code = (code - (range & (t - 1))) >>> 0; r = (r << 1) | (1 - t); } return r >>> 0; }
    function tree(base, bits) { let m = 1; for (let i = 0; i < bits; i++) m = (m << 1) | bit(base + m); return m - (1 << bits); }
    function rtree(base, bits) { let m = 1, r = 0; for (let i = 0; i < bits; i++) { const b = bit(base + m); m = (m << 1) | b; r |= b << i; } return r; }
    function len(base, posState) {
      if (!bit(base)) return tree(base + 2 + (posState << 3), 3);
      if (!bit(base + 1)) return 8 + tree(base + 130 + (posState << 3), 3);
      return 16 + tree(base + 258, 8);
    }
    function run(buf, at, unpacked) {   // decode `unpacked` bytes using compressed data at buf[at...]
      rcInit(buf, at);
      const o = s.out, end = s.pos + unpacked, pbMask = (1 << pb) - 1, lpMask = (1 << lp) - 1;
      while (s.pos < end) {
        const posState = s.pos & pbMask;
        if (!bit(IS_MATCH + (state << 4) + posState)) {
          const prev = s.pos ? o[s.pos - 1] : 0;
          const base = LITERAL + 0x300 * (((s.pos & lpMask) << lc) + (prev >> (8 - lc)));
          let sym = 1;
          if (state >= 7) {
            let mb = o[s.pos - rep0 - 1];
            while (sym < 0x100) {
              const m = (mb >> 7) & 1; mb <<= 1;
              const b = bit(base + ((1 + m) << 8) + sym);
              sym = (sym << 1) | b;
              if (m !== b) { while (sym < 0x100) sym = (sym << 1) | bit(base + sym); break; }
            }
          } else while (sym < 0x100) sym = (sym << 1) | bit(base + sym);
          o[s.pos++] = sym & 0xFF;
          state = state < 4 ? 0 : state < 10 ? state - 3 : state - 6;
          continue;
        }
        let l;
        if (bit(IS_REP + state)) {
          if (!bit(IS_REP_G0 + state)) {
            if (!bit(IS_REP0_LONG + (state << 4) + posState)) {
              state = state < 7 ? 9 : 11;
              o[s.pos] = o[s.pos - rep0 - 1]; s.pos++;
              continue;
            }
          } else {
            let d;
            if (!bit(IS_REP_G1 + state)) d = rep1;
            else { if (!bit(IS_REP_G2 + state)) d = rep2; else { d = rep3; rep3 = rep2; } rep2 = rep1; }
            rep1 = rep0; rep0 = d;
          }
          l = len(REP_LEN, posState);
          state = state < 7 ? 8 : 11;
        } else {
          rep3 = rep2; rep2 = rep1; rep1 = rep0;
          l = len(LEN, posState);
          state = state < 7 ? 7 : 10;
          const slot = tree(POS_SLOT + ((l < 3 ? l : 3) << 6), 6);
          if (slot < 4) rep0 = slot;
          else {
            const nb = (slot >> 1) - 1;
            let d = ((2 | (slot & 1)) << nb) >>> 0;
            if (slot < 14) d += rtree(SPEC_POS + d - slot - 1, nb);
            else { d += direct(nb - 4) << 4; d += rtree(ALIGN, 4); }
            rep0 = d >>> 0;
            if (rep0 === 0xFFFFFFFF) break;   // end marker
          }
        }
        l += 2;
        if (rep0 >= s.pos) throw new Error('Damaged LZMA data');
        for (; l > 0 && s.pos < end; l--) { o[s.pos] = o[s.pos - rep0 - 1]; s.pos++; }
      }
      return sp;
    }
    return { run, setProps, resetState, s, get out() { return s.out; } };
  }

  function lzma(packed, props, outSize) {
    const dec = lzmaDecoder(props[0], 0, outSize);
    dec.run(packed, 0, outSize);
    return dec.out;
  }

  function lzma2(packed, outSize) {
    const out = new Uint8Array(outSize);
    let dec = null, p = 0;
    const s = { pos: 0 };
    while (p < packed.length) {
      const c = packed[p++];
      if (c === 0) break;
      if (c === 1 || c === 2) {   // uncompressed chunk
        const n = ((packed[p] << 8) | packed[p + 1]) + 1; p += 2;
        out.set(packed.subarray(p, p + n), dec ? dec.s.pos : s.pos);
        if (dec) dec.s.pos += n; else s.pos += n;
        p += n;
        continue;
      }
      const unpacked = (((c & 0x1F) << 16) | (packed[p] << 8) | packed[p + 1]) + 1;
      const packedLen = ((packed[p + 2] << 8) | packed[p + 3]) + 1;
      p += 4;
      const mode = (c >> 5) & 3;
      if (!dec) { dec = lzmaDecoder(packed[p], 0, outSize, out); dec.s.pos = s.pos; }
      if (mode >= 2) { dec.setProps(packed[p]); p++; }
      if (mode >= 1) dec.resetState();
      dec.run(packed, p, unpacked);
      p += packedLen;
    }
    return out;
  }

  // ------------------------------------------------------------ 7z
  async function un7z(d) {
    if (be32(d, 0) !== 0x377ABCAF || be16(d, 4) !== 0x271C) throw new Error('Not a 7z file');
    const nhOff = 32 + Number(le32(d, 12)) + le32(d, 16) * 4294967296;
    const nhLen = le32(d, 20);
    let buf = d.subarray(nhOff, nhOff + nhLen), p = 0;
    const num = () => {   // 7z variable-length number
      const first = buf[p++]; let mask = 0x80, v = 0;
      for (let i = 0; i < 8; i++) {
        if (!(first & mask)) { const hi = first & (mask - 1); return v + hi * Math.pow(2, 8 * i); }
        v += buf[p++] * Math.pow(2, 8 * i); mask >>= 1;
      }
      return v;
    };
    const bits = (n) => { const r = []; let b = 0, m = 0; for (let i = 0; i < n; i++) { if (!m) { b = buf[p++]; m = 0x80; } r.push(!!(b & m)); m >>= 1; } return r; };
    const allOr = n => { if (buf[p++]) return new Array(n).fill(true); return bits(n); };
    const skipDigests = n => { const def = allOr(n); p += 4 * def.filter(Boolean).length; };

    function readStreams() {
      const st = { packPos: 0, packSizes: [], folders: [], unpackSizes: [], subCounts: [], subSizes: [] };
      for (;;) {
        const id = buf[p++];
        if (id === 0) return st;
        if (id === 6) {   // PackInfo
          st.packPos = num(); const n = num();
          for (;;) { const t = buf[p++]; if (t === 0) break;
            if (t === 9) for (let i = 0; i < n; i++) st.packSizes.push(num());
            else if (t === 10) skipDigests(n);
            else throw new Error('7z: unknown pack info'); }
        } else if (id === 7) {   // UnpackInfo
          if (buf[p++] !== 11) throw new Error('7z: bad folder info');
          const n = num();
          if (buf[p++] !== 0) throw new Error('7z: external folders not supported');
          for (let f = 0; f < n; f++) {
            const nc = num(), coders = [];
            let outTotal = 0;
            for (let c = 0; c < nc; c++) {
              const flag = buf[p++], idLen = flag & 15;
              let method = 0; for (let i = 0; i < idLen; i++) method = method * 256 + buf[p++];
              let nIn = 1, nOut = 1;
              if (flag & 0x10) { nIn = num(); nOut = num(); }
              let props = null;
              if (flag & 0x20) { const pl = num(); props = buf.slice(p, p + pl); p += pl; }
              coders.push({ method, props, nIn, nOut });
              outTotal += nOut;
            }
            for (let b = 0; b < outTotal - 1; b++) { num(); num(); }   // bind pairs (single-coder chains only)
            const packedStreams = coders.reduce((a, c) => a + c.nIn, 0) - (outTotal - 1);
            if (packedStreams > 1) for (let i = 0; i < packedStreams; i++) num();
            st.folders.push({ coders, outTotal });
          }
          if (buf[p++] !== 12) throw new Error('7z: bad unpack sizes');
          for (const f of st.folders) { f.sizes = []; for (let i = 0; i < f.outTotal; i++) f.sizes.push(num()); }
          for (;;) { const t = buf[p++]; if (t === 0) break;
            if (t === 10) skipDigests(st.folders.length);
            else throw new Error('7z: unknown unpack info'); }
        } else if (id === 8) {   // SubStreamsInfo
          st.subCounts = st.folders.map(() => 1);
          let t = buf[p++];
          if (t === 13) { st.subCounts = st.folders.map(() => num()); t = buf[p++]; }
          if (t === 9) {
            st.folders.forEach((f, i) => { let sum = 0; for (let k = 1; k < st.subCounts[i]; k++) { const v = num(); st.subSizes.push(v); sum += v; } st.subSizes.push(f.sizes[f.sizes.length - 1] - sum); });
            t = buf[p++];
          } else st.folders.forEach((f, i) => { if (st.subCounts[i] === 1) st.subSizes.push(f.sizes[f.sizes.length - 1]); });
          while (t !== 0) {
            if (t === 10) skipDigests(st.subCounts.reduce((a, b) => a + b, 0));
            else throw new Error('7z: unknown substream info');
            t = buf[p++];
          }
        } else throw new Error('7z: unexpected header part ' + id);
      }
    }
    function decodeFolder(st, fi, packStart) {
      const f = st.folders[fi];
      if (f.coders.length !== 1) throw new Error('7z: filters/multi-step compression not supported');
      const c = f.coders[0], size = f.sizes[f.sizes.length - 1];
      const packed = d.subarray(packStart, packStart + st.packSizes[fi]);
      if (c.method === 0) return packed;
      if (c.method === 0x030101) return lzma(packed, c.props, size);
      if (c.method === 0x21) return lzma2(packed, size);
      throw new Error('7z: unsupported compression method ' + c.method.toString(16) + ' (only LZMA/LZMA2/stored)');
    }

    // the header itself may be compressed
    let id = buf[p++];
    if (id === 0x17) {
      const st = readStreams();
      const hdr = decodeFolder(st, 0, 32 + st.packPos);
      buf = hdr; p = 0; id = buf[p++];
    }
    if (id !== 1) throw new Error('7z: bad header');
    let st = { folders: [], packSizes: [], subCounts: [], subSizes: [], packPos: 0 };
    const files = [];
    for (;;) {
      const t = buf[p++];
      if (t === 0) break;
      if (t === 2) { while (buf[p++] !== 0) { const sz = num(); p += sz; } }   // archive properties
      else if (t === 3) throw new Error('7z: additional streams not supported');
      else if (t === 4) st = readStreams();
      else if (t === 5) {
        const n = num();
        for (let i = 0; i < n; i++) files.push({ name: '', empty: false, dir: false });
        let empty = new Array(n).fill(false), emptyFile = null;
        for (;;) {
          const prop = buf[p++]; if (prop === 0) break;
          const size = num(), end = p + size;
          if (prop === 14) empty = bits(n);
          else if (prop === 15) emptyFile = bits(empty.filter(Boolean).length);
          else if (prop === 17) {   // kName (0x11): names, UTF-16LE
            p++;   // external flag
            for (let i = 0; i < n; i++) { let s = ''; for (;;) { const ch = le16(buf, p); p += 2; if (!ch) break; s += String.fromCharCode(ch); } files[i].name = s; }
          }
          p = end;
        }
        let ei = 0;
        files.forEach((f, i) => { f.empty = empty[i]; if (f.empty) { f.dir = !(emptyFile && emptyFile[ei]); ei++; } });
      } else throw new Error('7z: unexpected header part ' + t);
    }
    // map files to folders/substreams
    const out = [];
    let fi = 0, sub = 0, si = 0, packStart = 32 + st.packPos;
    const folderStart = [];
    for (let i = 0, a = packStart; i < st.folders.length; i++) { folderStart.push(a); a += st.packSizes[i]; }
    const cache = {};
    let offsetInFolder = 0;
    for (const f of files) {
      if (f.empty) continue;
      while (fi < st.folders.length && sub >= (st.subCounts[fi] ?? 1)) { fi++; sub = 0; offsetInFolder = 0; }
      const size = st.subSizes[si++], folder = fi, off = offsetInFolder;
      offsetInFolder += size; sub++;
      out.push({ name: f.name, size, data: async () => {
        if (!cache[folder]) cache[folder] = decodeFolder(st, folder, folderStart[folder]);
        return cache[folder].subarray(off, off + size);
      } });
    }
    return out;
  }

  // ------------------------------------------------------------ StuffIt
  function rle90(d, outLen) {
    const o = new Uint8Array(outLen); let p = 0, q = 0, last = 0;
    while (p < d.length && q < outLen) {
      const b = d[p++];
      if (b === 0x90) {
        const n = d[p++];
        if (n === 0) { o[q++] = last = 0x90; }
        else for (let i = 1; i < n && q < outLen; i++) o[q++] = last;
      } else o[q++] = last = b;
    }
    return o;
  }

  function lzwSit(d, outLen) {   // StuffIt method 2: Unix compress style, 14-bit, block mode
    const o = new Uint8Array(outLen); let q = 0;
    let bitPos = 0, width = 9, next = 257, prev = -1;
    const pre = new Int32Array(16384), suf = new Uint8Array(16384), stack = new Uint8Array(16384);
    for (let i = 0; i < 256; i++) suf[i] = i;
    let groupStart = 0;
    const read = () => {
      const byte = bitPos >> 3; if (byte + 2 >= d.length + 2) return -1;
      const v = (d[byte] | (d[byte + 1] << 8) | (d[byte + 2] << 16)) >>> (bitPos & 7);
      bitPos += width; return v & ((1 << width) - 1);
    };
    while (q < outLen) {
      const c = read(); if (c < 0) break;
      if (c === 256) {   // clear: skip to the end of the current 8-code group
        const groupBits = width * 8; bitPos = groupStart + Math.ceil((bitPos - groupStart) / groupBits) * groupBits;
        width = 9; next = 257; prev = -1; groupStart = bitPos; continue;
      }
      let sp = 0, cur = c;
      if (c >= next) { if (prev < 0) break; stack[sp++] = 0; cur = prev; }
      while (cur >= 256) { stack[sp++] = suf[cur]; cur = pre[cur]; }
      stack[sp++] = cur;
      const first = cur;
      if (c >= next) stack[0] = first;
      while (sp && q < outLen) o[q++] = stack[--sp];
      if (prev >= 0 && next < 16384) { pre[next] = prev; suf[next] = first; next++; }
      prev = c;
      if (next >= (1 << width) && width < 14) {
        const groupBits = width * 8; bitPos = groupStart + Math.ceil((bitPos - groupStart) / groupBits) * groupBits;
        groupStart = bitPos; width++;
      }
    }
    return o;
  }

  // Method 13: LZSS (64 KB window) with Huffman-coded literals/lengths and offsets.
  const META_CODES = [0x5d8,0x058,0x040,0x0c0,0x000,0x078,0x02b,0x014,0x00c,0x01c,0x01b,0x00b,0x010,0x020,0x038,0x018,
    0x0d8,0xbd8,0x180,0x680,0x380,0xf80,0x780,0x480,0x080,0x280,0x3d8,0xfd8,0x7d8,0x9d8,0x1d8,0x004,0x001,0x002,0x007,0x003,0x008];
  const META_LENS = [11,8,8,8,8,7,6,5,5,5,5,6,5,6,7,7,9,12,10,11,11,12,12,11,11,11,12,12,12,12,12,5,2,2,3,4,5];
  function sit13(d, outLen) {
    let bp = 0;   // bit position, low bit of each byte first
    const bitAt = () => { const v = (d[bp >> 3] >> (bp & 7)) & 1; bp++; return v; };
    const bitsLE = n => { let v = 0; for (let i = 0; i < n; i++) v |= bitAt() << i; return v; };
    // a prefix code as a binary tree: node[i] = [left,right]; leaves are ~symbol
    function codeFromList(list) {   // list of {sym, code, len, lowFirst}
      const t = [[0, 0]];
      for (const e of list) {
        let n = 0;
        for (let i = 0; i < e.len; i++) {
          const b = e.lowFirst ? (e.code >> i) & 1 : (e.code >> (e.len - 1 - i)) & 1;
          if (i === e.len - 1) t[n][b] = ~e.sym;
          else { if (!t[n][b]) { t.push([0, 0]); t[n][b] = t.length - 1; } n = t[n][b]; }
        }
      }
      return t;
    }
    function canonical(lens) {
      const list = []; let code = 0;
      for (let l = 1; l <= 32; l++) { for (let i = 0; i < lens.length; i++) if (lens[i] === l) list.push({ sym: i, code: code++, len: l }); code <<= 1; }
      if (!list.length) throw new Error('StuffIt: empty code table');
      if (list.length === 1) list.push({ sym: list[0].sym, code: 1, len: 1 });
      return codeFromList(list);
    }
    function sym(t) { let n = 0; for (let g = 0; g < 64; g++) { const v = t[n][bitAt()]; if (v < 0) return ~v; if (!v) throw new Error('StuffIt: bad code'); n = v; } throw new Error('StuffIt: bad code'); }
    const meta = codeFromList(META_CODES.map((c, i) => ({ sym: i, code: c, len: META_LENS[i], lowFirst: true })));
    function readLens(n) {
      const lens = new Int32Array(n); let len = 0;
      for (let i = 0; i < n; i++) {
        const v = sym(meta);
        if (v === 31) len = -1;
        else if (v === 32) len++;
        else if (v === 33) len--;
        else if (v === 34) { if (bitAt()) lens[i++] = len; }
        else if (v === 35) { let k = bitsLE(3) + 2; while (k--) lens[i++] = len; }
        else if (v === 36) { let k = bitsLE(6) + 10; while (k--) lens[i++] = len; }
        else len = v + 1;
        lens[i] = len;
      }
      return canonical(lens);
    }
    const first = d[0]; bp = 8;
    if (first >> 4) throw new Error('This StuffIt archive uses a preset table (method 13, set ' + (first >> 4) + ') that Heltec-Mac can\'t unpack yet');
    const c1 = readLens(321), c2 = first & 8 ? c1 : readLens(321), coff = readLens((first & 7) + 10);
    const o = new Uint8Array(outLen); let q = 0, cur = c1;
    while (q < outLen) {
      const v = sym(cur);
      if (v < 256) { o[q++] = v; cur = c1; continue; }
      cur = c2;
      let len;
      if (v < 0x13e) len = v - 0x100 + 3;
      else if (v === 0x13e) len = bitsLE(10) + 65;
      else if (v === 0x13f) len = bitsLE(15) + 65;
      else break;
      const bl = sym(coff);
      const off = bl === 0 ? 1 : bl === 1 ? 2 : (1 << (bl - 1)) + bitsLE(bl - 1) + 1;
      if (off > q) throw new Error('StuffIt: damaged data');
      for (let i = 0; i < len && q < outLen; i++, q++) o[q] = o[q - off];
    }
    return o;
  }

  function crc16(d) { let c = 0; for (let i = 0; i < d.length; i++) { c ^= d[i]; for (let k = 0; k < 8; k++) c = c & 1 ? (c >>> 1) ^ 0xA001 : c >>> 1; } return c; }

  function sitDecode(method, packed, outLen) {
    if (method === 15) {
      const A = typeof SitArsenic !== 'undefined' ? SitArsenic : (typeof require !== 'undefined' ? require('./sit-arsenic.js') : null);
      if (!A) throw new Error('StuffIt "Arsenic" compression needs sit-arsenic.js');
      return A.decode(packed, outLen);
    }
    switch (method & 15) {
      case 0: return packed.subarray(0, outLen);
      case 1: return rle90(packed, outLen);
      case 2: return lzwSit(packed, outLen);
      case 13: return sit13(packed, outLen);
      default: throw new Error('StuffIt compression method ' + (method & 15) + ' isn\'t supported (supported: 0, 1, 2, 13)');
    }
  }

  function unsit(d) {
    if (macRoman(d, 0, 4) !== 'SIT!' && macRoman(d, 10, 4) !== 'rLau') throw new Error('Not a StuffIt 1.5-4 archive (StuffIt 5 and later aren\'t supported)');
    const out = []; let p = 22; const path = [];
    while (p + 112 <= d.length) {
      const h = d.subarray(p, p + 112), rm = h[0], dm = h[1];
      const name = macRoman(h, 3, Math.min(h[2], 63));
      if (rm === 32 || dm === 32) { path.push(name); p += 112; continue; }
      if (rm === 33 || dm === 33) { path.pop(); p += 112; continue; }
      const type = macRoman(h, 66, 4), creator = macRoman(h, 70, 4);
      const rl = be32(h, 84), dl = be32(h, 88), crl = be32(h, 92), cdl = be32(h, 96), dcrc = be16(h, 102);
      const rs = p + 112, ds = rs + crl;
      const full = [...path, name].join('/');
      const rsrcPacked = d.subarray(rs, rs + crl), dataPacked = d.subarray(ds, ds + cdl);
      out.push({ name: full, size: dl, type, creator, rsrcSize: rl, finderFlags: be16(h, 74),
        data: async () => { const r = sitDecode(dm, dataPacked, dl); if (dl && (dm & 15) && crc16(r) !== dcrc) throw new Error(full + ': checksum mismatch, the archive may be damaged'); return r; },
        rsrc: async () => sitDecode(rm, rsrcPacked, rl) });
      p = ds + cdl;
    }
    return out;
  }

  // StuffIt 5 container ("StuffIt (c)1997-..."): same per-fork methods as older StuffIt.
  // Layout follows The Unarchiver/XADMaster's notes; method 15 (Arsenic) isn't supported.
  function unsit5(d) {
    if (macRoman(d, 0, 16) !== 'StuffIt (c)1997-') throw new Error('Not a StuffIt 5 archive');
    const version = d[82], flags = d[83];
    if (version !== 5) throw new Error('Unknown StuffIt 5 archive version ' + version);
    if (flags & 0x80) throw new Error('This StuffIt archive is password-protected');
    let todo = be16(d, 92), p = be32(d, 94);
    const dirs = {}, out = [];
    while (todo-- > 0 && p + 48 <= d.length) {
      const offs = p;
      if (be32(d, p) !== 0xA5A5A5A5) throw new Error('Damaged StuffIt 5 archive');
      const ver = d[p + 4], hsize = be16(d, p + 6), eflags = d[p + 9];
      const dirOffs = be32(d, p + 26), nameLen = be16(d, p + 30);
      const dataLen = be32(d, p + 34), dataComp = be32(d, p + 38), dataCrc = be16(d, p + 42);
      p += 46;
      let dataMethod = 0, nFiles = 0;
      if (eflags & 0x40) {
        nFiles = be16(d, p); p += 2;
        if (dataLen === 0xFFFFFFFF) { todo++; continue; }
      } else {
        dataMethod = d[p]; const passLen = d[p + 1]; p += 2;
        if (passLen) throw new Error('This StuffIt archive is password-protected');
      }
      const name = macRoman(d, p, nameLen); p += nameLen;
      if (p < offs + hsize) { const cl = be16(d, p); p += 4 + cl; }
      const something = be16(d, p); p += 4;
      const type = macRoman(d, p, 4), creator = macRoman(d, p + 4, 4), fflags = be16(d, p + 8); p += 10;
      p += ver === 1 ? 22 : 18;
      let rsrcLen = 0, rsrcComp = 0, rsrcMethod = 0;
      if (something & 1) {
        rsrcLen = be32(d, p); rsrcComp = be32(d, p + 4); rsrcMethod = d[p + 12];   // +8 crc, +10 unknown
        const passLen = d[p + 13]; p += 14;
        if (passLen) throw new Error('This StuffIt archive is password-protected');
      }
      const parent = dirs[dirOffs] || '';
      const full = parent ? parent + '/' + name : name;
      if (eflags & 0x40) { dirs[offs] = full; todo += nFiles; continue; }
      const rsrcPacked = d.subarray(p, p + rsrcComp), dataPacked = d.subarray(p + rsrcComp, p + rsrcComp + dataComp);
      p += rsrcComp + dataComp;
      out.push({ name: full, size: dataLen, type, creator, rsrcSize: rsrcLen, methods: [dataMethod, rsrcMethod], finderFlags: fflags,
        data: async () => { const r = sitDecode(dataMethod, dataPacked, dataLen); if (dataLen && dataMethod !== 15 && (dataMethod & 15) && crc16(r) !== dataCrc) throw new Error(full + ': checksum mismatch, the archive may be damaged'); return r; },
        rsrc: async () => sitDecode(rsrcMethod, rsrcPacked, rsrcLen) });
    }
    return out;
  }

  // ------------------------------------------------------------ BinHex 4.0 and MacBinary
  function unhqx(d) {
    const text = td.decode(d);
    const start = text.indexOf(':', text.indexOf('(This file must be converted with BinHex'));
    if (start < 0) throw new Error('Not a BinHex file');
    const alphabet = '!"#$%&\'()*+,-012345689@ABCDEFGHIJKLMNPQRSTUVXYZ[`abcdefhijklmpqr';
    const bytes = []; let acc = 0, n = 0;
    for (let i = start + 1; i < text.length; i++) {
      const ch = text[i]; if (ch === ':') break;
      const v = alphabet.indexOf(ch); if (v < 0) continue;
      acc = (acc << 6) | v; n += 6;
      if (n >= 8) { n -= 8; bytes.push((acc >> n) & 255); }
    }
    const o = []; let last = 0;   // RLE 0x90
    for (let i = 0; i < bytes.length; i++) {
      const b = bytes[i];
      if (b === 0x90) { const c = bytes[++i]; if (c === 0) o.push(last = 0x90); else for (let k = 1; k < c; k++) o.push(last); }
      else o.push(last = b);
    }
    const r = Uint8Array.from(o), nl = r[0];
    const name = macRoman(r, 1, nl); let p = 2 + nl;
    const type = macRoman(r, p, 4), creator = macRoman(r, p + 4, 4); p += 10;
    const dl = be32(r, p), rl = be32(r, p + 4); p += 10;
    const data = r.subarray(p, p + dl), rsrc = r.subarray(p + dl + 2, p + dl + 2 + rl);
    return [{ name, size: dl, type, creator, rsrcSize: rl, data: async () => data, rsrc: async () => rsrc }];
  }

  function isMacBinary(d) {
    return d.length >= 128 && d[0] === 0 && d[1] > 0 && d[1] < 64 && d[74] === 0 && d[82] === 0 &&
      be32(d, 83) + be32(d, 87) <= d.length && be32(d, 83) < 0x800000 && be32(d, 87) < 0x800000;
  }
  function unmacbinary(d) {
    const name = macRoman(d, 2, d[1]), dl = be32(d, 83), rl = be32(d, 87);
    const data = d.subarray(128, 128 + dl), rs = 128 + ((dl + 127) & ~127), rsrc = d.subarray(rs, rs + rl);
    return [{ name, size: dl, type: macRoman(d, 65, 4), creator: macRoman(d, 69, 4), rsrcSize: rl, data: async () => data, rsrc: async () => rsrc }];
  }

  // ------------------------------------------------------------ write a file onto an MFS (400K) disk
  // Adds a Mac file (data + resource forks, type/creator) to the disk's top level.
  // Returns a new disk image; throws with a plain message if it can't.
  function addToMFS(disk, file) {
    const d = new Uint8Array(disk), V = 1024;
    if (be16(d, V) !== 0xD2D7) throw new Error(be16(d, V) === 0x4244 ? 'Programs can only be added to 400K MFS disks (System 1-3 era), not HFS disks' : 'That disk isn\'t a Mac MFS disk');
    const w16 = (o, v) => { d[o] = v >> 8; d[o + 1] = v; }, w32 = (o, v) => { w16(o, v >>> 16); w16(o + 2, v & 0xFFFF); };
    // allocation map: 12-bit entries for blocks 2..nBlks+1, right after the 64-byte volume info
    const MAP = V + 64;
    const getMap = n => { const i = n - 2, o = MAP + ((i * 3) >> 1); return i & 1 ? ((d[o] & 15) << 8) | d[o + 1] : (d[o] << 4) | (d[o + 1] >> 4); };
    const setMap = (n, v) => { const i = n - 2, o = MAP + ((i * 3) >> 1);
      if (i & 1) { d[o] = (d[o] & 0xF0) | (v >> 8); d[o + 1] = v & 255; } else { d[o] = v >> 4; d[o + 1] = (d[o + 1] & 15) | ((v & 15) << 4); } };
    // the same program added before (same name, type and creator) is replaced, not duplicated
    {
      const dirSt = be16(d, V + 14), dirLen = be16(d, V + 16), nBlks = be16(d, V + 18), want = file.name.slice(0, 63).toLowerCase();
      for (let b = 0; b < dirLen; b++) {
        let p = (dirSt + b) * 512; const end = p + 512;
        while (p < end && d[p] & 0x80) {
          const nl = d[p + 50], len = (51 + nl + 1) & ~1;
          if (macRoman(d, p + 51, nl).toLowerCase() === want && macRoman(d, p + 2, 4) === (file.type || '????') && macRoman(d, p + 6, 4) === (file.creator || '????')) {
            let freed = 0;
            for (const st of [be16(d, p + 22), be16(d, p + 32)])
              for (let k = st, g = 0; k >= 2 && k < nBlks + 2 && g < nBlks; g++) { const nx = getMap(k); setMap(k, 0); freed++; if (nx === 1 || nx === 0) break; k = nx; }
            d.copyWithin(p, p + len, end); d.fill(0, end - len, end);
            w16(V + 12, be16(d, V + 12) - 1); w16(V + 34, be16(d, V + 34) + freed);
            continue;
          }
          p += len;
        }
      }
    }
    const nFiles = be16(d, V + 12), dirSt = be16(d, V + 14), dirLen = be16(d, V + 16);
    const nBlks = be16(d, V + 18), blkSize = be32(d, V + 20), alSt = be16(d, V + 28);
    const nextNum = be32(d, V + 30), free = be16(d, V + 34);
    const need = len => Math.ceil(len / blkSize);
    const nData = need(file.data.length), nRsrc = need(file.rsrc.length);
    if (nData + nRsrc > free) throw new Error('Not enough free space on that disk (' + Math.round(free * blkSize / 1024) + ' KB free, ' + file.name + ' needs ' + Math.round((nData + nRsrc) * blkSize / 1024) + ' KB)');
    // existing names, so we don't duplicate one
    const names = new Set(); let slot = -1;
    for (let b = 0; b < dirLen; b++) {
      let p = (dirSt + b) * 512; const end = p + 512;
      while (p < end && d[p] & 0x80) { const nl = d[p + 50]; names.add(macRoman(d, p + 51, nl).toLowerCase()); p += (51 + nl + 1) & ~1; }
      const entryLen = (51 + Math.min(file.name.length, 63) + 1 + 2) & ~1;
      if (slot < 0 && end - p >= entryLen) slot = p;
    }
    if (slot < 0) throw new Error('That disk\'s file directory is full');
    let name = file.name.slice(0, 63);
    for (let k = 2; names.has(name.toLowerCase()); k++) name = file.name.slice(0, 58) + ' ' + k;
    // claim blocks and copy each fork
    const fork = bytes => {
      const n = need(bytes.length); if (!n) return 0;
      const blocks = [];
      for (let b = 2; b < nBlks + 2 && blocks.length < n; b++) if (getMap(b) === 0) blocks.push(b);
      blocks.forEach((b, i) => { setMap(b, i + 1 < n ? blocks[i + 1] : 1);
        d.set(bytes.subarray(i * blkSize, (i + 1) * blkSize), alSt * 512 + (b - 2) * blkSize); });
      return blocks[0];
    };
    const dStart = fork(file.data), rStart = fork(file.rsrc);
    // directory entry
    const p = slot, now = Math.floor(Date.now() / 1000) + 2082844800;
    d.fill(0, p, p + 51 + name.length + 1);
    d[p] = 0x80; d[p + 1] = 0;
    for (let i = 0; i < 4; i++) { d[p + 2 + i] = (file.type || '????').charCodeAt(i); d[p + 6 + i] = (file.creator || '????').charCodeAt(i); }
    w16(p + 10, (file.finderFlags || 0) & ~0x4103);   // visible, not "inited" (the Finder places the icon), not on the desktop
    w32(p + 18, nextNum);
    w16(p + 22, dStart); w32(p + 24, file.data.length); w32(p + 28, nData * blkSize);
    w16(p + 32, rStart); w32(p + 34, file.rsrc.length); w32(p + 38, nRsrc * blkSize);
    w32(p + 42, now >>> 0); w32(p + 46, now >>> 0);
    d[p + 50] = name.length; for (let i = 0; i < name.length; i++) d[p + 51 + i] = name.charCodeAt(i) & 255;
    w16(V + 12, nFiles + 1); w32(V + 30, nextNum + 1); w16(V + 34, free - nData - nRsrc);
    w32(V + 6, now >>> 0);   // modified
    return { disk: d, name };
  }


  // ------------------------------------------------------------ HFS reader
  // Lists the files on an HFS disk (800K-era). Only needs the catalog B-tree; files whose
  // forks spill into the extents-overflow file (very fragmented ones) are skipped with a note.
  function hfsFiles(raw, notes) {
    const V = 1024;
    if (be16(raw, V) !== 0x4244) return [];
    const blkSize = be32(raw, V + 20), alSt = be16(raw, V + 28) * 512;
    const readExt = (ext, len) => {   // ext: [[start,count] x3]
      const out = new Uint8Array(len); let got = 0;
      for (const [st, n] of ext) {
        if (got >= len || !n) break;
        const o = alSt + st * blkSize, take = Math.min(n * blkSize, len - got);
        if (o + take > raw.length) return null;
        out.set(raw.subarray(o, o + take), got); got += take;
      }
      return got >= len ? out : null;
    };
    const extAt = (o, b = raw) => [0, 4, 8].map(i => [be16(b, o + i), be16(b, o + i + 2)]);
    const cat = readExt(extAt(V + 150), be32(raw, V + 146));
    if (!cat) { notes.push('The disk\'s catalog is damaged or fragmented'); return []; }
    const nodeSize = be16(cat, 14 + 18) || 512;
    const dirs = { 1: '', 2: '' }, recs = [];
    let node = be32(cat, 14 + 10), guard = 0;   // first leaf node, then follow the links
    while (node && guard++ < 10000) {
      const b = node * nodeSize; if (b + nodeSize > cat.length) break;
      const n = be16(cat, b + 10);
      for (let i = 0; i < n; i++) {
        const r = b + be16(cat, b + nodeSize - 2 * (i + 1));
        const kl = cat[r], parent = be32(cat, r + 2), name = macRoman(cat, r + 7, cat[r + 6]);
        let p = r + 1 + kl; if (p & 1) p++;
        const t = cat[p];
        if (t === 1) dirs[be32(cat, p + 6)] = { parent, name };
        else if (t === 2) recs.push({ parent, name, p });
      }
      node = be32(cat, b);
    }
    const pathOf = id => { const parts = []; let g = 0;
      while (dirs[id] && typeof dirs[id] === 'object' && g++ < 64) { parts.unshift(dirs[id].name); id = dirs[id].parent; }
      parts.shift(); return parts; };   // drop the volume name (the root folder)
    const files = [];
    for (const { parent, name, p } of recs) {
      const type = macRoman(cat, p + 4, 4), creator = macRoman(cat, p + 8, 4), finderFlags = be16(cat, p + 12);
      const dl = be32(cat, p + 26), rl = be32(cat, p + 36);
      const data = readExt(extAt(p + 74, cat), dl), rsrc = readExt(extAt(p + 86, cat), rl);
      const path = [...pathOf(parent), name].join('/');
      if (!data || !rsrc) { notes.push(path + ' is too fragmented on the disk to copy'); continue; }
      files.push({ name, path, type, creator, finderFlags, data, rsrc });
    }
    return files;
  }

  // An 800K HFS disk with no System can't be used directly (the 128K ROM can't read HFS, and there's
  // only one drive), so its programs are offered for copying onto the 400K disk in use instead.
  function addDisk(name, disk, result) {
    const info = diskInfo(disk.raw);
    if (info.fs === 'HFS' && !info.bootable) {
      const files = hfsFiles(disk.raw, result.notes).filter(f => !/^(System|Finder|Desktop.*|Clipboard File|Scrapbook File|Note Pad File)$/i.test(f.name) && f.type !== 'ZSYS' && f.type !== 'FNDR');
      if (files.length) {
        result.notes.push(name + ' is an 800K HFS disk without a System, so its contents can be copied onto a 400K disk instead.');
        result.files.push(...files); return;
      }
    }
    result.disks.push({ name, raw: disk.raw, how: disk.how, info });
  }

  // ------------------------------------------------------------ the main entry point
  // Opens a file and everything nested inside it. Returns {disks:[{name, raw, how, info}], other:[names], notes:[]}
  async function open(name, data, depth = 0, result = { disks: [], other: [], notes: [], files: [] }) {
    if (depth > 6) return result;
    const n = lower(name);
    const disk = asDisk(data, name);
    if (disk && !/\.(zip|7z|sit|hqx|bin|gz)$/i.test(n)) { addDisk(name, disk, result); return result; }
    let kids = null;
    try {
      if (data[0] === 0x50 && data[1] === 0x4B && data[2] === 3 && data[3] === 4) kids = await unzip(data);
      else if (be32(data, 0) === 0x377ABCAF) kids = await un7z(data);
      else if (macRoman(data, 0, 4) === 'SIT!' || macRoman(data, 10, 4) === 'rLau') kids = unsit(data);
      else if (data[0] === 0x1F && data[1] === 0x8B) kids = [{ name: name.replace(/\.gz$/i, ''), data: async () => inflate(data, 'gzip') }];
      else if (td.decode(data.subarray(0, Math.min(data.length, 2000))).includes('(This file must be converted with BinHex')) kids = unhqx(data);
      else if (isMacBinary(data)) kids = unmacbinary(data);
      else if (macRoman(data, 0, 16) === 'StuffIt (c)1997-') kids = unsit5(data);
      else if (macRoman(data, 0, 7) === 'StuffIt') throw new Error('StuffIt X (.sitx) archives aren\'t supported. Try a DiskCopy or .img version.');
    } catch (e) { result.notes.push(name + ': ' + e.message); return result; }
    if (!kids) {
      if (disk) addDisk(name, disk, result);
      else result.other.push(name);
      return result;
    }
    for (const k of kids) {
      let kd;
      try { kd = await k.data(); } catch (e) { result.notes.push(k.name + ': ' + e.message); continue; }
      if (k.rsrc && (k.type === 'APPL' || (!kd.length && k.rsrcSize) || (k.type && k.type !== 'dImg' && !asDisk(kd, k.name)))) {
        let rs; try { rs = k.rsrcSize ? await k.rsrc() : new Uint8Array(0); } catch (e) { result.notes.push(k.name + ': ' + e.message); continue; }
        (result.files = result.files || []).push({ name: k.name.split('/').pop(), path: k.name, type: k.type, creator: k.creator,
          finderFlags: k.finderFlags || 0, data: kd, rsrc: rs });
        continue;
      }
      await open(k.name, kd, depth + 1, result);
    }
    return result;
  }

  return { open, diskInfo, addToMFS, hfsFiles, _test: { lzma, lzma2, sit13, unsit, unsit5, unzip, un7z, unhqx, crc16 } };
})();
if (typeof module !== 'undefined') module.exports = MacFiles;
