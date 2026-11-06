// Heltec-Mac: a virtual Apple ImageWriter.
// The Mac prints by sending ImageWriter commands out of its printer port. The board passes the
// bytes to your browser, and this turns them into pages and a PDF, with no libraries.
//
//   const job = ImageWriter.render(bytes)   -> { pages: [{ bits, texts }], width, height }
//   const pdf = await ImageWriter.toPDF(job) -> Uint8Array (a PDF file)
//
// Pages are drawn at 144 dots per inch (the ImageWriter's finest step) on US Letter paper.
// Graphics (Standard and High quality printing) go into a 1-bit bitmap; text (Draft quality)
// is kept as text and set in Courier in the PDF, so it stays sharp and searchable.
(function (root) {
  'use strict';
  const U = 144;                     // units per inch
  const PAGE_W = Math.round(8.5 * U), PAGE_H = 11 * U;
  const ROW = (PAGE_W + 7) >> 3;
  const LEFT = Math.round(0.25 * U); // the print head's column 0 sits about 1/4" in from the paper edge
  // character pitches: ESC letter -> dots per inch for graphics (characters are 8 dots wide)
  const PITCH = { n: 72, N: 80, E: 96, e: 107, q: 120, Q: 136, p: 144, P: 160 };

  function render(d) {
    const pages = [];
    let page = null;
    const newPage = () => { page = { bits: new Uint8Array(ROW * PAGE_H), texts: [], inked: false }; pages.push(page); };
    newPage();
    let x = 0, y = 0;                 // head position in 1/144", relative to the left edge of the print area
    let dpi = 80, lf = 24, dir = 1, formLen = PAGE_H, margin = 0;
    let bold = false, under = false;

    const advanceLine = n => {
      y += n * dir;
      if (y < 0) y = 0;
      while (y >= formLen) { y -= formLen; newPage(); }
    };
    const dot = (px, py, w, h) => {
      for (let yy = py; yy < py + h; yy++) {
        if (yy < 0 || yy >= PAGE_H) continue;
        const r = yy * ROW;
        for (let xx = px; xx < px + w; xx++) if (xx >= 0 && xx < PAGE_W) page.bits[r + (xx >> 3)] |= 0x80 >> (xx & 7);
      }
      page.inked = true;
    };
    const column = byte => {             // one graphics column: 8 pins, LSB = top pin, pins 1/72" apart
      const x0 = LEFT + margin + Math.round(x), x1 = LEFT + margin + Math.round(x + U / dpi);
      if (byte) for (let p = 0; p < 8; p++) if (byte & (1 << p)) dot(x0, y + p * 2, Math.max(1, x1 - x0), 2);
      x += U / dpi;
    };
    const num = (i, n) => { let v = 0; for (let k = 0; k < n; k++) { const c = d[i + k]; if (c >= 48 && c <= 57) v = v * 10 + c - 48; } return v; };
    const putChar = c => {
      const cw = U * 8 / dpi;
      if (c === ' ' && !under) {          // a space only moves the head (and never makes a page "printed")
        const t = page.texts[page.texts.length - 1];
        if (t && t.y === y && Math.abs(t.x + t.s.length * t.cw - (LEFT + margin + x)) < 0.5 && t.cw === cw) t.s += c;
        x += cw; return;
      }
      let t = page.texts[page.texts.length - 1];
      const px = LEFT + margin + x;
      if (!t || t.y !== y || t.bold !== bold || Math.abs(t.x + t.s.length * t.cw - px) > 0.5 || t.cw !== cw)
        page.texts.push(t = { x: px, y, s: '', cw, bold });
      t.s += c;
      if (under) dot(Math.round(px), y + 16, Math.round(cw), 1);
      page.inked = true;
      x += cw;
    };

    for (let i = 0; i < d.length; ) {
      const c = d[i];
      if (c === 0x1B) {
        const k = String.fromCharCode(d[i + 1]);
        i += 2;
        switch (k) {
          case 'G': case 'S': { const n = num(i, 4); i += 4; for (let j = 0; j < n && i < d.length; j++) column(d[i++]); break; }
          case 'g': { const n = num(i, 3) * 8; i += 3; for (let j = 0; j < n && i < d.length; j++) column(d[i++]); break; }
          case 'V': { const n = num(i, 4); const b = d[i + 4]; i += 5; for (let j = 0; j < n; j++) column(b); break; }
          case 'R': { const n = num(i, 3); const ch = d[i + 3]; i += 4; for (let j = 0; j < n; j++) putChar(String.fromCharCode(ch)); break; }
          case 'F': x = num(i, 4) * U / dpi; i += 4; break;
          case 'T': lf = num(i, 2); i += 2; break;
          case 'A': lf = 24; break;
          case 'B': lf = 18; break;
          case 'H': formLen = num(i, 4) || PAGE_H; i += 4; break;
          case 'L': margin = num(i, 3) * U * 8 / dpi; i += 3; break;
          case 'f': dir = 1; break;
          case 'r': dir = -1; break;
          case 'v': y = 0; break;     // set top of form here
          case '!': bold = true; break;
          case '"': bold = false; break;
          case 'X': under = true; break;
          case 'Y': under = false; break;
          case 'c': dpi = 80; lf = 24; dir = 1; formLen = PAGE_H; margin = 0; bold = under = false; break;
          case 'Z': case 'D': i += 2; break;              // DIP-switch settings
          case 'l': case 'a': case 'K': case 's': case 'k': i += 1; break;
          case 'u': case 'U': i += 3; break;
          case '(': case ')': while (i < d.length && d[i] !== 0x2E) i++; i++; break;   // tab lists end with '.'
          default: if (PITCH[k]) dpi = PITCH[k];          // pitch; anything else (o, <, >, ...) needs no action
        }
        continue;
      }
      i++;
      if (c === 0x0D) x = 0;
      else if (c === 0x0A) advanceLine(lf);
      else if (c === 0x0C) { x = 0; y = 0; newPage(); }
      else if (c === 0x08) x = Math.max(0, x - U * 8 / dpi);
      else if (c === 0x1F) { const n = d[i++] - 48; advanceLine(lf * Math.max(1, n)); }   // US n: n line feeds
      else if (c >= 0x20 && c < 0x7F) putChar(String.fromCharCode(c));
      else if (c >= 0x80) putChar('?');
    }
    while (pages.length > 1 && !pages[pages.length - 1].inked) pages.pop();
    return { pages, width: PAGE_W, height: PAGE_H };
  }

  async function deflate(bytes) {
    if (typeof CompressionStream === 'undefined') return null;
    const cs = new CompressionStream('deflate');          // zlib format, what PDF's FlateDecode expects
    const out = new Response(new Blob([bytes]).stream().pipeThrough(cs));
    return new Uint8Array(await out.arrayBuffer());
  }

  async function toPDF(job, title) {
    const enc = new TextEncoder();
    const parts = [], offs = [];
    let len = 0;
    const put = s => { const b = typeof s === 'string' ? enc.encode(s) : s; parts.push(b); len += b.length; };
    const obj = (n, body) => { offs[n] = len; put(n + ' 0 obj\n'); for (const b of [].concat(body)) put(b); put('\nendobj\n'); };
    const esc = s => s.replace(/[\\()]/g, m => '\\' + m);

    const n = job.pages.length;
    // objects: 1 catalog, 2 pages, 3 Courier, 4 Courier-Bold, 5 info; then per page: page, contents, image
    put('%PDF-1.4\n%\xE2\xE3\xCF\xD3\n');
    obj(1, '<< /Type /Catalog /Pages 2 0 R >>');
    const kids = []; for (let p = 0; p < n; p++) kids.push((6 + p * 3) + ' 0 R');
    obj(2, `<< /Type /Pages /Count ${n} /Kids [${kids.join(' ')}] >>`);
    obj(3, '<< /Type /Font /Subtype /Type1 /BaseFont /Courier /Encoding /WinAnsiEncoding >>');
    obj(4, '<< /Type /Font /Subtype /Type1 /BaseFont /Courier-Bold /Encoding /WinAnsiEncoding >>');
    obj(5, `<< /Producer (Heltec-Mac virtual ImageWriter) /Title (${esc(title || 'Mac printout')}) >>`);
    const wPt = job.width / 2, hPt = job.height / 2;     // 144 dpi -> 72 pt per inch
    for (let p = 0; p < n; p++) {
      const pg = job.pages[p], po = 6 + p * 3;
      let content = `q ${wPt} 0 0 ${hPt} 0 0 cm /Im0 Do Q\n`;
      for (const t of pg.texts) {
        const size = t.cw / 2 / 0.6;                      // Courier advance is 0.6 em
        content += `BT /${t.bold ? 'F2' : 'F1'} ${size.toFixed(2)} Tf ${(t.x / 2).toFixed(2)} ${(hPt - (t.y + 14) / 2).toFixed(2)} Td (${esc(t.s)}) Tj ET\n`;
      }
      obj(po, `<< /Type /Page /Parent 2 0 R /MediaBox [0 0 ${wPt} ${hPt}] /Resources << /Font << /F1 3 0 R /F2 4 0 R >> /XObject << /Im0 ${po + 2} 0 R >> >> /Contents ${po + 1} 0 R >>`);
      const cb = enc.encode(content);
      obj(po + 1, [`<< /Length ${cb.length} >>\nstream\n`, cb, '\nendstream']);
      const z = await deflate(pg.bits);
      const data = z || pg.bits;
      obj(po + 2, [`<< /Type /XObject /Subtype /Image /Width ${job.width} /Height ${job.height} /ColorSpace /DeviceGray /BitsPerComponent 1 /Decode [1 0]${z ? ' /Filter /FlateDecode' : ''} /Length ${data.length} >>\nstream\n`, data, '\nendstream']);
    }
    const xref = len, count = 6 + n * 3;
    let x = `xref\n0 ${count}\n0000000000 65535 f \n`;
    for (let i = 1; i < count; i++) x += String(offs[i]).padStart(10, '0') + ' 00000 n \n';
    put(x + `trailer\n<< /Size ${count} /Root 1 0 R /Info 5 0 R >>\nstartxref\n${xref}\n%%EOF\n`);
    const out = new Uint8Array(len); let o = 0;
    for (const b of parts) { out.set(b, o); o += b.length; }
    return out;
  }

  const api = { render, toPDF, PAGE_W, PAGE_H };
  if (typeof module !== 'undefined' && module.exports) module.exports = api; else root.ImageWriter = api;
})(typeof self !== 'undefined' ? self : this);
