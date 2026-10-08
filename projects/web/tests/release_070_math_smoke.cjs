'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
class Node {
  constructor(tag = '#text', text='') {
    this.localName = tag === '#text' ? undefined : tag;
    this.tagName = tag.toUpperCase(); this.nodeType=tag==='#text'?3:1;
    this.textValue=text;this.attrs={};this.children=[];this.parentNode=null;
  }
  get childNodes(){return this.children;}
  get textContent(){return this.nodeType===3?this.textValue:this.children.map(x=>x.textContent).join('');}
  set textContent(v){this.children=[];this.textValue=String(v);if(v && this.nodeType!==3){this.appendChild(new Node('#text',String(v)));}}
  setAttribute(k,v){this.attrs[k]=String(v)}
  appendChild(ch){this.children.push(ch);ch.parentNode=this;return ch}
  append(...children){for(const c of children)this.appendChild(c)}
  replaceChild(next,old){const pos=this.children.indexOf(old);if(pos<0)throw Error('no child');this.children.splice(pos,1,...(next.fragment?next.children:[next]));for(const c of this.children)c.parentNode=this;}
  cloneNode(deep){const node=new Node(this.nodeType===3?'#text':this.localName,this.textValue);node.attrs={...this.attrs};if(deep)for(const c of this.children)node.appendChild(c.cloneNode(true));return node}
}
const document = {createElementNS(_ns,tag){return new Node(tag)},createElement(tag){return new Node(tag)},createTextNode(value){return new Node('#text',value)},createDocumentFragment(){const n=new Node('fragment');n.fragment=true;return n}};
const ctx={document};ctx.globalThis=ctx;
vm.runInNewContext(fs.readFileSync(path.join(__dirname,'../js/math.js'),'utf8'),ctx);
const math=ctx.EngCoderMath;
const fixture='The value remains \\(1\\). Since \\(X=1\\):\\n\\[5X+3=5(1)+3=8\\equiv1\\pmod{7}\\]\\n\\[\\boxed{1}\\]';
const tokens=math.tokenizeMath(fixture);
assert.equal(tokens.filter(x=>'math' in x).length,4);
const host=document.createElement('div');host.appendChild(document.createTextNode(fixture));math.renderMathInChat(host);
const xml=JSON.stringify(host,(key,value)=>key==='parentNode'?undefined:value);
for(const expected of ['"localName":"math"','"localName":"menclose"','"localName":"mrow"','"textValue":"≡"'])assert.ok(xml.includes(expected),expected);
const fraction=math.parseTeX('\\frac{9}{100}');assert.equal(fraction.children[0].localName,'mfrac');
const code=document.createElement('pre');code.appendChild(document.createTextNode('\\(1\\)'));const safe=document.createElement('div');safe.appendChild(code);math.renderMathInChat(safe);assert.equal(code.children[0].nodeType,3);
const plain=document.createElement('div');plain.appendChild(document.createTextNode('<img src=x onerror=alert(1)> \\(x^2\\)'));math.renderMathInChat(plain);assert.equal(plain.children[0].nodeType,3);
assert.equal(plain.children[0].textContent,'<img src=x onerror=alert(1)> ');
console.log('PASS: offline MathML display, fractions, boxed math, code isolation, text safety');
