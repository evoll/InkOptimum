// Run: NODE_PATH=<dir with jsdom> node test/web/opfedit_test.js
// Tests the EPUB metadata/cover helpers used by the Files page "About book" editor.
const fs = require('fs');
const { JSDOM } = require('jsdom');
const src = fs.readFileSync(require('path').join(__dirname, '../../web/pages/files.js'), 'utf8');
const block = src.slice(src.indexOf('// <opfedit-begin>'), src.indexOf('// <opfedit-end>'));
const dom = new JSDOM('<!doctype html><html></html>');
const { DOMParser, XMLSerializer } = dom.window;
const api = new Function('DOMParser', 'XMLSerializer', block +
  '\nreturn {opfParse,opfSerialize,opfReadMetadata,opfApplyMetadata,opfEnsureCover,opfDiffFields,opfIsbnOf};')(DOMParser, XMLSerializer);
const assert = (c, m) => { if (!c) { console.error('FAIL', m); process.exit(1); } };

const opf = `<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id">
<metadata xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:opf="http://www.idpf.org/2007/opf">
<dc:title>Old</dc:title><dc:creator opf:file-as="A, B">A B</dc:creator><dc:language>en</dc:language>
<dc:identifier id="id">urn:uuid:123</dc:identifier></metadata>
<manifest><item id="c1" href="t/ch1.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="c1"/></spine></package>`;

let doc = api.opfParse(opf);
let meta = api.opfReadMetadata(doc);
assert(meta.title === 'Old' && meta.authors[0] === 'A B' && meta.isbn === '', 'read');

const form = { ...meta, title: 'New Title', authors: ['X Y', 'Z'], isbn: '9780306406157', series: 'Saga', seriesIndex: '2', description: 'Hello' };
const changed = api.opfDiffFields(meta, form);
assert(!('language' in changed) && changed.title === 'New Title' && changed.authors.length === 2, 'diff');
api.opfApplyMetadata(doc, changed);
const out = api.opfSerialize(doc);
const re = api.opfReadMetadata(api.opfParse(out));
assert(re.title === 'New Title' && re.authors.join('|') === 'X Y|Z', 'title/authors');
assert(re.isbn === '9780306406157' && re.series === 'Saga' && re.seriesIndex === '2' && re.description === 'Hello', 'isbn/series/desc');
assert(re.language === 'en', 'untouched field kept');
assert(/urn:uuid:123/.test(out), 'uuid identifier kept');
assert(out.startsWith('<?xml'), 'xml decl');

// remove fields
doc = api.opfParse(out);
api.opfApplyMetadata(doc, { isbn: '', series: '', description: '' });
const re2 = api.opfReadMetadata(api.opfParse(api.opfSerialize(doc)));
assert(re2.isbn === '' && re2.series === '' && re2.description === '', 'removal');

// cover: create when absent, find when present
doc = api.opfParse(opf);
let cover = api.opfEnsureCover(doc, 'OEBPS/');
assert(!cover.exists && cover.path === 'OEBPS/images/cover.jpg', 'new cover');
const again = api.opfEnsureCover(api.opfParse(api.opfSerialize(doc)), 'OEBPS/');
assert(again.exists && again.path === 'OEBPS/images/cover.jpg' && again.mediaType === 'image/jpeg', 'existing cover');

assert(api.opfIsbnOf('ISBN 978-0-306-40615-7'.replace('ISBN ', '')) === '9780306406157', 'isbn hyphens');
assert(api.opfIsbnOf('123') === '', 'isbn bad');
console.log('ALL OK');
