import { readFileSync, mkdirSync, writeFileSync } from 'node:fs';
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';
const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const mod = new WebAssembly.Module(readFileSync(resolve(root, 'dist/PortraitCraft.wasm')));
const manifest = JSON.parse(readFileSync(resolve(root, 'manifest.json')));
const defaults = Object.fromEntries(Object.entries(manifest.params).map(([k,v])=>[k,v.default]));
const encode = x => new TextEncoder().encode(JSON.stringify(x));
function run(input, w, h, params={}, {channels=4, depth=16, mode=3, alpha=channels===4}={}) {
  const e = new WebAssembly.Instance(mod).exports;
  const p=e.pc_alloc(input.byteLength);
  const j=encode({...defaults,...params}); const jp=e.pc_alloc(j.length);
  new Float32Array(e.memory.buffer,p,input.length).set(input);
  new Uint8Array(e.memory.buffer,jp,j.length).set(j);
  const code=e.pc_filter(p,input.byteLength,w,h,channels,depth|(alpha?256:0)|(mode<<16),jp,j.length);
  return {code, data:new Float32Array(e.memory.buffer,p,input.length).slice()};
}
function fixture(w,h) {
  const p = new Float32Array(w*h*4);
  let seed=1323;
  for(let y=0;y<h;y++) for(let x=0;x<w;x++) {
    seed=(Math.imul(seed,1664525)+1013904223)>>>0;
    const noise=((seed>>>8)/16777216-.5)*.055;
    const i=(y*w+x)*4;
    const skin=x<w*.7;
    const color=skin ? [.72,.46,.33] : [.05,.15,.78];
    for(let c=0;c<3;c++) p[i+c]=color[c]+noise;
    p[i+3]=1;
  } return p;
}
function mse(a,b,indices) { let s=0; for(const i of indices) s+=(a[i]-b[i])**2; return s/indices.length; }
test('ABI, no imports, exported manifest matches source',()=>{
  assert.deepEqual(WebAssembly.Module.imports(mod),[]);
  const e=new WebAssembly.Instance(mod).exports; assert.equal(e.pc_abi_version(),1);
  const v=e.pc_manifest(), p=Number(v&0xffffffffn), n=Number(v>>32n);
  assert.deepEqual(JSON.parse(new TextDecoder().decode(new Uint8Array(e.memory.buffer,p,n))),manifest);
});
test('amount zero is bit-exact, including HDR and alpha',()=>{
  const p=fixture(47,39); p[0]=2.5; p[1]=-.2; p[3]=.37;
  const r=run(p,47,39,{amount:0,warmth:100},{depth:32});
  assert.equal(r.code,0); assert.deepEqual(r.data,p);
});
test('fine/medium/large zero and neutral enhancements are identity',()=>{
  const p=fixture(37,29), r=run(p,37,29,{preset:'Custom',fine:0,medium:0,large:0});
  assert.equal(r.code,0); assert.ok(Math.max(...r.data.map((v,i)=>Math.abs(v-p[i])))<1e-7);
});
test('smoothing reduces measured skin noise and excludes blue background',()=>{
  const w=120,h=90,p=fixture(w,h);
  const {code,data}=run(p,w,h,{preset:'Natural',amount:100,texture:0}); assert.equal(code,0);
  const indices=[]; const truth=new Float32Array(p.length);
  for(let y=15;y<h-15;y++) for(let x=15;x<65;x++) {
    const i=(y*w+x)*4; indices.push(i); truth[i]=.72;
  }
  assert.ok(mse(data,truth,indices)<mse(p,truth,indices)*.60);
  for(let y=0;y<h;y++) for(let x=90;x<w;x++) {
    const i=(y*w+x)*4; assert.deepEqual(data.slice(i,i+4),p.slice(i,i+4));
  }
});
test('texture protection retains more high frequency information',()=>{
  const p=fixture(80,61);
  const a=run(p,80,61,{preset:'Custom',texture:0}).data, b=run(p,80,61,{preset:'Custom',texture:100}).data;
  const ids=Array.from({length:p.length},(_,i)=>i).filter(i=>i%4!==3);
  assert.ok(mse(b,p,ids)<mse(a,p,ids));
});
test('presets distinctly reduce both pores and broader skin unevenness in order',()=>{
  const w=192,h=144, truth=fixture(w,h), p=truth.slice(), ids=[];
  for(let y=0;y<h;y++) for(let x=0;x<w;x++) {
    const i=(y*w+x)*4;
    if(x<w*.7) {
      const blotch=.045*Math.sin(x*.23)*Math.cos(y*.19);
      for(let k=0;k<3;k++) {
        truth[i+k]=[.72,.46,.33][k]; p[i+k]+=blotch;
        if(x>25 && x<100 && y>25 && y<h-25) ids.push(i+k);
      }
    }
  }
  const errors=['Gentle','Natural','Strong smoothing'].map(preset=>{
    const r=run(p,w,h,{preset}); assert.equal(r.code,0);
    // The stronger preset must not spill onto an excluded background.
    for(let y=0;y<h;y++) for(let x=150;x<w;x++) {
      const i=(y*w+x)*4; assert.deepEqual(r.data.slice(i,i+4),p.slice(i,i+4));
    }
    return mse(r.data,truth,ids)/mse(p,truth,ids);
  });
  assert.ok(errors[1]<errors[0]*.75,JSON.stringify(errors));
  assert.ok(errors[2]<errors[1]*.65,JSON.stringify(errors));
  writeFileSync(resolve(root,'tests/output/preset-metrics.json'),JSON.stringify({
    fixture:'synthetic pores plus medium-scale unevenness; default amount 85',
    residualMseRatio:Object.fromEntries(['Gentle','Natural','Strong smoothing'].map((p,i)=>[p,errors[i]]))
  },null,2)+'\n');
});
test('named recipes are independent of custom sliders; natural matches custom defaults',()=>{
  const p=fixture(73,61);
  for(const preset of ['Gentle','Natural','Strong smoothing']) {
    assert.deepEqual(run(p,73,61,{preset}).data,run(p,73,61,{
      preset,fine:0,medium:0,large:0,texture:100,edge:100,scale:4,threshold:0
    }).data);
    assert.deepEqual(run(p,73,61,{preset,amount:0}).data,p);
  }
  assert.deepEqual(run(p,73,61,{preset:'Natural'}).data,run(p,73,61,{preset:'Custom'}).data);
  for(const [file,preset] of [['gentle','Gentle'],['natural','Natural'],['strong','Strong smoothing'],['custom-natural','Custom']]) {
    const params=JSON.parse(readFileSync(resolve(root,`presets/${file}.json`)));
    assert.deepEqual(run(p,73,61,params).data,run(p,73,61,{preset}).data,`${file} CLI preset matches dropdown`);
  }
});
test('custom threshold and edge protection independently control smoothing',()=>{
  const w=96,h=80,p=fixture(w,h), params={preset:'Custom',mask:'Whole image',amount:100};
  const ids=Array.from({length:p.length},(_,i)=>i).filter(i=>i%4!==3);
  const low=run(p,w,h,{...params,threshold:0}).data;
  const high=run(p,w,h,{...params,threshold:100}).data;
  assert.ok(mse(high,p,ids)>mse(low,p,ids)*2);
  const protect=run(p,w,h,{...params,edge:100}).data;
  const unprotect=run(p,w,h,{...params,edge:0}).data;
  assert.ok(mse(protect,p,ids)<mse(unprotect,p,ids));
  // High-contrast features keep almost all contrast, even in the strong preset.
  const edge=new Float32Array(w*h*4);
  for(let y=0;y<h;y++) for(let x=0;x<w;x++) {
    const i=(y*w+x)*4; edge.set(x<48 ? [.2,.2,.2,1] : [.8,.8,.8,1],i);
  }
  const r=run(edge,w,h,{preset:'Strong smoothing',mask:'Whole image',amount:100}).data;
  assert.ok(r[(40*w+48)*4]-r[(40*w+47)*4]>.55);
});
test('automatic scale uses full canvas dimensions, never band dimensions',()=>{
  const p=fixture(97,79);
  for(const [canvasWidth,canvasHeight,scale] of [[1999,2000,1],[2000,2000,2],[4000,2500,3],[5000,4000,4]]) {
    const params={preset:'Custom',_image:{canvasWidth,canvasHeight}};
    assert.deepEqual(run(p,97,79,{...params,scale:0}).data,run(p,97,79,{...params,scale}).data);
  }
});
test('all scales, sharpening and tonal controls have an effect',()=>{
  const p=fixture(81,63);
  const neutral={preset:'Custom',fine:0,medium:0,large:0,texture:0,mask:'Whole image',amount:100};
  for(const key of ['fine','medium','large','sharpen','warmth','tint','brightness','contrast','saturation']) {
    const r=run(p,81,63,{...neutral,[key]:75}); assert.equal(r.code,0);
    assert.ok(r.data.some((v,i)=>Math.abs(v-p[i])>1e-5), key);
  }
});
test('transparent RGB is ignored by smoothing; alpha is bit-exact',()=>{
  const a=fixture(53,43),b=a.slice();
  for(let y=0;y<43;y++) for(let x=0;x<53;x++) {
    const i=(y*53+x)*4;
    a[i+3]=b[i+3]=x<26 ? 0 : ((y%5)+1)/5;
    if(x<26) { a[i]=1; a[i+1]=0; a[i+2]=1; b[i]=b[i+1]=b[i+2]=0; }
  }
  const ra=run(a,53,43,{mask:'Whole image'}).data, rb=run(b,53,43,{mask:'Whole image'}).data;
  for(let i=0;i<a.length;i+=4) {
    assert.equal(ra[i+3],a[i+3]);
    if(a[i+3]>0) for(let k=0;k<3;k++) assert.ok(Math.abs(ra[i+k]-rb[i+k])<1e-6);
    else assert.deepEqual(ra.slice(i,i+4),a.slice(i,i+4));
  }
});
test('RGB 8, 16 and 32 bit; ordinary HDR stays above white',()=>{
  const p=new Float32Array(17*13*3).fill(1.8);
  for(const depth of [8,16,32]) {
    const r=run(p,17,13,{mask:'Whole image'},{channels:3,depth}); assert.equal(r.code,0);
    assert.ok(r.data.every(Number.isFinite));
    assert.ok(r.data.every(x=>depth===32 ? x>1.79 : x<=1));
  }
});
test('unsupported color modes fail explicitly without writing',()=>{
  const p=fixture(10,10);
  for(const mode of [0,1,2,4,7,8,9]) {
    const r=run(p,10,10,{}, {mode}); assert.equal(r.code,2); assert.deepEqual(r.data,p);
  }
});
test('bad dimensions and pointers fail safely; allocation cap is enforced',()=>{
  const e=new WebAssembly.Instance(mod).exports;
  assert.equal(e.pc_filter(0,10,1,1,4,3<<16,0,2),1);
  assert.equal(e.pc_alloc(0),0); assert.equal(e.pc_alloc(536870913),0);
  assert.equal(e.pc_filter(65535,16,1,1,4,(3<<16)|16,65536,2),1);
  assert.equal(run(fixture(4,4),5,4).code,1);
});
test('mask modes, sample RGB and tolerance work',()=>{
  const p=fixture(20,20);
  const auto=run(p,20,20,{view:'Skin mask'}).data;
  assert.ok(auto[0]>.9); assert.equal(auto[19*4],0);
  const all=run(p,20,20,{view:'Skin mask',mask:'Whole image'}).data;
  assert.ok(all.every(v=>v===1));
  const target=run(p,20,20,{view:'Skin mask',mask:'Sampled skin',sampleR:13,sampleG:38,sampleB:199,tolerance:25}).data;
  assert.ok(target[19*4]>.9); assert.equal(target[0],0);
  const tight=run(p,20,20,{view:'Skin mask',tolerance:0}).data;
  const wide=run(p,20,20,{view:'Skin mask',tolerance:100}).data;
  assert.ok(wide.reduce((s,v)=>s+v,0)>=tight.reduce((s,v)=>s+v,0));
});
test('red overlay is local and preserves alpha',()=>{
  const p=fixture(20,20),r=run(p,20,20,{view:'Red overlay'}).data;
  assert.ok(r[0]>p[0]); assert.ok(r[1]<p[1]);
  assert.deepEqual(r.slice(19*4,20*4),p.slice(19*4,20*4));
});
test('split view uses canvas position, not the band center',()=>{
  const p=fixture(20,20);
  assert.deepEqual(run(p,20,20,{view:'Split view',_image:{x:0,canvasWidth:100}}).data,p);
  assert.deepEqual(run(p,20,20,{view:'Split view',_image:{x:60,canvasWidth:100}}).data,run(p,20,20).data);
});
test('NaN/Infinity do not spread; fully transparent hidden values stay untouched',()=>{
  const p=fixture(19,17); p[4]=NaN; p[8]=Infinity; p[12]=-Infinity;
  const r=run(p,19,17,{mask:'Whole image'}); assert.equal(r.code,0);
  for(let i=16;i<p.length;i++) assert.ok(Number.isFinite(r.data[i]));
  assert.ok(Number.isNaN(r.data[4])); assert.equal(r.data[8],Infinity);
});
test('48 px halo gives seamless 2D tiles at maximum radius',()=>{
  const w=149,h=139,p=fixture(w,h), params={preset:'Custom',scale:4,sharpen:45,warmth:10};
  const full=run(p,w,h,params).data, tiled=new Float32Array(p.length), halo=48;
  for(let oy=0;oy<h;oy+=47) for(let ox=0;ox<w;ox+=53) {
    const iw=Math.min(53,w-ox),ih=Math.min(47,h-oy),tw=iw+2*halo,th=ih+2*halo;
    const tile=new Float32Array(tw*th*4);
    for(let y=0;y<th;y++) for(let x=0;x<tw;x++) {
      const sx=Math.max(0,Math.min(w-1,ox+x-halo)),sy=Math.max(0,Math.min(h-1,oy+y-halo));
      tile.set(p.subarray((sy*w+sx)*4,(sy*w+sx)*4+4),(y*tw+x)*4);
    }
    const r=run(tile,tw,th,{...params,_image:{x:ox-halo,canvasWidth:w}}); assert.equal(r.code,0);
    for(let y=0;y<ih;y++) for(let x=0;x<iw;x++) tiled.set(r.data.subarray(((y+halo)*tw+x+halo)*4,((y+halo)*tw+x+halo)*4+4),((oy+y)*w+ox+x)*4);
  }
  let max=0; for(let i=0;i<full.length;i++) max=Math.max(max,Math.abs(full[i]-tiled[i]));
  assert.ok(max<2e-6,`maximum seam error ${max}`);
});
test('1x1 and skinny images work at maximum radius',()=>{
  for(const [w,h] of [[1,1],[1,30],[30,1]]) {
    const r=run(fixture(w,h),w,h,{preset:'Custom',scale:4}); assert.equal(r.code,0); assert.ok(r.data.every(Number.isFinite));
  }
});
// Shared binary fixture for independent PhotoCraft host integration tests.
mkdirSync(resolve(root,'tests/output'),{recursive:true});
const reference=fixture(320,240).map(x=>Math.round(x*255)/255);
writeFileSync(resolve(root,'tests/output/reference-input.f32'),Buffer.from(reference.buffer));
writeFileSync(resolve(root,'tests/output/reference-output.f32'),Buffer.from(run(reference,320,240).data.buffer));
