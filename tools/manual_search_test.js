/* 中文：核对用户的真实问法与模糊关键词对应到文档中的操作。
 * English: Verify natural user questions and fuzzy keywords against documented tasks.
 */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const tasks = JSON.parse(fs.readFileSync(path.join(root, 'flash/manual/manual-index.json'), 'utf8'));
const search = require(path.join(root, 'flash/assets/manual-search.js')).create(tasks);
const cases = [
  ['最近看的书怎么排在前面', 'recent-sort'],
  ['请问如何按最近排序呢？', 'recent-sort'],
  ['英文怎么打大写', 'ime-case'],
  ['大小谢', 'ime-case'],
  ['Wi-Fi 密码输错了', 'password-fix'],
  ['wifi密吗', 'password-fix'],
  ['恢复阅读进度', 'restore-config'],
  ['怎么长按中间返回主页', 'custom-hold'],
  ['卡不识别 ESP_ERR_INVALID_STATE', 'faq-sd'],
  ['怎么调首行缩进', 'indent-fine'],
  ['怎么把一段话连续打出来', 'ime-phrases'],
  ['全键盘中英文切换', 'ime-english'],
  ['解除锁屏密码', 'pin-change'],
  ['不想显示图片', 'hide-images'],
  ['怎么设置自动休眠', 'auto-lock'],
  ['wifi传书', 'wifi-transfer'],
  ['手机设置签名', 'web-signature'],
  ['蓝牙书架翻页', 'ble-shelf'],
  ['文件改名', 'rename-file'],
  ['tf卡升级', 'tf-update'],
  ['想要回退版本', 'downgrade'],
  ['上下翻页', 'tap-vertical'],
  ['如何备份配置', 'save-config'],
  ['主页刷新模式', 'main-refresh']
];
for (const [question, expected] of cases) assert.equal(search(question)[0]?.id, expected, question);
for (const q of ['', '请问一下呢', '火星天气预报', '购买机票', '<script>alert(1)</script>']) assert.deepEqual(search(q), [], q);
assert.equal(new Set(tasks.map(t => t.id)).size, tasks.length);
const html = fs.readFileSync(path.join(root, 'flash/manual/index.html'), 'utf8');
for (const task of tasks) assert.ok(html.includes(`id="${task.id}"`), `Missing anchor: ${task.id}`);
for (const result of search('密码')) assert.ok(tasks.some(t => t.id === result.id && t.steps.join('') === result.steps.join('')));
console.log(`${cases.length} question/ranking cases, unrelated queries and all ${tasks.length} anchors verified.`);
