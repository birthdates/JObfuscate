import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const candidates = [
  path.resolve(__dirname, '../../build/core/Release/obfuscator_cli.exe'),
  path.resolve(__dirname, '../../build/core/obfuscator_cli.exe'),
  path.resolve(__dirname, '../../build/core/Release/obfuscator_cli'),
  path.resolve(__dirname, '../../build/core/obfuscator_cli'),
  path.resolve(__dirname, '../../bin/obfuscator_cli.exe'),
  path.resolve(__dirname, '../../bin/obfuscator_cli'),
];

const destDir = path.resolve(__dirname, '../src-tauri/bin');
if (!fs.existsSync(destDir)) {
  fs.mkdirSync(destDir, { recursive: true });
}

let found = false;
for (const src of candidates) {
  if (fs.existsSync(src)) {
    const filename = path.basename(src);
    const dest = path.join(destDir, filename);
    fs.copyFileSync(src, dest);
    try {
      fs.chmodSync(dest, 0o755);
    } catch {}
    console.log(`[copy-cli] Successfully copied ${src} -> ${dest}`);
    found = true;
    break;
  }
}

if (!found) {
  console.warn('[copy-cli] Warning: obfuscator_cli binary not found in build directory. Make sure to build the C++ project first.');
}
