#!/usr/bin/env node
// Run gSender's real detector against an actual-AVR $I transcript. No npm install.
// Usage: node tools/test_gsender_detection.cjs <gsender-checkout> <transcript>
const assert = require('node:assert/strict');
const { EventEmitter } = require('node:events');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const crypto = require('node:crypto');

assert.equal(process.argv.length, 4, 'provide gSender checkout and AVR transcript');
const sourcePath = path.join(process.argv[2], 'src/server/lib/Connection.js');
const original = fs.readFileSync(sourcePath, 'utf8');
const transcript = fs.readFileSync(process.argv[3], 'utf8');
assert(!transcript.includes("['$' for help]"), 'test must not rely on startup banner');
assert(transcript.includes('[VER:1.2i.'), 'test must retain actual upstream version');
assert(transcript.endsWith('ok\r\n'), 'incomplete AVR transcript');

// Stub external dependencies, retaining the complete Connection class unchanged.
const context = vm.createContext({
  EventEmitter, SerialConnection: class {}, GRBL: 'Grbl', GRBLHAL: 'grblHAL',
  GRBL_REALTIME_COMMANDS: {}, GRBLHAL_REALTIME_COMMANDS: {},
  WRITE_SOURCE_CLIENT: 'client', noop() {}, includes: (array, value) => array.includes(value),
  logger: () => new Proxy({}, { get: () => () => {} }),
  setInterval, clearInterval, setTimeout, clearTimeout,
});
const source = original.replace(/^import[\s\S]*?;\n/gm, '')
  .replace('export default Connection;', 'globalThis.Connection = Connection;');
vm.runInContext(source, context, { filename: sourcePath });

function detect(reply, fallback = false) {
  const connection = new context.Connection({}, '/dev/fake', {
    baudrate: 115200, defaultFirmware: 'grblHAL',
  }, () => {});
  const found = [];
  connection.on('firmwareFound', type => found.push(type));
  for (const line of reply.split('\r\n').filter(Boolean)) {
    connection.connectionEventListener.data(line);
  }
  if (fallback && !found.length) connection.useDefaultFirmware();
  return found;
}

assert.deepEqual(detect(transcript), ['Grbl']);
console.log('PASS: real gSender Connection identifies AVR $I as Grbl, despite grblHAL default and no startup banner');
const oldReply = transcript.replace('[MSG:Grbl-Mega-5X RF2000]\r\n', '');
assert.deepEqual(detect(oldReply), []);
assert.deepEqual(detect(oldReply, true), ['grblHAL']);
console.log('PASS: negative control reproduces old reply falling back to grblHAL');
console.log(`gSender Connection.js SHA256 ${crypto.createHash('sha256').update(original).digest('hex')}`);
console.log(`AVR transcript SHA256 ${crypto.createHash('sha256').update(transcript).digest('hex')}`);
