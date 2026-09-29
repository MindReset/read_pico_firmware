/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 执行真实网页脚本，模拟网络与DOM验证用户流程。/ Run the actual webpage script with network and DOM boundaries mocked.
 */
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const source = fs.readFileSync('components/read_pico_transfer/upload.html', 'utf8').match(/<script>([\s\S]*?)<\/script>/)[1];
const flush = () => new Promise(resolve => setImmediate(resolve));

async function setup({existing = false, confirmations = [], responses = [], deleteResponse = {ok: true}, fontsEnabled = true, bookLimit = 0} = {}) {
  const all = [];
  class Element {
    constructor() { this.children = []; this.dataset = {}; this.value = ''; all.push(this); }
    set textContent(text) { this.text = String(text); this.children = []; }
    get textContent() { return this.text || ''; }
    set innerHTML(_) { throw Error('Untrusted text must not use innerHTML'); }
    append(...children) { this.children.push(...children); }
    replaceChildren(...children) { this.children = children; }
  }
  const nodes = new Map();
  const document = {getElementById(id) { if (!nodes.has(id)) nodes.set(id, new Element()); return nodes.get(id); },
    createElement() { return new Element(); }, querySelectorAll() { return all.filter(e => e.dataset.mutate); }};
  const state = {sent: [], requested: [], deleted: 0, retried: 0, retryURLs: []};
  const policy = {mode: 'ap', root: '/sdcard/books', free_bytes: 100000000, file_limit: bookLimit, wifi_configured: false,
    fonts_enabled: fontsEnabled, font_root: '/sdcard/fonts', font_limit: 33554432};
  const fetch = async (url, options = {}) => {
    state.requested.push(url);
    let status = 200, body;
    if (url === '/info') body = policy;
    else if (options.method === 'DELETE') { ++state.deleted; body = deleteResponse; if (body.progress_cleanup_failed) status = 500; }
    else if (options.method === 'POST') { ++state.retried; state.retryURLs.push(url); body = {ok: true}; }
    else if (url.startsWith('/books?name=') || url.startsWith('/fonts?name=')) { status = existing ? 200 : 404; body = existing ? {item: {name: 'same.txt', size: 10}} : {error: '不存在'}; }
    else body = {root: '/sdcard/books', root_label: 'TF 卡', items: [], total: 0, pages: 0};
    return {ok: status === 200, status, json: async () => body};
  };
  class XHR {
    constructor() { this.upload = {}; }
    open(method, url) { this.url = url; assert.equal(method, 'PUT'); }
    send(file) {
      state.sent.push(this.url);
      this.upload.onprogress({loaded: file.size / 2, total: file.size});
      const response = responses.shift() || {status: 200, body: {ok: true}};
      if (response === 'pending') return;
      queueMicrotask(() => {
        if (response === 'error') return this.onerror();
        this.status = response.status; this.responseText = JSON.stringify(response.body); this.onload();
      });
    }
    abort() { this.onabort(); }
  }
  const context = vm.createContext({document, fetch, XMLHttpRequest: XHR, TextEncoder, Date,
    confirm: () => confirmations.length ? confirmations.shift() : true});
  vm.runInContext(source, context); await flush(); await flush();
  const file = {name: '<book & test>.txt', size: 5000000};
  function item(name = file.name, kind = 'book') { return {file: {...file, name}, row: new Element(), kind}; }
  async function run(items) { context.testItems = items; return vm.runInContext('run(testItems)', context); }
  return {context, nodes, state, item, run};
}

(async () => {
  let t = await setup({existing: true, confirmations: [false]});
  let item = t.item(); await t.run([item]); assert.match(item.row.textContent, /跳过/); assert.equal(t.state.sent.length, 0);
  t = await setup({existing: true, confirmations: [true]}); item = t.item(); await t.run([item]);
  assert.match(t.state.sent[0], /overwrite=1/); assert.match(item.row.textContent, /已保存/);
  t = await setup({confirmations: [false], responses: [{status: 409, body: {conflict: true}}]});
  item = t.item(); await t.run([item]); assert.match(item.row.textContent, /跳过/); assert.equal(t.state.sent.length, 1);
  t = await setup({responses: ['error', {status: 200, body: {ok: true}}]});
  item = t.item(); await t.run([item]); assert.match(item.row.textContent, /网络中断/);
  await item.row.children.find(b => b.textContent === '重试此文件').onclick(); assert.match(item.row.textContent, /已保存/); assert.equal(t.state.sent.length, 2);
  t = await setup({responses: ['pending']}); const items = [t.item('first.txt'), t.item('second.txt')];
  const pending = t.run(items); await flush(); t.nodes.get('cancel').onclick(); await pending;
  assert.equal(t.state.sent.length, 1); assert.match(items[0].row.textContent, /取消/); assert.match(items[1].row.textContent, /未上传/);
  t = await setup({responses: [{status: 500, body: {committed: true, progress_cleanup_failed: true, error: '文件已保存，但旧阅读进度清理失败'}}]});
  item = t.item(); await t.run([item]); assert.match(item.row.textContent, /已保存/);
  await item.row.children.find(b => b.textContent === '重试清理阅读进度').onclick();
  assert.equal(t.state.sent.length, 1); assert.equal(t.state.retried, 1);
  t = await setup({responses: [{status: 500, body: {committed: true, progress_cleanup_failed: true, name: 'Actual.txt', error: '清理失败'}}]});
  item = t.item('actual.txt'); await t.run([item]);
  await item.row.children.find(b => b.textContent === '重试清理阅读进度').onclick();
  assert.match(t.state.retryURLs[0], /name=Actual\.txt&/);
  t = await setup({deleteResponse: {deleted: true, progress_cleanup_failed: true, name: 'Actual.txt', error: '清理失败'}});
  item = t.item('actual.txt'); t.context.testBook = item.file; t.context.testRow = item.row;
  await vm.runInContext('removeBook(testBook,testRow)', t.context);
  const notice = t.nodes.get('results').children[0];
  await notice.children.find(b => b.textContent === '重试清理阅读进度').onclick();
  assert.match(t.state.retryURLs[0], /name=Actual\.txt&/);
  t = await setup({responses: [{status: 200, body: {ok: true, committed: true, storage_cleanup_failed: true, warning: '旧书备份已保留，请勿重复上传'}}]});
  item = t.item(); await t.run([item]); assert.match(item.row.textContent, /已保存.*备份已保留/);
  assert.equal(t.state.sent.length, 1); assert.equal(item.row.children.length, 0);
  t = await setup({confirmations: [false, true]}); item = t.item(); t.context.testBook = item.file; t.context.testRow = item.row;
  await vm.runInContext('removeBook(testBook,testRow)', t.context); assert.equal(t.state.deleted, 0);
  await vm.runInContext('removeBook(testBook,testRow)', t.context); assert.equal(t.state.deleted, 1);
  t = await setup({bookLimit: 1048576}); item = t.item('FullChinese.ttf', 'font');
  await t.run([item]); assert.match(t.state.sent[0], /^\/fonts\?name=FullChinese.ttf$/);
  assert(t.state.requested.some(u => u === '/fonts?name=FullChinese.ttf'));
  assert.match(item.row.textContent, /已保存.*字体/); assert.equal(t.state.retried, 0);
  t = await setup({fontsEnabled: false}); item = t.item('Font.ttf', 'font'); await t.run([item]);
  assert.match(item.row.textContent, /TF 卡/); assert.equal(t.state.sent.length, 0); assert(t.nodes.get('sendFonts').disabled);
  t = await setup(); item = t.item('Font.otf', 'font'); await t.run([item]);
  assert.match(item.row.textContent, /仅支持 TTF/); assert.equal(t.state.sent.length, 0);
  item = t.item('Huge.ttf', 'font'); item.file.size = 33554433; await t.run([item]);
  assert.match(item.row.textContent, /超过单文件/); assert.equal(t.state.sent.length, 0);
  t = await setup({existing: true, confirmations: [false]}); item = t.item('Font.ttf', 'font'); await t.run([item]);
  assert.match(item.row.textContent, /已跳过/); assert.equal(t.state.sent.length, 0);
  t = await setup({existing: true, confirmations: [true]}); item = t.item('Font.ttf', 'font'); await t.run([item]);
  assert.match(t.state.sent[0], /^\/fonts\?.*overwrite=1/);
  t = await setup({responses: [{status: 422, body: {error: '字体校验失败'}}, {status: 200, body: {ok: true}}]});
  item = t.item('Font.ttf', 'font'); await t.run([item]); assert.match(item.row.textContent, /校验失败/);
  await item.row.children.find(b => b.textContent === '重试此文件').onclick();
  assert(t.state.sent.every(u => u.startsWith('/fonts?'))); assert.match(item.row.textContent, /已保存/);
  t = await setup({responses: ['pending']}); const fonts = [t.item('One.ttf', 'font'), t.item('Two.ttf', 'font')];
  const fontPending = t.run(fonts); await flush(); t.nodes.get('cancel').onclick(); await fontPending;
  assert.equal(t.state.sent.length, 1); assert.match(fonts[1].row.textContent, /未上传/);
  console.log('transfer web interaction tests passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
