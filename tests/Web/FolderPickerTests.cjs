const fs = require('node:fs');
const assert = require('node:assert/strict');
const path = require('node:path');
// 用模拟的浏览器与虚拟文件系统运行 WebApp.cpp 里 MiniCADWeb_PickFolder 的 JS（找外部参照的文件夹选择）。
const source = fs.readFileSync(path.join(__dirname, '../../apps/Web/src/Host/WebApp.cpp'), 'utf8');
const head = source.indexOf('EM_JS(void, MiniCADWeb_PickFolder');
const start = source.indexOf('{', head) + 1;
const end = source.indexOf('\n    });', start);
assert.ok(head >= 0 && end > start, 'EM_JS folder picker body exists');
const body = source.slice(start, end);

const file = (name, byte, relative) => ({ name, webkitRelativePath: relative, arrayBuffer: async () => new Uint8Array([byte]).buffer });

function run(id, names, window, input) {
  return new Promise(resolve => {
    const writes = new Map(), dirs = [];
    const FS = { mkdirTree: d => dirs.push(d), writeFile: (p, data) => writes.set(p, data[0]) };
    const execute = new Function('requestId', 'namesPtr', 'window', 'document', 'UTF8ToString', 'stringToNewUTF8', 'FS', '_MiniCADWeb_OnFolderPicked', '_free', body);
    const document = { createElement: () => input, body: { appendChild() {} } };
    execute(id, names, window, document, x => x, x => x, FS, (rid, dir, error) => resolve({ rid, dir, error, writes, dirs }), () => {});
  });
}

// showDirectoryPicker：逐级遍历，只读需要的文件（不分大小写，无扩展名的名字匹配 .dwg）
function dirHandle(name, entries) {
  return { kind: 'directory', name, async *values() { yield* entries; } };
}
function fileHandle(name, byte) {
  return { kind: 'file', name, getFile: async () => file(name, byte) };
}

(async () => {
  const tree = dirHandle('项目', [
    dirHandle('共用', [dirHandle('xref', [fileHandle('轴网.DWG', 1), fileHandle('无关.dwg', 2)])]),
    fileHandle('Xref-A.dwg', 3),
    fileHandle('readme.txt', 4),
  ]);
  const a = await run(7, '轴网.dwg\nxref-a\n', { showDirectoryPicker: async () => tree }, null);
  assert.equal(a.rid, 7);
  assert.equal(a.dir, '/xref/7');
  assert.equal(a.error, '');
  assert.deepEqual([...a.writes.keys()].sort(), ['/xref/7/项目/Xref-A.dwg', '/xref/7/项目/共用/xref/轴网.DWG']);

  // 取消目录选择：回调空目录、不报错
  const abort = Object.assign(new Error('cancel'), { name: 'AbortError' });
  const b = await run(8, 'a.dwg\n', { showDirectoryPicker: async () => { throw abort; } }, null);
  assert.deepEqual([b.dir, b.error], ['', '']);

  // 没有 showDirectoryPicker：<input webkitdirectory>，按 webkitRelativePath 保留子目录，只读需要的文件
  const events = new Map();
  const input = { style: {}, addEventListener: (n, f) => events.set(n, f), remove() {}, click() {} };
  const pending = run(9, '轴网.dwg\n', {}, input);
  assert.equal(input.webkitdirectory, true);
  input.files = [file('轴网.dwg', 5, '资料/深层/轴网.dwg'), file('其他.dwg', 6, '资料/其他.dwg')];
  await events.get('change')();
  const c = await pending;
  assert.equal(c.dir, '/xref/9');
  assert.deepEqual([...c.writes.entries()], [['/xref/9/资料/深层/轴网.dwg', 5]]);

  console.log('PASS: directory handle walk with name filter; cancel; webkitdirectory fallback keeps subfolders.');
})().catch(e => { console.error(e); process.exitCode = 1; });
