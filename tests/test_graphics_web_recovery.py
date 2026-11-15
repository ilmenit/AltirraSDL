"""Exercise the actual context-loss handler without an SDL/WebGL canvas."""
from pathlib import Path
import shutil
import subprocess
import pytest


def test_web_context_loss_reload_keeps_storage():
    node = shutil.which('node')
    if not node:
        pytest.skip('Node is needed to exercise the browser handler')
    source = (Path(__file__).resolve().parents[1] /
              'src/AltirraSDL/source/display/display_recovery.cpp').read_text()
    handler = source.split('EM_ASM({', 1)[1].split('\n\t});', 1)[0]
    harness = r'''
const assert = require('node:assert/strict');
let eventHandler, stopped = false, reloaded = false;
const children = [];
const canvas = {addEventListener: (event, callback) => {
  assert.equal(event, 'webglcontextlost'); eventHandler = callback;
}};
const Module = {canvas, _ATGraphicsContextLost: () => { stopped = true; }};
const window = {location: {reload: () => { reloaded = true; }}};
const document = {
  createElement: tag => ({tag, style: {}, children: [],
    setAttribute(name, value) { this[name] = value; },
    appendChild(child) { this.children.push(child); }}),
  body: {appendChild: child => children.push(child)}
};
'''
    checks = r'''
assert.equal(children.length, 0);
eventHandler();
assert.equal(stopped, true);
assert.equal(reloaded, false);
assert.equal(children[0].role, 'alert');
assert.match(children[0].textContent, /Unsaved progress/);
assert.equal(children[0].children[0].textContent, 'Reload');
children[0].children[0].onclick();
assert.equal(reloaded, true);
'''
    subprocess.run([node, '-e', harness + '(function() {' + handler + '})();' + checks], check=True)
