// Run: NODE_PATH=<dir with jsdom> node test/web/epubkit_split_test.js  (npm i jsdom)
const fs = require('fs');
const { JSDOM } = require('jsdom');
const src = fs.readFileSync(require('path').join(__dirname, '../../web/pages/epubkit.js'), 'utf8');

function extractFn(name) {
  const re = new RegExp(`  (async )?function ${name}\\(`);
  const m = re.exec(src); if (!m) throw new Error('no ' + name);
  let i = src.indexOf('{', m.index), depth = 0;
  for (; i < src.length; i++) { if (src[i] === '{') depth++; else if (src[i] === '}') { depth--; if (depth === 0) break; } }
  return src.slice(m.index, i + 1);
}
const names = ['extension','dirname','basename','normalizeZipPath','decodeHref','resolveZipPath','relativeZipPath','localName','elementsByLocalName','firstByLocalName','parseXml','serializeDocument','manifestInfo','appendOpfElement','makeUniquePath','readEffectiveText'];
const block = src.slice(src.indexOf('// <split-begin>'), src.indexOf('// <split-end>'));
const dom = new JSDOM('<!doctype html><html></html>');
const { DOMParser, XMLSerializer } = dom.window;
const code = `
const encoder = new TextEncoder(); const decoder = new TextDecoder();
const state = {cancelled:false}; const tr = (k,f)=>f;
${names.map(extractFn).join('\n')}
${block}
return {splitLargeChapters, splitXhtml, planSplit};
`;
const mod = new Function('DOMParser', 'XMLSerializer', code)(DOMParser, XMLSerializer);

// planSplit
const plan = mod.planSplit([10,10,10,10,10,10], 25);
console.log('plan', JSON.stringify(plan));
if (plan.length !== 3) throw new Error('plan');
if (mod.planSplit([100], 10).length !== 1) throw new Error('single');

// Build a fake book
const para = i => `<p id="p${i}">${'слово '.repeat(60)}${i}</p>`;
const body = Array.from({length: 40}, (_, i) => i % 10 === 0 ? `<h2 id="h${i}">Section ${i}</h2>` + para(i) : para(i)).join('');
const chapter = `<?xml version="1.0" encoding="utf-8"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>T</title></head><body>${body}<p><a href="#p3">back</a> <a href="ch1.xhtml#p35">self-ext</a></p></body></html>`;
const other = `<?xml version="1.0" encoding="utf-8"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>O</title></head><body><p><a href="ch1.xhtml#h30">to h30</a> <a href="ch1.xhtml">to ch1</a> <a href="ch1.xhtml#nonexistent">x</a></p></body></html>`;
const ncx = `<?xml version="1.0"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/"><navMap><navPoint><content src="text/ch1.xhtml#h20"/></navPoint><navPoint><content src="text/ch2.xhtml"/></navPoint></navMap></ncx>`;
const opf = `<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0"><metadata/><manifest>
<item id="c1" href="text/ch1.xhtml" media-type="application/xhtml+xml"/><item id="c2" href="text/ch2.xhtml" media-type="application/xhtml+xml"/>
<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest><spine toc="ncx"><itemref idref="c1"/><itemref idref="c2"/></spine>
<guide><reference type="text" href="text/ch1.xhtml#h30"/></guide></package>`;
const files = {'OEBPS/text/ch1.xhtml': chapter, 'OEBPS/text/ch2.xhtml': other, 'OEBPS/toc.ncx': ncx.replace('text/ch1','text/ch1'), 'OEBPS/content.opf': opf};
// ncx lives at OEBPS/toc.ncx so src is relative to OEBPS
const zip = { has: p => p in files, readText: async p => files[p] };
(async () => {
  const opfDoc = mod && new DOMParser().parseFromString(opf, 'application/xml');
  const updates = new Map(), removed = new Set(), xhtmlTexts = new Map(), occupied = new Set(Object.keys(files));
  const report = {log: []};
  const added = await mod.splitLargeChapters(zip, updates, removed, opfDoc, 'OEBPS/content.opf', xhtmlTexts, occupied, 4000, report);
  console.log('added parts', added, report.log);
  if (added < 2) throw new Error('expected split');
  const spineIds = Array.from(opfDoc.getElementsByTagName('itemref')).map(e => e.getAttribute('idref'));
  console.log('spine', spineIds.join(','));
  if (spineIds[0] !== 'c1' || !/^c1-part2$/.test(spineIds[1]) || spineIds[spineIds.length - 1] !== 'c2') throw new Error('spine order');
  const manifestHrefs = Array.from(opfDoc.getElementsByTagName('item')).map(e => e.getAttribute('href'));
  console.log(manifestHrefs.join(' '));
  // Every p id must appear exactly once across parts, nothing lost.
  const partPaths = Array.from(updates.keys()).filter(k => /ch1(_part\d+)?\.xhtml$/.test(k));
  let all = [];
  for (const p of partPaths) { const t = updates.get(p).value; all.push(...[...t.matchAll(/ id="(p\d+)"/g)].map(m => m[1])); if (t.length > 20000) throw new Error('part too big ' + t.length); }
  const uniq = new Set(all);
  if (all.length !== 40 || uniq.size !== 40) throw new Error(`ids lost/duplicated: ${all.length}/${uniq.size}`);
  // links remapped
  const o = updates.get('OEBPS/text/ch2.xhtml').value;
  console.log(o.match(/href="[^"]*"/g).join(' '));
  if (/ch1\.xhtml#h30/.test(o)) throw new Error('h30 link not remapped');
  if (!/href="ch1\.xhtml"/.test(o)) throw new Error('plain link must stay');
  if (!/ch1\.xhtml#nonexistent/.test(o)) throw new Error('unknown fragment must stay');
  const n = updates.get('OEBPS/toc.ncx').value; console.log(n.match(/src="[^"]*"/g).join(' '));
  if (/ch1\.xhtml#h20/.test(n)) throw new Error('ncx not remapped');
  const g = opfDoc.getElementsByTagName('reference')[0].getAttribute('href'); console.log('guide', g);
  if (g === 'text/ch1.xhtml#h30') throw new Error('guide not remapped');
  // in-part link: first-part "#p3" stays, "ch1.xhtml#p35" from last part becomes '#p35'
  const last = updates.get(partPaths.sort().pop()).value;
  console.log('last-part links', last.match(/href="[^"]*"/g));
  console.log('ALL OK');
})().catch(e => { console.error('FAIL', e); process.exit(1); });
