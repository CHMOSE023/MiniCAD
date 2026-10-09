const fs = require('node:fs');
const assert = require('node:assert/strict');
const path = require('node:path');
// Exercise the shipped picker code with a simulated browser and virtual FS.
const source = fs.readFileSync(path.join(__dirname, '../../apps/Web/src/Host/WebApp.cpp'), 'utf8');
const start = source.indexOf('const input = document.createElement("input");');
const end = source.indexOf('\n    });', start);
assert.ok(start >= 0 && end > start, 'EM_JS picker body exists');
const body = source.slice(start, end);
async function run(id, files, cancel = false) {
  const events = new Map(), calls = [], writes = new Map();
  const input = {style:{}, files, addEventListener:(name, f)=>events.set(name,f), remove(){}, click(){}};
  const execute = new Function('requestId','acceptPtr','multiple','document','UTF8ToString','stringToNewUTF8','FS','_MiniCADWeb_OnFilePicked','_MiniCADWeb_OnFilePickCompleted','_free',body);
  execute(id,'.dwg',1,{createElement:()=>input,body:{appendChild(){}}},x=>x,x=>x,{mkdirTree(){},writeFile:(p,data)=>writes.set(p,data)},(id,path)=>calls.push(['file',id,path]),(id,error)=>calls.push(['done',id,error]),()=>{});
  if(cancel){ events.get('cancel')(); events.get('cancel')(); }
  else await events.get('change')();
  return {calls,writes,input};
}
(async()=>{
  const file = (name, byte)=>({name,arrayBuffer:async()=>new Uint8Array([byte]).buffer});
  const a = await run(1,[file('同名 图纸.dwg',1),file('同名 图纸.dwg',2)]);
  assert.equal(a.input.multiple,true);
  assert.deepEqual(a.calls,[['file',1,'/upload/1/0/同名 图纸.dwg'],['file',1,'/upload/1/1/同名 图纸.dwg'],['done',1,'']]);
  assert.equal(a.writes.get('/upload/1/0/同名 图纸.dwg')[0],1);
  assert.equal(a.writes.get('/upload/1/1/同名 图纸.dwg')[0],2);
  const b = await run(2,[file('同名 图纸.dwg',3)]);
  assert.equal(b.calls[0][2],'/upload/2/0/同名 图纸.dwg');
  const mixed = await run(3,[file('good.dwg',4),{name:'broken.dwg',arrayBuffer:async()=>{throw Error('read denied')}},file('last.dwg',5)]);
  assert.equal(mixed.calls.length,3);
  assert.equal(mixed.calls[1][2],'/upload/3/2/last.dwg');
  assert.match(mixed.calls[2][2],/broken.dwg: Error: read denied/);
  assert.deepEqual((await run(4,[],true)).calls,[['done',4,'']]);
  assert.deepEqual((await run(5,[])).calls,[['done',5,'']]);
  console.log('PASS: multi-file order; same-name independence across files and requests; partial read failure; cancellation once; empty selection.');
})().catch(e=>{console.error(e);process.exitCode=1;});
