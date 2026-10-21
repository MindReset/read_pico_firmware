/* SPDX-License-Identifier: Apache-2.0
 * 中文：执行实际刷机页补丁，验证串口过滤、翻译及观察器不重复触发。
 * English: Execute actual flasher patches; verify port filtering, translation and observer idempotence.
 */
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const page=fs.readFileSync(process.argv[2]||'flash/index.html','utf8');
// 中文：执行实际外置桥接脚本，跳转脚本不属于安装器补丁。/ English: Execute the external bridge; the redirect is separate from installer patches.
const scripts=page.includes('src="./installer-bridge.js"')?[fs.readFileSync('flash/installer-bridge.js','utf8')]:[...page.matchAll(/<script>([\s\S]*?)<\/script>/g)].slice(0,2).map(x=>x[1]);
let options,writes=0,interval;const watched=[];
class Element{attachShadow(){this.shadowRoot={querySelectorAll:()=>[],ownerDocument:doc,texts:[]};return this.shadowRoot;}}
const doc={getElementById:()=>null,createTreeWalker(root){let at=0;return{nextNode:()=>root.texts[at++]||null}}};
const context=vm.createContext({window:{},navigator:{serial:{requestPort:async value=>{options=value;return 'pico'}}},Element,document:doc,NodeFilter:{SHOW_TEXT:4},MutationObserver:class{constructor(cb){this.cb=cb}observe(root){watched.push({root,cb:this.cb})}},setInterval:cb=>interval=cb,Map,WeakSet});
for(const script of scripts)vm.runInContext(script,context);
function text(value){return{value,get nodeValue(){return this.value},set nodeValue(v){writes++;this.value=v}};}
(async()=>{
 await context.navigator.serial.requestPort();assert.equal(options.filters[0].usbVendorId,0x303a);
 await context.navigator.serial.requestPort({filters:[{usbVendorId:1}]});assert.equal(options.filters[0].usbVendorId,1);
 const el=new Element(),root=el.attachShadow({mode:'open'});
 root.texts=['Do you want to erase the device before installing ','Pico','? All data on the device will be lost.','Erase device','Next','Installation complete!',
 '\n          Back\n        ',
 '\n Failed to initialize. Try resetting your device or holding the BOOT button while clicking INSTALL.\n ',
 'Pico Back Edition'].map(text);
 interval();assert.equal(watched.length,1);assert.match(root.texts.map(n=>n.value).join(''),/^安装 Pico 前是否擦除设备/);
 assert.equal(root.texts[3].value,'擦除设备');assert.equal(root.texts[4].value,'下一步');
 assert.equal(root.texts[6].value,'\n          返回\n        ');
 assert.match(root.texts[7].value,/未连接到刷写模式，尚未开始写入/);
 assert.match(root.texts[7].value,/设置 → 升级和恢复 → BOOT 刷机/);
 assert.equal(root.texts[8].value,'Pico Back Edition','never translate a word embedded in a firmware name');
 const before=writes;watched[0].cb();interval();assert.equal(writes,before,'translated nodes must not trigger another mutation');
 root.texts[3].value='Erase device';watched[0].cb();assert.equal(root.texts[3].value,'擦除设备');
 const manifest=JSON.parse(fs.readFileSync('flash/manifest.json'));assert.equal(manifest.new_install_prompt_erase,true);
 console.log('flasher patches: PASS (port filter, delayed shadow root, Chinese fragments, re-render, no mutation loop, preserve-data default)');
})().catch(e=>{console.error(e);process.exitCode=1});
