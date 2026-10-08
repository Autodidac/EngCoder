/* EngCoder MathML renderer — offline, no executable HTML or remote dependencies.
 * Displays TeX-style math in chat without modifying persisted model output.
 * This intentionally supports a safe subset; unrecognized commands remain visible.
 */
(function () {
  'use strict';
  const NS = 'http://www.w3.org/1998/Math/MathML';
  const aliases = Object.freeze({
    cdot:'⋅', times:'×', div:'÷', pm:'±', mp:'∓', le:'≤', leq:'≤', ge:'≥', geq:'≥',
    neq:'≠', ne:'≠', equiv:'≡', approx:'≈', sim:'∼', infty:'∞', to:'→', rightarrow:'→',
    leftarrow:'←', implies:'⇒', iff:'⇔', in:'∈', notin:'∉', subset:'⊂', subseteq:'⊆',
    forall:'∀', exists:'∃', sum:'∑', prod:'∏', int:'∫', partial:'∂', nabla:'∇',
    alpha:'α', beta:'β', gamma:'γ', delta:'δ', epsilon:'ε', theta:'θ', lambda:'λ',
    mu:'μ', pi:'π', sigma:'σ', phi:'φ', omega:'ω', Delta:'Δ', Sigma:'Σ', Pi:'Π',
    cdots:'⋯', ldots:'…', lbrace:'{', rbrace:'}', lvert:'|', rvert:'|',
    percent:'%', underscore:'_', textbackslash:'\\'
  });
  const operators = '+−-=<>×÷()[]{}|,;:!∑∏∫±∓∈≡≤≥≈';
  function element(tag, content) {
    const el = document.createElementNS(NS, tag);
    if (content !== undefined) el.textContent = String(content);
    return el;
  }
  function sequence(nodes) {
    if (nodes.length === 1) return nodes[0];
    const row = element('mrow');
    for (const n of nodes) row.appendChild(n);
    return row;
  }
  function parseTeX(expression) {
    const source = String(expression ?? '');
    let index = 0;
    const readGroupText = () => {
      while (source[index] === ' ') index++;
      if (source[index] !== '{') return '';
      let depth = 0, start = ++index;
      for (; index < source.length; index++) {
        if (source[index] === '{') depth++;
        else if (source[index] === '}') {
          if (depth === 0) { const text = source.slice(start, index); index++; return text; }
          depth--;
        }
      }
      return source.slice(start);
    };
    function argument() {
      while (/\s/.test(source[index] || '') && index < source.length) index++;
      if (source[index] === '{') { index++; return parse(true); }
      return atom();
    }
    function atom() {
      if (index >= source.length) return element('mrow');
      const ch = source[index++];
      if (ch === '{') return parse(true);
      if (ch === '\\') {
        const start = index;
        while (/[A-Za-z]/.test(source[index] || '')) index++;
        const name = index === start ? source[index++] || '' : source.slice(start, index);
        if (name === 'frac' || name === 'dfrac' || name === 'tfrac') {
          const fraction = element('mfrac');
          fraction.append(argument(), argument());
          return fraction;
        }
        if (name === 'sqrt') { const root = element('msqrt'); root.appendChild(argument()); return root; }
        if (name === 'boxed') { const box = element('menclose'); box.setAttribute('notation', 'box'); box.appendChild(argument()); return box; }
        if (name === 'text' || name === 'mathrm' || name === 'operatorname') return element('mtext', readGroupText());
        if (name === 'pmod') { const row = element('mrow'); row.append(element('mtext', '(mod '), argument(), element('mo', ')')); return row; }
        if (name === 'mod' || name === 'bmod') return element('mtext', ' mod ');
        if (name === 'left' || name === 'right' || name === 'big' || name === 'Big') return atom();
        if ([',',';',':','!',' '].includes(name)) return element('mspace', '');
        if (Object.hasOwn(aliases,name)) return element('mo', aliases[name]);
        return element('mtext', `\\${name}`);
      }
      if (/[0-9]/.test(ch)) {
        const start = index - 1;
        while (/[0-9.,]/.test(source[index] || '')) index++;
        return element('mn', source.slice(start, index));
      }
      if (/[A-Za-z]/.test(ch)) return element('mi', ch);
      if (ch === ' ' || ch === '\n' || ch === '\t') return element('mspace');
      return element(operators.includes(ch) ? 'mo' : 'mtext', ch === '-' ? '−' : ch);
    }
    function parse(inGroup = false) {
      const nodes = [];
      while (index < source.length) {
        if (inGroup && source[index] === '}') { index++; break; }
        const next = source[index];
        if ((next === '^' || next === '_') && nodes.length) {
          index++;
          const base = nodes.pop();
          const child = argument();
          const sub = next === '_';
          if ((sub && base.localName === 'msup') || (!sub && base.localName === 'msub')) {
            const both = element('msubsup');
            const old = base.children;
            both.append(old[0].cloneNode(true), sub ? child : old[1].cloneNode(true), sub ? old[1].cloneNode(true) : child);
            nodes.push(both);
          } else {
            const scripted = element(sub ? 'msub' : 'msup'); scripted.append(base, child); nodes.push(scripted);
          }
        } else nodes.push(atom());
      }
      return sequence(nodes);
    }
    const math = element('math');
    math.setAttribute('xmlns', NS);
    math.appendChild(parse());
    return math;
  }
  function tokenizeMath(text) {
    const source = String(text ?? '');
    const result = [];
    let cursor = 0, plainStart = 0;
    const delimiters = [['\\[','\\]',true],['\\(','\\)',false],['$$','$$',true],['$','$',false]];
    while (cursor < source.length) {
      const match = delimiters.find(([open]) => source.startsWith(open,cursor));
      if (!match || (cursor > 0 && source[cursor-1] === '\\')) { cursor++; continue; }
      const [open,close,display] = match;
      const end = source.indexOf(close,cursor+open.length);
      if (end < 0) { cursor+=open.length; continue; }
      const inner = source.slice(cursor+open.length,end).trim();
      if (!inner || (open === '$' && inner.includes('\n'))) { cursor += open.length; continue; }
      if (plainStart < cursor) result.push({text: source.slice(plainStart,cursor)});
      result.push({math:inner,display});
      cursor = end+close.length;
      plainStart = cursor;
    }
    if (plainStart < source.length) result.push({text: source.slice(plainStart)});
    return result;
  }
  function renderMathInChat(root) {
    if (!root || typeof document === 'undefined') return;
    function visit(node) {
      if (node.nodeType === 3) {
        const parts = tokenizeMath(node.textContent);
        if (!parts.some(part => 'math' in part)) return;
        const fragment = document.createDocumentFragment();
        for (const part of parts) {
          if ('text' in part) fragment.appendChild(document.createTextNode(part.text));
          else {
            try {
              const math = parseTeX(part.math);
              math.setAttribute('display',part.display ? 'block' : 'inline');
              const wrap = document.createElement('span');
              wrap.className = `chat-math ${part.display ? 'chat-math-display' : 'chat-math-inline'}`;
              wrap.setAttribute('aria-label',part.math);
              wrap.appendChild(math);
              fragment.appendChild(wrap);
            } catch (_) { fragment.appendChild(document.createTextNode(part.math)); }
          }
        }
        node.parentNode?.replaceChild(fragment,node);
        return;
      }
      if (node.nodeType !== 1) return;
      if (['PRE','CODE','SCRIPT','STYLE','TEXTAREA','MATH'].includes(node.tagName)) return;
      for (const child of [...node.childNodes]) visit(child);
    }
    visit(root);
  }
  if (typeof globalThis !== 'undefined') {
    globalThis.EngCoderMath = Object.freeze({parseTeX,tokenizeMath,renderMathInChat});
  }
})();
