#!/usr/bin/env node
// Automated Test Harness for Java Bytecode Obfuscator
// Tests compilation, all obfuscation passes, runtime execution, bytecode matrix (Java 8-25), and secrecy verification.

const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const { spawnSync } = require('node:child_process');

const isWindows = process.platform === 'win32';
const scriptDir = __dirname;
const rootDir = path.resolve(scriptDir, '..');

// Colors
const colors = {
  reset: '\x1b[0m',
  cyan: '\x1b[36m',
  yellow: '\x1b[33m',
  green: '\x1b[32m',
  red: '\x1b[31m',
  gray: '\x1b[90m',
  darkCyan: '\x1b[36m',
  white: '\x1b[37m',
};

function logColor(color, message) {
  console.log(`${color}${message}${colors.reset}`);
}

logColor(colors.cyan, '============================================================');
logColor(colors.cyan, `     Java Obfuscator Automated Test Suite (${isWindows ? 'Windows' : 'POSIX'})        `);
logColor(colors.cyan, '============================================================');

// 1. Locate Java, Javac & Jar
function findTool(toolName) {
  const ext = isWindows ? '.exe' : '';
  const fullName = `${toolName}${ext}`;

  // 1. JAVA_HOME
  if (process.env.JAVA_HOME) {
    const candidate = path.join(process.env.JAVA_HOME, 'bin', fullName);
    if (fs.existsSync(candidate)) return candidate;
  }

  // 2. Known Windows paths
  if (isWindows) {
    const defaultJdk = path.join('C:\\Program Files\\Microsoft\\jdk-25.0.4.101-hotspot\\bin', fullName);
    if (fs.existsSync(defaultJdk)) return defaultJdk;
  }

  // 3. System PATH lookup
  const lookupCmd = isWindows ? 'where.exe' : 'which';
  const res = spawnSync(lookupCmd, [fullName], { encoding: 'utf-8', shell: true });
  if (res.status === 0 && res.stdout) {
    const firstLine = res.stdout.trim().split(/\r?\n/)[0];
    if (firstLine && fs.existsSync(firstLine)) return firstLine;
  }

  return null;
}

const javaBin = findTool('java');
const javacBin = findTool('javac');
const jarBin = findTool('jar');
const javapBin = findTool('javap');

if (!javaBin || !javacBin || !jarBin) {
  console.error(`${colors.red}[ERROR] Java development tools (java, javac, jar) not found! Please set JAVA_HOME.${colors.reset}`);
  process.exit(1);
}

logColor(colors.gray, `[SETUP] Using Java: ${javaBin}`);
const javaVerRes = spawnSync(javaBin, ['-version'], { encoding: 'utf-8' });
console.log(javaVerRes.stderr || javaVerRes.stdout);

// 2. Locate Obfuscator CLI
const cliCandidates = [
  path.join(rootDir, 'build', 'core', 'Release', isWindows ? 'obfuscator_cli.exe' : 'obfuscator_cli'),
  path.join(rootDir, 'build', 'core', isWindows ? 'obfuscator_cli.exe' : 'obfuscator_cli'),
  path.join(rootDir, 'bin', isWindows ? 'obfuscator_cli.exe' : 'obfuscator_cli'),
];

let cliBin = null;
for (const candidate of cliCandidates) {
  if (fs.existsSync(candidate)) {
    cliBin = candidate;
    break;
  }
}

if (!cliBin) {
  console.error(`${colors.red}[ERROR] Obfuscator CLI binary not found. Please build the C++ project first.${colors.reset}`);
  process.exit(1);
}
logColor(colors.gray, `[SETUP] Using Obfuscator CLI: ${cliBin}`);

// 3. Setup Test Output Directory
const testJarsDir = path.join(rootDir, 'build', 'test_jars');
if (fs.existsSync(testJarsDir)) {
  try {
    fs.rmSync(testJarsDir, { recursive: true, force: true });
  } catch {
    // Ignore busy files if any
  }
}
fs.mkdirSync(testJarsDir, { recursive: true });
logColor(colors.gray, `[SETUP] Test JARs directory: ${testJarsDir}`);

// Helper: recursively collect files
function collectFiles(dir, filterFn) {
  let results = [];
  const entries = fs.readdirSync(dir, { withFileTypes: true });
  for (const entry of entries) {
    const fullPath = path.join(dir, entry.name);
    if (entry.isDirectory()) {
      results = results.concat(collectFiles(fullPath, filterFn));
    } else if (filterFn(fullPath)) {
      results.push(fullPath);
    }
  }
  return results;
}

// 4. Compile Sample Java Application
console.log('');
logColor(colors.yellow, '[STEP 1] Compiling Sample Java Application...');
const sampleAppDir = path.join(rootDir, 'sample_app');
const classesDir = path.join(sampleAppDir, 'build', 'classes');
if (fs.existsSync(classesDir)) {
  fs.rmSync(classesDir, { recursive: true, force: true });
}
fs.mkdirSync(classesDir, { recursive: true });

const javaSources = collectFiles(path.join(sampleAppDir, 'src', 'main', 'java'), (f) => f.endsWith('.java'));
const javacRes = spawnSync(javacBin, ['-d', classesDir, ...javaSources], { encoding: 'utf-8' });
if (javacRes.status !== 0) {
  console.error(`${colors.red}[ERROR] Failed to compile sample Java application:\n${javacRes.stderr || javacRes.stdout}${colors.reset}`);
  process.exit(1);
}

const inputJar = path.join(testJarsDir, 'test_sample_input.jar');
const manifest = path.join(sampleAppDir, 'META-INF', 'MANIFEST.MF');
const jarRes = spawnSync(jarBin, ['-cfm', inputJar, manifest, '-C', classesDir, '.'], { encoding: 'utf-8' });
if (jarRes.status !== 0) {
  console.error(`${colors.red}[ERROR] Failed to package test input JAR:\n${jarRes.stderr || jarRes.stdout}${colors.reset}`);
  process.exit(1);
}
logColor(colors.green, `Created input test archive: ${inputJar}`);

let totalTests = 0;
let passedTests = 0;

function runTestCase(testName, fn) {
  totalTests++;
  console.log('');
  logColor(colors.gray, '------------------------------------------------------------');
  logColor(colors.yellow, `TEST: ${testName}`);
  try {
    fn();
    logColor(colors.green, `-> PASSED: ${testName}`);
    passedTests++;
  } catch (err) {
    logColor(colors.red, `-> FAILED: ${testName}`);
    logColor(colors.red, `   Error: ${err.message || err}`);
  }
}

function runCli(args) {
  const res = spawnSync(cliBin, args, { encoding: 'utf-8' });
  if (res.status !== 0) {
    throw new Error(`Obfuscator CLI failed with exit code ${res.status}:\n${res.stderr || res.stdout}`);
  }
  return res.stdout || '';
}

function runJavaJar(jarPath) {
  const res = spawnSync(javaBin, ['-jar', jarPath], { encoding: 'utf-8' });
  const output = (res.stdout || '') + '\n' + (res.stderr || '');
  if (res.status !== 0) {
    throw new Error(`Obfuscated JAR failed execution with exit code ${res.status}:\n${output}`);
  }
  return output;
}

function getJarEntries(jarPath) {
  const res = spawnSync(jarBin, ['-tf', jarPath], { encoding: 'utf-8' });
  if (res.status !== 0) {
    throw new Error(`Failed to read JAR entries from ${jarPath}`);
  }
  return (res.stdout || '').split(/\r?\n/).filter((l) => l.trim().length > 0);
}

function disassembleClass(jarPath, className) {
  if (!javapBin) return '';
  const res = spawnSync(javapBin, ['-p', '-cp', jarPath, className], { encoding: 'utf-8' });
  return (res.stdout || '') + '\n' + (res.stderr || '');
}

// TEST 1: Full Obfuscation (All Passes Enabled)
runTestCase('Full Obfuscation (All Passes) & Execution Verification', () => {
  const outJar = path.join(testJarsDir, 'test_sample_all_obf.jar');
  if (fs.existsSync(outJar)) fs.unlinkSync(outJar);

  runCli(['-i', inputJar, '-o', outJar]);
  if (!fs.existsSync(outJar)) throw new Error('Output JAR was not generated.');

  const javaOutput = runJavaJar(outJar);
  console.log(javaOutput);
  if (!javaOutput.includes('TEST SUITES PASSED SUCCESSFULLY!')) {
    throw new Error('Verification signature not found in program output!');
  }
});

// TEST 2: Secrecy & String Erasure Verification
runTestCase('Secrecy Check (Plaintext secrets & indy recipes erased, randomized decryptor)', () => {
  const outJar = path.join(testJarsDir, 'test_sample_all_obf.jar');
  const content = fs.readFileSync(outJar, 'latin1');

  const forbiddenSecrets = [
    'AKIA-8921-TOP-SECRET-KEY-2025',
    'jdbc:postgresql://db.production.internal:5432/secure_vault',
    '=== PROTECTED SYSTEM ACCESS ONLY ===',
    '[OBFUSCATOR-VERIFICATION-PASS]',
  ];

  for (const secret of forbiddenSecrets) {
    if (content.includes(secret)) {
      throw new Error(`Plaintext secret or concat recipe leak detected in obfuscated JAR: '${secret}'`);
    }
  }

  // Verify randomized decryptor name is used instead of static "_decrypt"
  if (content.includes('_decrypt')) {
    throw new Error("Static '_decrypt' method name detected! Expected randomized confusing decryptor identifier.");
  }

  logColor(colors.darkCyan, '   All secrets, indy recipes, and static decryptor names confirmed absent from archive.');
});

// TEST 3: Selective Pass Toggles
runTestCase('Selective Pass Toggle (--no-control-flow)', () => {
  const outJar = path.join(testJarsDir, 'test_sample_no_cf.jar');
  runCli(['-i', inputJar, '-o', outJar, '--no-control-flow']);
  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) throw new Error('Failed execution with --no-control-flow');
});

runTestCase('Selective Pass Toggle (--no-strings)', () => {
  const outJar = path.join(testJarsDir, 'test_sample_no_strings.jar');
  runCli(['-i', inputJar, '-o', outJar, '--no-strings']);
  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) throw new Error('Failed execution with --no-strings');
});

runTestCase('Fast Rolling XOR String Algorithm (--string-algo xor)', () => {
  const outJar = path.join(testJarsDir, 'test_sample_string_xor.jar');
  runCli(['-i', inputJar, '-o', outJar, '--string-algo', 'xor']);
  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) throw new Error('Failed execution with --string-algo xor');
});

runTestCase('Stack-Split String Keys (--split-keys / --no-split-keys)', () => {
  const splitJar = path.join(testJarsDir, 'test_sample_split_keys.jar');
  const plainJar = path.join(testJarsDir, 'test_sample_single_keys.jar');
  const baseArgs = ['--no-rename', '--no-control-flow', '--no-number-obfuscation', '--no-reflection'];
  runCli(['-i', inputJar, '-o', splitJar, '--split-keys', ...baseArgs]);
  runCli(['-i', inputJar, '-o', plainJar, '--no-split-keys', ...baseArgs]);

  // Both variants must execute correctly
  const outSplit = runJavaJar(splitJar);
  if (!outSplit.includes('TEST SUITES PASSED')) throw new Error('Failed execution with --split-keys');
  const outPlain = runJavaJar(plainJar);
  if (!outPlain.includes('TEST SUITES PASSED')) throw new Error('Failed execution with --no-split-keys');

  // Bytecode shape check: in split mode every decryptor call site
  //   ldc <ciphertext> ... ixor ... invokestatic (String,II)String
  // must build its key with ixor; in single-key mode no call site may.
  const countGuardedSites = (jarPath) => {
    const entries = getJarEntries(jarPath).filter((e) => e.endsWith('.class'));
    let guarded = 0;
    let total = 0;
    for (const entry of entries) {
      const cls = entry.replace(/\//g, '.').replace(/\.class$/, '');
      const res = spawnSync(javapBin, ['-c', '-p', '-cp', jarPath, cls], { encoding: 'utf-8' });
      const text = res.stdout || '';
      // Split into per-method chunks to avoid cross-method contamination
      const chunks = text.split(/\r?\n(?=  \w.*\(.*\)\s*;?\s*$)/m);
      for (const chunk of chunks) {
        const lines = chunk.split(/\r?\n/);
        let pending = false;
        let sawIxor = false;
        for (const line of lines) {
          if (/ldc\s+#\d+\s+\/\/ String/.test(line)) {
            pending = true;
            sawIxor = false;
          } else if (pending && /(^|\s)ixor(\s|$)/.test(line)) {
            sawIxor = true;
          } else if (pending && /invokestatic.*\(Ljava\/lang\/String;II\)/.test(line)) {
            total++;
            if (sawIxor) guarded++;
            pending = false;
            sawIxor = false;
          }
        }
      }
    }
    return { guarded, total };
  };

  const split = countGuardedSites(splitJar);
  const plain = countGuardedSites(plainJar);
  if (split.total === 0) throw new Error('No decryptor call sites found in --split-keys JAR!');
  if (split.guarded !== split.total) {
    throw new Error(`Only ${split.guarded}/${split.total} call sites use split keys in --split-keys JAR!`);
  }
  if (plain.guarded !== 0) {
    throw new Error(`Found ${plain.guarded} split-key call sites in --no-split-keys JAR!`);
  }
  logColor(colors.darkCyan, `   Split-key call sites: ${split.guarded}/${split.total} (split) vs ${plain.guarded}/${plain.total} (single).`);
});

runTestCase('Selective Pass Toggle (--no-rename)', () => {
  const outJar = path.join(testJarsDir, 'test_sample_no_rename.jar');
  runCli(['-i', inputJar, '-o', outJar, '--no-rename']);
  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) throw new Error('Failed execution with --no-rename');
});

runTestCase('Selective Pass Toggle (--no-number-obfuscation)', () => {
  const outJar = path.join(testJarsDir, 'test_sample_no_num.jar');
  runCli(['-i', inputJar, '-o', outJar, '--no-number-obfuscation']);
  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) throw new Error('Failed execution with --no-number-obfuscation');
});

runTestCase('Selective Pass Toggle (--no-reflection)', () => {
  const outJar = path.join(testJarsDir, 'test_sample_no_refl.jar');
  runCli(['-i', inputJar, '-o', outJar, '--no-reflection']);
  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) throw new Error('Failed execution with --no-reflection');
});

// TEST 4: Package Scoping Filter (-p / --packages)
runTestCase('Scoped Obfuscation (-p com/testapp/logic)', () => {
  const outJar = path.join(testJarsDir, 'test_sample_scoped.jar');
  runCli(['-i', inputJar, '-o', outJar, '-p', 'com/testapp/logic']);
  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) throw new Error('Failed execution with scoped package filter');
});

// TEST 5: Class & Package Scrambling (--rename-classes --rename-packages)
runTestCase('Class & Package Scrambling (--rename-classes --rename-packages)', () => {
  const outJar = path.join(testJarsDir, 'test_sample_renamed_all.jar');
  runCli(['-i', inputJar, '-o', outJar, '--rename-classes', '--rename-packages']);
  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) throw new Error('Failed execution with scrambled classes and packages');
});

// TEST 6: Package Unnesting / Root Flattening (--remove-packages -p 'com.testapp.*')
runTestCase("Package Unnesting / Root Flattening (--remove-packages -p 'com.testapp.*')", () => {
  const outJar = path.join(testJarsDir, 'test_sample_unnested.jar');
  runCli(['-i', inputJar, '-o', outJar, '-p', 'com.testapp.*', '--remove-packages']);

  const entries = getJarEntries(outJar);
  const hasCom = entries.some((e) => e.startsWith('com/'));
  const rootClassCount = entries.filter((e) => e.endsWith('.class') && !e.includes('/')).length;

  if (hasCom) throw new Error("Found 'com/' directory in unnested archive: expected classes at root package only!");
  if (rootClassCount === 0) throw new Error('No root-level .class files found in unnested archive!');

  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) throw new Error('Failed execution with unnested packages');
  logColor(colors.darkCyan, `   Archive unnested into default package: ${rootClassCount} root classes, 0 package directories.`);
});

// TEST 7: Scoped Package Prefix Preservation (-p 'com.testapp.*' --rename-packages)
runTestCase("Scoped Package Prefix Preservation (-p 'com.testapp.*' --rename-packages)", () => {
  const outJar = path.join(testJarsDir, 'test_sample_prefix_preserved.jar');
  runCli(['-i', inputJar, '-o', outJar, '-p', 'com.testapp.*', '--rename-packages']);

  const entries = getJarEntries(outJar);
  const hasComPrefix = entries.some((e) => e.startsWith('com/'));
  const hasTestapp = entries.some((e) => e.includes('/testapp/'));

  if (!hasComPrefix) throw new Error("Expected preserved 'com/' prefix in archive entries!");
  if (hasTestapp) throw new Error("Unscrambled 'testapp' package found in archive entries: expected package to be scrambled!");

  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) throw new Error('Failed execution with preserved package prefix');
  logColor(colors.darkCyan, "   'com/' preserved, 'testapp' scrambled into randomized package paths.");
});

// TEST 8: Root Wildcard Package Scrambling (-p 'com.*' --rename-packages)
runTestCase("Root Wildcard Package Scrambling (-p 'com.*' --rename-packages)", () => {
  const outJar = path.join(testJarsDir, 'test_sample_root_scrambled.jar');
  runCli(['-i', inputJar, '-o', outJar, '-p', 'com.*', '--rename-packages']);

  const entries = getJarEntries(outJar);
  const hasCom = entries.some((e) => e.startsWith('com/'));
  if (hasCom) throw new Error("Found 'com/' directory in root-scrambled archive: expected com itself to be scrambled!");

  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) throw new Error('Failed execution with root wildcard package scrambling');
  logColor(colors.darkCyan, "   Root 'com/' package scrambled into randomized top-level directories.");
});

// TEST 9: Reflection Proxying with Wildcard Scope (--import-java --import-packages 'com.testapp.*')
runTestCase("Reflection Proxying with Wildcard Scope (--import-java --import-packages 'com.testapp.*')", () => {
  const outJar = path.join(testJarsDir, 'test_sample_refl_java.jar');
  runCli(['-i', inputJar, '-o', outJar, '--import-java', '--import-packages', 'com.testapp.*']);
  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) throw new Error('Failed execution with --import-java --import-packages');

  // Proxies must dispatch via MethodHandle.invokeExact (fast path), not Method.invoke.
  // Resolution (publicLookup/find*) must live only in shared helpers inside the
  // global cache class (the Object[] holder), never inline in proxies:
  // constructor proxies intentionally stay on Constructor.newInstance, and the
  // sample app itself legitimately calls Method.invoke in its reflection test.
  const entries = getJarEntries(outJar).filter((e) => e.endsWith('.class'));
  let exactCount = 0;
  let proxyCount = 0;
  let helperMethods = 0;
  let helperGetters = 0;
  for (const entry of entries) {
    const cls = entry.replace(/\//g, '.').replace(/\.class$/, '');
    const res = spawnSync(javapBin, ['-c', '-p', '-cp', outJar, cls], { encoding: 'utf-8' });
    const text = res.stdout || '';
    const chunks = text.split(/\r?\n(?=  \w.*\(.*\)\s*;?\s*$)/m);
    const isCacheClass = /final java\.lang\.Object\[\]/.test(text);
    for (const chunk of chunks) {
      if (!chunk.includes('invokeExact') && !chunk.includes('publicLookup') && !chunk.includes('findStaticGetter')) continue;
      if (isCacheClass) {
        // Shared lookup helper bodies.
        if (chunk.includes('MethodType.methodType')) helperMethods++;
        if (chunk.includes('findStaticGetter')) helperGetters++;
        continue;
      }
      if (!chunk.includes('invokeExact')) continue;
      proxyCount++;
      if (chunk.includes('publicLookup') || chunk.includes('findVirtual') ||
          chunk.includes('findStatic') || chunk.includes('MethodType.methodType') ||
          /reflect\/Method[^:]*\.invoke\s*:/.test(chunk)) {
        throw new Error(`Inline resolution dispatch found in proxy (${entry})!`);
      }
      const m = chunk.match(/invokeExact/g);
      if (m) exactCount += m.length;
    }
  }
  if (proxyCount === 0) throw new Error('No generated MethodHandle proxies found in proxied JAR!');
  if (exactCount === 0) throw new Error('No invokeExact dispatches found in proxied JAR!');
  if (helperMethods < 2 || helperMethods > 4) {
    throw new Error(`Expected 2-4 shared method lookup helpers in cache class, found ${helperMethods}!`);
  }
  if (helperGetters > 1) {
    throw new Error(`Expected at most 1 shared getter helper in cache class, found ${helperGetters}!`);
  }
  logColor(colors.darkCyan, `   MethodHandle proxies: ${proxyCount}, invokeExact: ${exactCount}, shared helpers: ${helperMethods} method + ${helperGetters} getter, Method.invoke: 0.`);
});

// TEST 10: Enum Constant Obfuscation (--obfuscate-enums)
runTestCase('Enum Constant Obfuscation (--obfuscate-enums)', () => {
  const outJar = path.join(testJarsDir, 'test_sample_enums.jar');
  runCli(['-i', inputJar, '-o', outJar, '--obfuscate-enums']);
  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) throw new Error('Failed execution with --obfuscate-enums');
  logColor(colors.darkCyan, '   Enum constants scrambled and runtime values verified.');
});

// TEST 11: Access Reducer Pass (--reduce-access)
runTestCase('Access Reducer Pass (--reduce-access)', () => {
  const outJar = path.join(testJarsDir, 'test_sample_access.jar');
  runCli(['-i', inputJar, '-o', outJar, '--reduce-access']);
  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) throw new Error('Failed execution with --reduce-access');
  logColor(colors.darkCyan, '   Unreferenced members privatized and execution verified.');
});

// TEST 11b: Disable Access Reduction (--disable-access-reduction)
runTestCase('Disable Access Reduction (--disable-access-reduction)', () => {
  const outJar = path.join(testJarsDir, 'test_sample_no_access.jar');
  runCli(['-i', inputJar, '-o', outJar, '--disable-access-reduction']);
  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) throw new Error('Failed execution with --disable-access-reduction');
  logColor(colors.darkCyan, '   Access reduction disabled and verified.');
});

// TEST 12: Enterprise Ultimate Combination (All Passes + Enums + Access + Unnesting)
runTestCase('Enterprise Ultimate Combination (All Passes + Enums + Access + Unnesting)', () => {
  const outJar = path.join(testJarsDir, 'test_sample_enterprise_ultimate.jar');
  runCli(['-i', inputJar, '-o', outJar, '--obfuscate-enums', '--reduce-access', '--remove-packages', 'com.testapp.*']);
  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) throw new Error('Failed execution with enterprise ultimate options');
  logColor(colors.darkCyan, '   All advanced protection passes executed and verified in unison.');
});

// TEST 13: Package Scoping with Wildcard Pattern (-p 'com.testapp*')
runTestCase("Package Scoping with Wildcard Pattern (-p 'com.testapp*')", () => {
  const outJar = path.join(testJarsDir, 'test_sample_wildcard_no_dot.jar');
  runCli(['-i', inputJar, '-o', outJar, '-p', 'com.testapp*']);
  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) throw new Error("Failed execution with -p 'com.testapp*'");
  logColor(colors.darkCyan, "   Scoped 'com.testapp*' successfully targeted and verified.");
});

// TEST 14: Explicit Package Exclusion (-x 'com.testapp.logic.*')
runTestCase("Explicit Package Exclusion (-x 'com.testapp.logic.*')", () => {
  const outJar = path.join(testJarsDir, 'test_sample_excluded_pkg.jar');
  runCli(['-i', inputJar, '-o', outJar, '-x', 'com.testapp.logic.*', '--rename-packages']);

  const entries = getJarEntries(outJar);
  const hasLogicExternal = entries.some(
    (e) => e === 'com/testapp/logic/ExternalCaller.class' || e === 'com/testapp/logic/FlowLogic.class'
  );
  if (!hasLogicExternal) {
    throw new Error('Expected com/testapp/logic classes to remain untouched when excluded via -x!');
  }

  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) throw new Error('Failed execution with excluded package');
  logColor(colors.darkCyan, '   Package exclusion verified: com.testapp.logic.* preserved intact.');
});

// TEST 15: Shaded Dependency Ignoring (--ignore-dependencies)
runTestCase('Shaded Dependency Ignoring (--ignore-dependencies)', () => {
  const outJar = path.join(testJarsDir, 'test_sample_ignore_deps.jar');
  runCli(['-i', inputJar, '-o', outJar, '--ignore-dependencies']);
  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) throw new Error('Failed execution with --ignore-dependencies');
  logColor(colors.darkCyan, '   Shaded dependency ignoring verified.');
});

// TEST 16-20: Multi-Target Java Bytecode Compatibility (Java 8, 11, 17, 21, 25)
const targetVersions = [8, 11, 17, 21, 25];
for (const targetVer of targetVersions) {
  runTestCase(`Bytecode Version Compatibility (Target Java ${targetVer})`, () => {
    const verClassesDir = path.join(sampleAppDir, 'build', `classes_j${targetVer}`);
    if (fs.existsSync(verClassesDir)) {
      fs.rmSync(verClassesDir, { recursive: true, force: true });
    }
    fs.mkdirSync(verClassesDir, { recursive: true });

    // Filter version-specific language showcase sources for target release
    const filteredSources = javaSources.filter((src) => {
      if (targetVer < 11 && src.endsWith('Java11.java')) return false;
      if (targetVer < 17 && src.endsWith('Java17.java')) return false;
      if (targetVer < 21 && src.endsWith('Java21.java')) return false;
      if (targetVer < 25 && src.endsWith('Java25.java')) return false;
      return true;
    });

    const compRes = spawnSync(javacBin, ['-Xlint:-options', '--release', String(targetVer), '-d', verClassesDir, ...filteredSources], { encoding: 'utf-8' });
    if (compRes.status !== 0) {
      throw new Error(`Failed to compile for target Java ${targetVer}:\n${compRes.stderr || compRes.stdout}`);
    }

    const verInputJar = path.join(testJarsDir, `test_sample_j${targetVer}.jar`);
    const verOutJar = path.join(testJarsDir, `test_sample_j${targetVer}_obf.jar`);

    const pRes = spawnSync(jarBin, ['-cfm', verInputJar, manifest, '-C', verClassesDir, '.'], { encoding: 'utf-8' });
    if (pRes.status !== 0) {
      throw new Error(`Failed to package JAR for Java ${targetVer}:\n${pRes.stderr || pRes.stdout}`);
    }

    runCli(['-i', verInputJar, '-o', verOutJar]);
    const out = runJavaJar(verOutJar);
    if (!out.includes('TEST SUITES PASSED')) {
      throw new Error(`Execution failed for obfuscated Java ${targetVer} bytecode`);
    }
  });
}

// TEST 21 (Total test 26): NDJSON Streaming Protocol for GUI/IPC
runTestCase('NDJSON Streaming Protocol (--json)', () => {
  const outJar = path.join(testJarsDir, 'test_sample_json.jar');
  const res = spawnSync(cliBin, ['-i', inputJar, '-o', outJar, '--json'], { encoding: 'utf-8' });
  if (res.status !== 0) {
    throw new Error(`Obfuscator failed with --json:\n${res.stderr || res.stdout}`);
  }

  const lines = (res.stdout || '').split(/\r?\n/).filter((l) => l.trim().length > 0);
  let foundProgress = false;
  let foundComplete = false;

  for (const line of lines) {
    try {
      const obj = JSON.parse(line);
      if (obj.type === 'progress') foundProgress = true;
      if (obj.type === 'complete' && obj.status === 'success') foundComplete = true;
    } catch {}
  }

  if (!foundProgress) throw new Error("Did not receive 'progress' events in NDJSON stream.");
  if (!foundComplete) throw new Error("Did not receive 'complete' event in NDJSON stream.");
  logColor(colors.darkCyan, '   NDJSON stream parsed successfully with full event contract.');
});

// TEST 22: InvokeDynamic Call Site Indirection (--indy-calls)
runTestCase('InvokeDynamic Call Site Indirection (--indy-calls)', () => {
  const outJar = path.join(testJarsDir, 'test_sample_indy.jar');
  runCli(['-i', inputJar, '-o', outJar, '--indy-calls']);
  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) throw new Error('Failed execution with --indy-calls');
  logColor(colors.darkCyan, '   Direct invocations scrambled into invokedynamic with runtime BSM caching.');
});

// TEST 23: True Control Flow Flattening & Decompiler Traps (--flatten-control-flow --decompiler-traps)
runTestCase('True Control Flow Flattening & Decompiler Traps (--flatten-control-flow --decompiler-traps)', () => {
  const outJar = path.join(testJarsDir, 'test_sample_flatten_traps.jar');
  runCli(['-i', inputJar, '-o', outJar, '--flatten-control-flow', '--decompiler-traps', '--flattening-intensity', '100']);
  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) throw new Error('Failed execution with --flatten-control-flow and --decompiler-traps');
  logColor(colors.darkCyan, '   Chenxi Wang state machine switch & irreducible loop CFGs verified.');
});

// TEST 23b: MBA Opaque Predicates (mixed boolean-arithmetic identities)
runTestCase('MBA Opaque Predicates (mixed boolean-arithmetic identities)', () => {
  const outJar = path.join(testJarsDir, 'test_sample_mba.jar');
  // Isolate control flow: nothing else in this configuration emits isub.
  runCli(['-i', inputJar, '-o', outJar, '--no-rename', '--no-strings', '--no-reflection', '--no-number-obfuscation']);
  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) throw new Error('Failed execution with MBA predicates active');

  // V7 fingerprint: ixor ... ior ... swap ... iand ... isub with only
  // pushes/dups/swaps (plus the runtime-int prologue) between. Absent from
  // unobfuscated javac output (verified zero hits on the input JAR).
  const FILLER = new Set(['bipush', 'sipush', 'ldc', 'ldc_w',
    'iconst_m1', 'iconst_0', 'iconst_1', 'iconst_2', 'iconst_3', 'iconst_4', 'iconst_5',
    'dup', 'swap', 'invokestatic', 'l2i']);
  const NEED = ['ixor', 'ior', 'swap', 'iand', 'isub'];
  const entries = getJarEntries(outJar).filter((e) => e.endsWith('.class'));
  let hits = 0;
  for (const entry of entries) {
    const cls = entry.replace(/\//g, '.').replace(/\.class$/, '');
    const res = spawnSync(javapBin, ['-c', '-p', '-cp', outJar, cls], { encoding: 'utf-8' });
    const ops = (res.stdout || '').split(/\r?\n/)
      .map((l) => { const m = l.match(/^\s*\d+: (\w+)/); return m ? m[1] : null; })
      .filter(Boolean);
    let stage = 0;
    for (const op of ops) {
      if (op === NEED[stage]) {
        if (++stage === NEED.length) {
          hits++;
          stage = 0;
        }
      } else if (!FILLER.has(op)) {
        stage = (op === NEED[0]) ? 1 : 0;
      }
    }
  }
  if (hits === 0) throw new Error('No MBA predicate sequence found in obfuscated output!');
  logColor(colors.darkCyan, `   MBA identity sequences emitted: ${hits}, execution verified.`);
});

// TEST 23b: Exception Dispatch (--exception-dispatch / --no-exception-dispatch)
runTestCase('Exception Dispatch (--exception-dispatch / --no-exception-dispatch)', () => {
  const onJar = path.join(testJarsDir, 'test_sample_exc_dispatch.jar');
  const offJar = path.join(testJarsDir, 'test_sample_no_exc_dispatch.jar');
  // Fixed seed so sampling rolls are deterministic across both runs.
  runCli(['-i', inputJar, '-o', onJar, '--no-rename', '--exception-dispatch', '--seed', '42']);
  runCli(['-i', inputJar, '-o', offJar, '--no-rename', '--no-exception-dispatch', '--seed', '42']);

  const outOn = runJavaJar(onJar);
  if (!outOn.includes('TEST SUITES PASSED')) throw new Error('Failed execution with --exception-dispatch');
  const outOff = runJavaJar(offJar);
  if (!outOff.includes('TEST SUITES PASSED')) throw new Error('Failed execution with --no-exception-dispatch');

  // Bytecode check: dispatch must add try/catch handler entries jar-wide.
  const countCatches = (jarPath) => {
    const entries = getJarEntries(jarPath).filter((e) => e.endsWith('.class'));
    let catches = 0;
    for (const entry of entries) {
      const cls = entry.replace(/\//g, '.').replace(/\.class$/, '');
      const res = spawnSync(javapBin, ['-v', '-p', '-cp', jarPath, cls], { encoding: 'utf-8' });
      const text = res.stdout || '';
      const m = text.match(/^\s*\d+\s+\d+\s+\d+\s+Class java\/lang\/\S+Exception/mg);
      if (m) catches += m.length;
    }
    return catches;
  };

  const onCount = countCatches(onJar);
  const offCount = countCatches(offJar);
  if (onCount <= offCount) {
    throw new Error(`Expected more handler entries with dispatch on (${onCount}) than off (${offCount})!`);
  }
  logColor(colors.darkCyan, `   Exception handler entries: ${onCount} (dispatch) vs ${offCount} (off).`);
});

// TEST 24: Tier 1 Ultimate Fortress (Indy + Flattening + Traps + All Protections)
runTestCase('Tier 1 Ultimate Fortress (All Passes + Indy + Flattening + Traps)', () => {
  const outJar = path.join(testJarsDir, 'test_sample_tier1_fortress.jar');
  runCli(['-i', inputJar, '-o', outJar, '--indy-calls', '--flatten-control-flow', '--decompiler-traps', '--flattening-intensity', '80']);
  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) throw new Error('Failed execution with Tier 1 Ultimate Fortress options');
  logColor(colors.darkCyan, '   Full Tier 1 defense package verified in unison.');
});

// TEST 25: Baseline Dead Code Presence (Non-Shrunk Archive)
runTestCase('Baseline Dead Code Presence Check (Non-Shrunk Archive)', () => {
  const entries = getJarEntries(inputJar);
  const hasDeadClass = entries.some((e) => e.includes('UnusedDeadClass'));
  if (!hasDeadClass) {
    throw new Error('UnusedDeadClass was not found in the input JAR archive!');
  }
  logColor(colors.darkCyan, '   UnusedDeadClass confirmed present in input archive before shrinking.');
});

// TEST 26: Full Code Shrinking & Tree Shaking (--shrink)
runTestCase('Full Code Shrinking & Tree Shaking (--shrink)', () => {
  const nonShrunkJar = path.join(testJarsDir, 'test_sample_no_shrink.jar');
  runCli(['-i', inputJar, '-o', nonShrunkJar, '--no-rename', '--no-strings']);

  const shrunkJar = path.join(testJarsDir, 'test_sample_shrunk.jar');
  runCli(['-i', inputJar, '-o', shrunkJar, '--shrink', '--no-rename', '--no-strings']);

  // 1. Verify dead class is erased
  const entries = getJarEntries(shrunkJar);
  const hasDeadClass = entries.some((e) => e.includes('UnusedDeadClass'));
  if (hasDeadClass) {
    throw new Error('Dead class UnusedDeadClass was NOT removed by --shrink!');
  }

  // 2. Verify dead field & method are pruned from CryptoService
  const javapOut = disassembleClass(shrunkJar, 'com.testapp.service.CryptoService');
  if (javapOut.includes('UNUSED_DEAD_FIELD')) {
    throw new Error('Dead field UNUSED_DEAD_FIELD was NOT removed by --shrink!');
  }
  if (javapOut.includes('unusedDeadCalculation')) {
    throw new Error('Dead method unusedDeadCalculation was NOT removed by --shrink!');
  }

  // 3. Verify file size reduction
  const nonShrunkSize = fs.statSync(nonShrunkJar).size;
  const shrunkSize = fs.statSync(shrunkJar).size;
  if (shrunkSize >= nonShrunkSize) {
    throw new Error(`Expected shrunk JAR size (${shrunkSize} B) to be strictly smaller than non-shrunk JAR (${nonShrunkSize} B)!`);
  }

  // 4. Verify runtime execution
  const out = runJavaJar(shrunkJar);
  if (!out.includes('TEST SUITES PASSED')) {
    throw new Error('Shrunk JAR failed execution verification!');
  }
  logColor(colors.darkCyan, `   Dead classes, methods, and fields pruned successfully! Size reduced from ${nonShrunkSize} B to ${shrunkSize} B (-${nonShrunkSize - shrunkSize} B).`);
});

// TEST 27: Selective Shrinking Toggles (--shrink-classes --no-shrink-methods --no-shrink-fields)
runTestCase('Selective Shrinking Toggles (--shrink-classes --no-shrink-methods --no-shrink-fields)', () => {
  const outJar = path.join(testJarsDir, 'test_sample_selective_shrink.jar');
  runCli(['-i', inputJar, '-o', outJar, '--shrink-classes', '--no-shrink-methods', '--no-shrink-fields', '--no-rename', '--no-strings']);

  const entries = getJarEntries(outJar);
  if (entries.some((e) => e.includes('UnusedDeadClass'))) {
    throw new Error('UnusedDeadClass was not pruned when --shrink-classes was active!');
  }

  const javapOut = disassembleClass(outJar, 'com.testapp.service.CryptoService');
  if (!javapOut.includes('unusedDeadCalculation')) {
    throw new Error('unusedDeadCalculation was pruned even though --no-shrink-methods was specified!');
  }
  if (!javapOut.includes('UNUSED_DEAD_FIELD')) {
    throw new Error('UNUSED_DEAD_FIELD was pruned even though --no-shrink-fields was specified!');
  }

  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) {
    throw new Error('Selective shrink JAR failed execution verification!');
  }
  logColor(colors.darkCyan, '   Class pruning active while method/field pruning successfully preserved.');
});

// TEST 28: Ultimate Fortress with Code Shrinking (All Passes + Shrink + Indy + Traps + Flattening)
runTestCase('Ultimate Fortress with Code Shrinking (All Passes + Shrink + Indy + Traps)', () => {
  const outJar = path.join(testJarsDir, 'test_sample_ultimate_shrunk.jar');
  runCli(['-i', inputJar, '-o', outJar, '--shrink', '--indy-calls', '--flatten-control-flow', '--decompiler-traps', '--flattening-intensity', '70']);

  const entries = getJarEntries(outJar);
  if (entries.some((e) => e.includes('UnusedDeadClass'))) {
    throw new Error('Dead class UnusedDeadClass found in ultimate obfuscated archive!');
  }

  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) {
    throw new Error('Ultimate Fortress with Shrinking failed execution verification!');
  }
  logColor(colors.darkCyan, '   All 9 hardening & protection passes verified in unison with 100% success.');
});

// TEST 28b: Keep Rule Preserves Class Through Shrinking (--keep + --shrink)
runTestCase('Keep Rule Preserves Class Through Shrinking (--keep + --shrink)', () => {
  const outJar = path.join(testJarsDir, 'test_sample_keep_shrink.jar');
  runCli(['-i', inputJar, '-o', outJar, '--shrink', '--no-rename', '--no-strings',
    '--keep', 'class com.testapp.dead.UnusedDeadClass { *; }']);

  // Without the rule this class is pruned (see shrinking tests above).
  const entries = getJarEntries(outJar);
  if (!entries.some((e) => e.includes('UnusedDeadClass'))) {
    throw new Error('Kept class UnusedDeadClass was pruned by --shrink despite --keep!');
  }

  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) {
    throw new Error('Keep+shrink JAR failed execution verification!');
  }
  logColor(colors.darkCyan, '   UnusedDeadClass survived tree-shaking via --keep and executes.');
});

// TEST 28c: Keep Rule Preserves Names Through Renaming (--keep)
runTestCase('Keep Rule Preserves Names Through Renaming (--keep)', () => {
  const outJar = path.join(testJarsDir, 'test_sample_keep_rename.jar');
  runCli(['-i', inputJar, '-o', outJar,
    '--keep', 'class com.testapp.service.CryptoService { getBanner; }']);

  const entries = getJarEntries(outJar);
  if (!entries.includes('com/testapp/service/CryptoService.class')) {
    throw new Error('Kept class com/testapp/service/CryptoService was renamed despite --keep!');
  }

  const javapOut = disassembleClass(outJar, 'com.testapp.service.CryptoService');
  if (!javapOut.includes('getBanner')) {
    throw new Error('Kept method getBanner was renamed despite --keep!');
  }

  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) {
    throw new Error('Keep+rename JAR failed execution verification!');
  }
  logColor(colors.darkCyan, '   CryptoService class and getBanner method names preserved, execution verified.');
});

// TEST 28d: Keepclassmembers Renames Class but Keeps Member (--keepclassmembers)
runTestCase('Keepclassmembers Renames Class but Keeps Member (--keepclassmembers)', () => {
  const outJar = path.join(testJarsDir, 'test_sample_keep_members.jar');
  const mappingFile = path.join(testJarsDir, 'test_keep_members_mapping.txt');
  if (fs.existsSync(mappingFile)) fs.unlinkSync(mappingFile);
  runCli(['-i', inputJar, '-o', outJar, '-m', mappingFile,
    '--keepclassmembers', 'class com.testapp.service.CryptoService { getBanner; }']);

  const entries = getJarEntries(outJar);
  if (entries.includes('com/testapp/service/CryptoService.class')) {
    throw new Error('Class was NOT renamed despite only --keepclassmembers (class should scramble)!');
  }

  // Resolve the scrambled CryptoService via mapping.txt and check the member.
  const mapping = fs.readFileSync(mappingFile, 'utf-8');
  const cryptoMatch = mapping.match(/com\.testapp\.service\.CryptoService -> ([^:]+):/);
  if (!cryptoMatch) throw new Error('CryptoService missing from mapping.txt!');
  const obfCrypto = cryptoMatch[1].trim();
  const javapOut = disassembleClass(outJar, obfCrypto);
  if (!javapOut.includes('getBanner')) {
    throw new Error('Member getBanner was renamed despite --keepclassmembers!');
  }

  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) {
    throw new Error('Keepclassmembers JAR failed execution verification!');
  }
  logColor(colors.darkCyan, `   Class scrambled to ${obfCrypto}, getBanner preserved, execution verified.`);
});

// TEST 28e: Keep File With Multiple Rules (--keep-file)
runTestCase('Keep File With Multiple Rules (--keep-file)', () => {
  const keepFile = path.join(testJarsDir, 'test_keep_rules.pro');
  fs.writeFileSync(keepFile, [
    '# test keep file: comments and multi-line braces',
    '-keep class com.testapp.dead.UnusedDeadClass {',
    '  *;',
    '}',
    '-keepclassmembers class com.testapp.service.CryptoService { getBanner; }',
    '',
  ].join('\n'), 'utf-8');

  const outJar = path.join(testJarsDir, 'test_sample_keep_file.jar');
  runCli(['-i', inputJar, '-o', outJar, '--shrink', '--no-strings', '--keep-file', keepFile]);

  const entries = getJarEntries(outJar);
  if (!entries.some((e) => e.includes('UnusedDeadClass'))) {
    throw new Error('Keep-file rule for UnusedDeadClass was not honored!');
  }
  if (entries.includes('com/testapp/service/CryptoService.class')) {
    throw new Error('CryptoService should have been renamed (only keepclassmembers given)!');
  }

  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) {
    throw new Error('Keep-file JAR failed execution verification!');
  }
  logColor(colors.darkCyan, '   Multi-rule keep file honored for shrinking and member renaming.');
});

// TEST 28f: Invalid Keep Rule Rejected
runTestCase('Invalid Keep Rule Rejected', () => {
  const outJar = path.join(testJarsDir, 'test_sample_keep_invalid.jar');
  let failed = false;
  try {
    runCli(['-i', inputJar, '-o', outJar, '--keep', 'keepclassmemberz class Foo { *; }']);
  } catch {
    failed = true;
  }
  if (!failed) throw new Error('Invalid keep rule was silently accepted!');
  logColor(colors.darkCyan, '   Malformed keep rule correctly aborts the run.');
});

// TEST 28g: Kept Interface (Methods + Static Accessor, Impl Scrambled)
runTestCase('Kept Interface With Static Accessor (--keep interface ...)', () => {
  const outJar = path.join(testJarsDir, 'test_sample_keep_iface.jar');
  runCli(['-i', inputJar, '-o', outJar,
    '--keep', 'interface com.testapp.api.API { void methodOne(); void methodTwo(); com.testapp.api.API get(); }']);

  const entries = getJarEntries(outJar);
  if (!entries.includes('com/testapp/api/API.class')) {
    throw new Error('Kept interface com/testapp/api/API.class was renamed despite --keep!');
  }

  const javapOut = disassembleClass(outJar, 'com.testapp.api.API');
  for (const sig of [/\bmethodOne\(\)/, /\bmethodTwo\(\)/, /\bget\(\);/]) {
    if (!sig.test(javapOut)) {
      throw new Error(`Kept member ${sig} missing from obfuscated API interface!`);
    }
  }
  if (javapOut.includes('ImplementedAPI')) {
    throw new Error('Unrenamed ImplementedAPI type leaked in kept API interface!');
  }

  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) {
    throw new Error('Keep-interface JAR failed execution verification!');
  }
  logColor(colors.darkCyan, '   API interface + members kept, impl type remapped, execution verified.');
});

// TEST 29: Class Encryption & In-Memory Execution (--encrypt-classes)
runTestCase('Class Encryption & In-Memory Execution (--encrypt-classes)', () => {
  const outJar = path.join(testJarsDir, 'test_sample_encrypted.jar');
  runCli(['-i', inputJar, '-o', outJar, '--encrypt-classes']);

  const entries = getJarEntries(outJar);
  const classEntries = entries.filter((e) => e.endsWith('.class'));

  // Exactly 1 class must be exposed: Bootstrap.class
  if (classEntries.length !== 1 || !classEntries[0].includes('Bootstrap')) {
    throw new Error(`Expected exactly 1 exposed class (Bootstrap.class), found ${classEntries.length}: ${classEntries.join(', ')}`);
  }

  // Application classes must NOT be present as .class files
  if (entries.some((e) => e.includes('CryptoService') || e.includes('User.class'))) {
    throw new Error('Application classes found exposed as plaintext .class files in encrypted archive!');
  }

  // Encrypted resource must be present
  if (!entries.includes('META-INF/.data/app.bin')) {
    throw new Error("Encrypted resource bundle 'META-INF/.data/app.bin' missing from archive!");
  }

  // Execute and verify
  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) {
    throw new Error('Encrypted JAR failed in-memory execution verification!');
  }
  logColor(colors.darkCyan, '   All application classes encrypted into in-memory ClassLoader. 0 exposed application .class files.');
});

// TEST 30: Custom Bootstrap Launcher & Encrypted Resource Path (--launcher-class & --encrypted-resource)
runTestCase('Custom Bootstrap Launcher & Resource Path (--launcher-class & --encrypted-resource)', () => {
  const outJar = path.join(testJarsDir, 'test_sample_custom_loader.jar');
  runCli([
    '-i', inputJar,
    '-o', outJar,
    '--encrypt-classes',
    '--launcher-class', 'com/app/CustomLoader',
    '--encrypted-resource', 'assets/bin/kernel.dat'
  ]);

  const entries = getJarEntries(outJar);
  if (!entries.includes('com/app/CustomLoader.class')) {
    throw new Error("Expected custom launcher 'com/app/CustomLoader.class' in archive!");
  }
  if (!entries.includes('assets/bin/kernel.dat')) {
    throw new Error("Expected custom resource path 'assets/bin/kernel.dat' in archive!");
  }

  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) {
    throw new Error('Custom launcher JAR failed execution verification!');
  }
  logColor(colors.darkCyan, '   Custom launcher com/app/CustomLoader and payload assets/bin/kernel.dat executed flawlessly.');
});

// TEST 31: Full Fortress with Class Encryption (All 10 Passes Active)
runTestCase('Full Fortress with Class Encryption (All 10 Passes Active)', () => {
  const outJar = path.join(testJarsDir, 'test_sample_full_fortress.jar');
  runCli([
    '-i', inputJar,
    '-o', outJar,
    '--shrink',
    '--encrypt-classes',
    '--indy-calls',
    '--flatten-control-flow',
    '--decompiler-traps',
    '--flattening-intensity', '70'
  ]);

  const entries = getJarEntries(outJar);
  const classEntries = entries.filter((e) => e.endsWith('.class'));
  if (classEntries.length !== 1) {
    throw new Error(`Expected only 1 class in fortress archive, found ${classEntries.length}`);
  }

  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) {
    throw new Error('Full Fortress with Class Encryption failed execution verification!');
  }
  logColor(colors.darkCyan, '   All 10 obfuscation, shrinking, flattening, and encryption passes operating in complete harmony.');
});

// TEST 32: ConstantDynamic (Condy) Call Sites (Java 11+) (--condy)
runTestCase('ConstantDynamic (Condy) Call Sites (Java 11+) (--condy)', () => {
  const outJar = path.join(testJarsDir, 'test_sample_condy.jar');
  runCli([
    '-i', inputJar,
    '-o', outJar,
    '--condy',
    '--no-reflection'
  ]);

  // Verify runtime execution
  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) {
    throw new Error('Condy obfuscated JAR failed runtime execution!');
  }

  // Inspect bytecode using javap
  const tempExtractDir = path.join(testJarsDir, 'condy_extracted');
  if (fs.existsSync(tempExtractDir)) fs.rmSync(tempExtractDir, { recursive: true, force: true });
  fs.mkdirSync(tempExtractDir, { recursive: true });
  spawnSync(jarBin, ['-xf', outJar], { cwd: tempExtractDir });

  let foundCondy = false;
  const findFiles = (dir) => {
    for (const f of fs.readdirSync(dir)) {
      const full = path.join(dir, f);
      if (fs.statSync(full).isDirectory()) findFiles(full);
      else if (f.endsWith('.class')) {
        const dis = spawnSync(javapBin, ['-v', '-p', full], { encoding: 'utf-8' });
        if (dis.stdout && (dis.stdout.includes('Dynamic') || dis.stdout.includes('_condy'))) {
          foundCondy = true;
          break;
        }
      }
    }
  };
  findFiles(tempExtractDir);

  if (!foundCondy) {
    throw new Error('No CONSTANT_Dynamic or _condy call sites detected in bytecode!');
  }

  logColor(colors.darkCyan, '   ConstantDynamic (condy) call sites successfully injected & executed on Java 11+ runtime.');
});

// TEST 33: Standards-Compliant Decompiler Deterrence Suite (--decompiler-deterrence)
runTestCase('Standards-Compliant Decompiler Deterrence Suite (--decompiler-deterrence)', () => {
  const outJar = path.join(testJarsDir, 'test_sample_deterrence.jar');
  runCli([
    '-i', inputJar,
    '-o', outJar,
    '--decompiler-deterrence',
    '--no-reflection'
  ]);

  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) {
    throw new Error('Decompiler deterrence JAR failed execution verification!');
  }

  // Verify bytecode contains decompiler deterrence features
  const tempExtractDir = path.join(testJarsDir, 'deterrence_extracted');
  if (fs.existsSync(tempExtractDir)) fs.rmSync(tempExtractDir, { recursive: true, force: true });
  fs.mkdirSync(tempExtractDir, { recursive: true });
  spawnSync(jarBin, ['-xf', outJar], { cwd: tempExtractDir });

  let foundCustomAttr = false;
  let foundPhantomInner = false;
  const inspectClasses = (dir) => {
    for (const f of fs.readdirSync(dir)) {
      const full = path.join(dir, f);
      if (fs.statSync(full).isDirectory()) inspectClasses(full);
      else if (f.endsWith('.class')) {
        const dis = spawnSync(javapBin, ['-v', '-p', full], { encoding: 'utf-8' });
        if (dis.stdout) {
          if (dis.stdout.includes('$JObfDeterrent')) foundCustomAttr = true;
          if (dis.stdout.includes('InnerClasses') && dis.stdout.includes('$1')) foundPhantomInner = true;
        }
      }
    }
  };
  inspectClasses(tempExtractDir);

  if (!foundCustomAttr) {
    throw new Error('Custom specification-compliant attribute $JObfDeterrent not found in bytecode!');
  }
  if (!foundPhantomInner) {
    throw new Error('Phantom InnerClasses attribute table not found in bytecode!');
  }

  logColor(colors.darkCyan, '   Decompiler deterrence (overloads, synthetic bridge flags, phantom inner classes, custom attributes) verified.');
});

// TEST 34: Grand Ultimate Fortress with All 12 Passes Active
runTestCase('Grand Ultimate Fortress (All 12 Passes: Condy, Deterrence, Shrink, Indy, Traps, & Encrypt)', () => {
  const outJar = path.join(testJarsDir, 'test_sample_grand_fortress.jar');
  runCli([
    '-i', inputJar,
    '-o', outJar,
    '--shrink',
    '--indy-calls',
    '--condy',
    '--decompiler-deterrence',
    '--flatten-control-flow',
    '--decompiler-traps',
    '--encrypt-classes'
  ]);

  const entries = getJarEntries(outJar);
  const classEntries = entries.filter((e) => e.endsWith('.class'));
  if (classEntries.length !== 1) {
    throw new Error(`Expected only 1 launcher class in grand fortress archive, found ${classEntries.length}`);
  }

  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) {
    throw new Error('Grand Ultimate Fortress failed runtime execution verification!');
  }
  logColor(colors.darkCyan, '   All 12 obfuscation, condy, deterrence, shrinking, and class encryption passes running in complete harmony!');
});

// TEST 35: Method Declaration Order Scrambling (--scramble-methods)
runTestCase('Method Declaration Order Scrambling (--scramble-methods)', () => {
  const outJar = path.join(testJarsDir, 'test_sample_scrambled_methods.jar');
  runCli([
    '-i', inputJar,
    '-o', outJar,
    '--no-rename',
    '--scramble-methods',
    '--no-reflection'
  ]);

  // Extract both input and output to compare method order
  const extractOrig = path.join(testJarsDir, 'scramble_orig');
  const extractObf = path.join(testJarsDir, 'scramble_obf');
  if (fs.existsSync(extractOrig)) fs.rmSync(extractOrig, { recursive: true, force: true });
  if (fs.existsSync(extractObf)) fs.rmSync(extractObf, { recursive: true, force: true });
  fs.mkdirSync(extractOrig, { recursive: true });
  fs.mkdirSync(extractObf, { recursive: true });

  spawnSync(jarBin, ['-xf', inputJar], { cwd: extractOrig });
  spawnSync(jarBin, ['-xf', outJar], { cwd: extractObf });

  // Get method declaration order of CryptoService from javap
  const getMethods = (classPath) => {
    const res = spawnSync(javapBin, ['-p', classPath], { encoding: 'utf-8' });
    const lines = res.stdout.split(/\r?\n/).filter(l => l.includes('(') && l.includes(')'));
    return lines.map(l => l.trim());
  };

  const origCrypto = path.join(extractOrig, 'com', 'testapp', 'service', 'CryptoService.class');
  const obfCrypto = path.join(extractObf, 'com', 'testapp', 'service', 'CryptoService.class');

  if (fs.existsSync(origCrypto) && fs.existsSync(obfCrypto)) {
    const origMethods = getMethods(origCrypto);
    const obfMethods = getMethods(obfCrypto);

    const isDifferent = JSON.stringify(origMethods) !== JSON.stringify(obfMethods);
    if (!isDifferent) {
      throw new Error('Method order in CryptoService was not scrambled!');
    }
  }

  // Verify runtime execution
  const out = runJavaJar(outJar);
  if (!out.includes('TEST SUITES PASSED')) {
    throw new Error('Scrambled methods JAR failed runtime execution!');
  }

  logColor(colors.darkCyan, '   Method declaration sequence scrambled; decompilers disoriented while JVM executes cleanly.');
});

// TEST 36: Method Order Scrambling Default ON in Default Obfuscation & --no-scramble-methods Opt-Out
runTestCase('Method Scrambling Default ON in obf_all & --no-scramble-methods Opt-Out', () => {
  const outJarDefault = path.join(testJarsDir, 'test_sample_default_scramble.jar');
  const outJarNoScramble = path.join(testJarsDir, 'test_sample_no_scramble.jar');

  // 1. Run with default options (without --scramble-methods flag, but with --no-rename to inspect CryptoService)
  runCli([
    '-i', inputJar,
    '-o', outJarDefault,
    '--no-rename',
    '--no-reflection',
    '--no-strings'
  ]);

  // 2. Run with explicit --no-scramble-methods opt-out
  runCli([
    '-i', inputJar,
    '-o', outJarNoScramble,
    '--no-rename',
    '--no-scramble-methods',
    '--no-reflection',
    '--no-strings'
  ]);

  const extractOrig = path.join(testJarsDir, 'scramble_orig_def');
  const extractDef = path.join(testJarsDir, 'scramble_extract_def');
  const extractNo = path.join(testJarsDir, 'scramble_extract_no');
  if (fs.existsSync(extractOrig)) fs.rmSync(extractOrig, { recursive: true, force: true });
  if (fs.existsSync(extractDef)) fs.rmSync(extractDef, { recursive: true, force: true });
  if (fs.existsSync(extractNo)) fs.rmSync(extractNo, { recursive: true, force: true });
  fs.mkdirSync(extractOrig, { recursive: true });
  fs.mkdirSync(extractDef, { recursive: true });
  fs.mkdirSync(extractNo, { recursive: true });

  spawnSync(jarBin, ['-xf', inputJar], { cwd: extractOrig });
  spawnSync(jarBin, ['-xf', outJarDefault], { cwd: extractDef });
  spawnSync(jarBin, ['-xf', outJarNoScramble], { cwd: extractNo });

  const getMethodNames = (classPath) => {
    const res = spawnSync(javapBin, ['-p', classPath], { encoding: 'utf-8' });
    const lines = res.stdout.split(/\r?\n/).filter(l => l.includes('(') && l.includes(')'));
    return lines.map(l => {
      const beforeParen = l.split('(')[0].trim();
      const parts = beforeParen.split(/\s+/);
      return parts[parts.length - 1];
    });
  };

  const origCrypto = path.join(extractOrig, 'com', 'testapp', 'service', 'CryptoService.class');
  const defCrypto = path.join(extractDef, 'com', 'testapp', 'service', 'CryptoService.class');
  const noCrypto = path.join(extractNo, 'com', 'testapp', 'service', 'CryptoService.class');

  const origMethods = getMethodNames(origCrypto);
  const defMethods = getMethodNames(defCrypto);
  const noMethods = getMethodNames(noCrypto);

  // Default run MUST be scrambled
  if (JSON.stringify(origMethods) === JSON.stringify(defMethods)) {
    throw new Error('Default obfuscation run did not scramble method order!');
  }

  // --no-scramble-methods MUST preserve original order
  if (JSON.stringify(origMethods) !== JSON.stringify(noMethods)) {
    throw new Error('--no-scramble-methods failed to preserve original method order!');
  }

  // Runtime verification for both
  const outDef = runJavaJar(outJarDefault);
  const outNo = runJavaJar(outJarNoScramble);
  if (!outDef.includes('TEST SUITES PASSED') || !outNo.includes('TEST SUITES PASSED')) {
    throw new Error('Runtime execution failed for default scramble or no-scramble JARs!');
  }

  logColor(colors.darkCyan, '   Confirmed: Method scrambling is active by default in standard obfuscation and disabled cleanly by --no-scramble-methods.');
});

// TEST 37: Spigot plugin.yml Class Remapping (Default ON)
runTestCase('Spigot plugin.yml Class Reference Remapping', () => {
  const spigotTempDir = path.join(testJarsDir, 'spigot_prep');
  if (fs.existsSync(spigotTempDir)) fs.rmSync(spigotTempDir, { recursive: true, force: true });
  fs.mkdirSync(spigotTempDir, { recursive: true });

  const spigotInputJar = path.join(testJarsDir, 'test_spigot_input.jar');
  const spigotOutputJar = path.join(testJarsDir, 'test_spigot_obf.jar');
  fs.copyFileSync(inputJar, spigotInputJar);

  const samplePluginYml = [
    'name: JObfSpigotTest',
    'version: 2.5.0',
    'main: com.testapp.service.CryptoService',
    'author: Rejxk',
    'description: Test Spigot plugin main class remapping',
    'commands:',
    '  crypto:',
    '    description: Run crypto operations',
    '    permission: testapp.crypto',
  ].join('\n');

  fs.writeFileSync(path.join(spigotTempDir, 'plugin.yml'), samplePluginYml, 'utf-8');
  spawnSync(jarBin, ['-uf', spigotInputJar, 'plugin.yml'], { cwd: spigotTempDir });

  // Run full obfuscation with default settings
  runCli(['-i', spigotInputJar, '-o', spigotOutputJar]);

  const spigotExtractDir = path.join(testJarsDir, 'spigot_extract');
  if (fs.existsSync(spigotExtractDir)) fs.rmSync(spigotExtractDir, { recursive: true, force: true });
  fs.mkdirSync(spigotExtractDir, { recursive: true });

  spawnSync(jarBin, ['-xf', spigotOutputJar], { cwd: spigotExtractDir });

  const obfPluginYmlPath = path.join(spigotExtractDir, 'plugin.yml');
  if (!fs.existsSync(obfPluginYmlPath)) {
    throw new Error('plugin.yml not found in obfuscated Spigot JAR!');
  }

  const obfPluginYml = fs.readFileSync(obfPluginYmlPath, 'utf-8');
  console.log('Obfuscated plugin.yml:');
  console.log(obfPluginYml);

  if (obfPluginYml.includes('main: com.testapp.service.CryptoService')) {
    throw new Error('plugin.yml still contains original un-obfuscated main class name!');
  }

  const mainLine = obfPluginYml.split(/\r?\n/).find(l => l.startsWith('main:'));
  if (!mainLine) {
    throw new Error('main: key was removed or corrupted in plugin.yml!');
  }

  const remappedClass = mainLine.replace('main:', '').trim();
  if (!remappedClass || remappedClass === 'com.testapp.service.CryptoService') {
    throw new Error(`Invalid remapped main class in plugin.yml: '${remappedClass}'`);
  }

  // Verify the remapped class actually exists in the archive!
  const remappedClassPath = path.join(spigotExtractDir, ...remappedClass.split('.')) + '.class';
  if (!fs.existsSync(remappedClassPath)) {
    throw new Error(`Remapped class '${remappedClass}' from plugin.yml does not exist in JAR at '${remappedClassPath}'!`);
  }

  // Verify other YAML fields were preserved
  if (!obfPluginYml.includes('name: JObfSpigotTest') || !obfPluginYml.includes('version: 2.5.0')) {
    throw new Error('plugin.yml metadata fields were corrupted during remapping!');
  }

  logColor(colors.darkCyan, `   Spigot plugin.yml successfully remapped main class -> ${remappedClass}`);
});

// TEST 38: Disable Resource Remapping (--no-remap-resources)
runTestCase('Resource Remapping Opt-Out (--no-remap-resources)', () => {
  const spigotInputJar = path.join(testJarsDir, 'test_spigot_input.jar');
  const spigotNoRemapJar = path.join(testJarsDir, 'test_spigot_no_remap.jar');

  runCli(['-i', spigotInputJar, '-o', spigotNoRemapJar, '--no-remap-resources']);

  const extractDir = path.join(testJarsDir, 'spigot_extract_no_remap');
  if (fs.existsSync(extractDir)) fs.rmSync(extractDir, { recursive: true, force: true });
  fs.mkdirSync(extractDir, { recursive: true });

  spawnSync(jarBin, ['-xf', spigotNoRemapJar, 'plugin.yml'], { cwd: extractDir });

  const pluginYml = fs.readFileSync(path.join(extractDir, 'plugin.yml'), 'utf-8');
  if (!pluginYml.includes('main: com.testapp.service.CryptoService')) {
    throw new Error('--no-remap-resources failed: plugin.yml was unexpectedly modified!');
  }

  logColor(colors.darkCyan, '   Confirmed: --no-remap-resources cleanly preserves original descriptor class names.');
});

// TEST 39: Java ServiceLoader (META-INF/services) Remapping
runTestCase('Java ServiceLoader (META-INF/services) Remapping', () => {
  const servicePrepDir = path.join(testJarsDir, 'service_prep');
  if (fs.existsSync(servicePrepDir)) fs.rmSync(servicePrepDir, { recursive: true, force: true });
  const servicesDir = path.join(servicePrepDir, 'META-INF', 'services');
  fs.mkdirSync(servicesDir, { recursive: true });

  const serviceInputJar = path.join(testJarsDir, 'test_service_input.jar');
  const serviceOutputJar = path.join(testJarsDir, 'test_service_obf.jar');
  fs.copyFileSync(inputJar, serviceInputJar);

  // Interface file named com.testapp.service.CryptoService containing implementation com.testapp.service.CryptoService
  fs.writeFileSync(
    path.join(servicesDir, 'com.testapp.service.CryptoService'),
    'com.testapp.service.CryptoService\n',
    'utf-8'
  );
  spawnSync(jarBin, ['-uf', serviceInputJar, 'META-INF/services/com.testapp.service.CryptoService'], { cwd: servicePrepDir });

  // Run full obfuscation
  runCli(['-i', serviceInputJar, '-o', serviceOutputJar]);

  const serviceExtractDir = path.join(testJarsDir, 'service_extract');
  if (fs.existsSync(serviceExtractDir)) fs.rmSync(serviceExtractDir, { recursive: true, force: true });
  fs.mkdirSync(serviceExtractDir, { recursive: true });

  spawnSync(jarBin, ['-xf', serviceOutputJar], { cwd: serviceExtractDir });

  const outServicesDir = path.join(serviceExtractDir, 'META-INF', 'services');
  if (!fs.existsSync(outServicesDir)) {
    throw new Error('META-INF/services directory not found in obfuscated archive!');
  }

  const serviceFiles = fs.readdirSync(outServicesDir);
  if (serviceFiles.length === 0) {
    throw new Error('No service descriptor files found in META-INF/services!');
  }

  const remappedIfaceName = serviceFiles[0];
  if (remappedIfaceName === 'com.testapp.service.CryptoService') {
    throw new Error('Service descriptor filename was not remapped!');
  }

  const content = fs.readFileSync(path.join(outServicesDir, remappedIfaceName), 'utf-8').trim();
  if (content === 'com.testapp.service.CryptoService') {
    throw new Error('Service provider class inside descriptor was not remapped!');
  }

  if (content !== remappedIfaceName) {
    throw new Error(`Expected provider '${content}' to match remapped interface '${remappedIfaceName}'!`);
  }

  logColor(colors.darkCyan, `   META-INF/services interface & implementation remapped to: ${remappedIfaceName}`);
});

// TEST 40: Symbol mapping.txt Export
runTestCase('Symbol mapping.txt Export', () => {
  const mappingOutJar = path.join(testJarsDir, 'test_mapping_output.jar');
  const mappingFile = path.join(testJarsDir, 'test_mapping.txt');
  if (fs.existsSync(mappingFile)) fs.unlinkSync(mappingFile);

  // Run with -m mappingFile
  runCli(['-i', inputJar, '-o', mappingOutJar, '-m', mappingFile]);

  if (!fs.existsSync(mappingFile)) {
    throw new Error(`mapping.txt was not generated at: ${mappingFile}`);
  }

  const mappingContent = fs.readFileSync(mappingFile, 'utf-8');
  if (!mappingContent.includes('# mapping_format: JObfuscate')) {
    throw new Error('mapping.txt missing mapping header identifier!');
  }

  // Check class mapping entry
  if (!mappingContent.includes('com.testapp.Main -> ') || !mappingContent.includes('com.testapp.service.CryptoService -> ')) {
    throw new Error('mapping.txt missing expected class mapping lines!');
  }

  // Check method mapping entry
  if (!mappingContent.includes('void main(java.lang.String[]) -> ')) {
    throw new Error('mapping.txt missing method mapping lines (e.g. main)!');
  }

  // Check field mapping entry (e.g. username or id in User class)
  if (!mappingContent.includes('java.lang.String username -> ') || !mappingContent.includes('int id -> ')) {
    throw new Error('mapping.txt missing field mapping lines (e.g. username/id)!');
  }

  logColor(colors.darkCyan, `   mapping.txt successfully generated (${mappingContent.split(/\r?\n/).length} lines)`);
});

// TEST 41: Stack Trace Retrace / Deobfuscation (CLI & Retrace Engine)
runTestCase('Stack Trace Retrace / Deobfuscation', () => {
  const mappingFile = path.join(testJarsDir, 'test_mapping.txt');
  if (!fs.existsSync(mappingFile)) {
    throw new Error('Mapping file from TEST 40 not found!');
  }
  const mappingContent = fs.readFileSync(mappingFile, 'utf-8');

  // Extract the obfuscated class name for com.testapp.service.CryptoService
  const cryptoMatch = mappingContent.match(/com\.testapp\.service\.CryptoService -> ([^:]+):/);
  if (!cryptoMatch) {
    throw new Error('Could not find com.testapp.service.CryptoService in mapping.txt');
  }
  const obfCryptoCls = cryptoMatch[1].trim();

  // Find a method of CryptoService
  const lines = mappingContent.split(/\r?\n/);
  let obfCryptoMethod = null;
  let inCrypto = false;
  for (const line of lines) {
    if (line.startsWith('com.testapp.service.CryptoService -> ')) {
      inCrypto = true;
      continue;
    }
    if (inCrypto) {
      if (line && !line.startsWith(' ') && !line.startsWith('\t')) {
        break;
      }
      const mMatch = line.match(/\s+([a-zA-Z0-9_.\[\]]+)\s+([a-zA-Z0-9_$]+)\(([^)]*)\)\s+->\s+([a-zA-Z0-9_$]+)/);
      if (mMatch && mMatch[2] !== '<init>' && mMatch[2] !== '<clinit>') {
        obfCryptoMethod = { orig: mMatch[2], obf: mMatch[4] };
        break;
      }
    }
  }

  if (!obfCryptoMethod) {
    throw new Error('Could not find an obfuscated method for CryptoService in mapping.txt');
  }

  // Synthesize an obfuscated runtime crash report
  const fakeCrash = [
    `java.lang.RuntimeException: Decryption failed`,
    `\tat ${obfCryptoCls}.${obfCryptoMethod.obf}(Unknown Source)`,
    `Caused by: ${obfCryptoCls}: Bad key`,
    `\tat ${obfCryptoCls}.${obfCryptoMethod.obf}(Unknown Source)`
  ].join('\n');

  const crashFile = path.join(testJarsDir, 'test_crash_trace.txt');
  fs.writeFileSync(crashFile, fakeCrash, 'utf-8');

  // Run CLI --retrace
  const retraceRes = spawnSync(cliBin, ['--retrace', mappingFile, crashFile], { encoding: 'utf-8' });
  if (retraceRes.status !== 0) {
    throw new Error(`CLI --retrace exited with code ${retraceRes.status}: ${retraceRes.stderr}`);
  }

  const output = retraceRes.stdout;
  if (!output.includes(`at com.testapp.service.CryptoService.${obfCryptoMethod.orig}(CryptoService.java)`)) {
    throw new Error(`Retraced output did not restore original class and method! Output:\n${output}`);
  }

  if (!output.includes('Caused by: com.testapp.service.CryptoService: Bad key')) {
    throw new Error(`Retraced output did not restore exception class name! Output:\n${output}`);
  }

  logColor(colors.darkCyan, `   Retraced frame: at ${obfCryptoCls}.${obfCryptoMethod.obf}(Unknown Source) -> at com.testapp.service.CryptoService.${obfCryptoMethod.orig}(CryptoService.java)`);
  logColor(colors.darkCyan, '   CLI --retrace successfully deobfuscated multi-frame stack trace and exception causes.');
});

// ===========================================================================
// Kotlin sample app (kotlinc embeddable, cached under build/kotlin,
// downloaded from Maven Central on first use).
// ===========================================================================
const kotlinDir = path.join(rootDir, 'build', 'kotlin');
const KOTLIN_COMPILER_JAR = 'kotlin-compiler-embeddable-2.1.20.jar';
const KOTLIN_STDLIB_JAR = 'kotlin-stdlib-1.9.24.jar';
const KOTLIN_ARTIFACTS = [
  `org/jetbrains/kotlin/kotlin-compiler-embeddable/2.1.20/${KOTLIN_COMPILER_JAR}`,
  `org/jetbrains/kotlin/kotlin-stdlib/1.9.24/${KOTLIN_STDLIB_JAR}`,
  'org/jetbrains/kotlin/kotlin-script-runtime/1.9.24/kotlin-script-runtime-1.9.24.jar',
  'org/jetbrains/kotlin/kotlin-reflect/1.9.24/kotlin-reflect-1.9.24.jar',
  'org/jetbrains/annotations/13.0/annotations-13.0.jar',
  'org/jetbrains/intellij/deps/trove4j/1.0.20200330/trove4j-1.0.20200330.jar',
  'org/jetbrains/kotlinx/kotlinx-coroutines-core-jvm/1.10.1/kotlinx-coroutines-core-jvm-1.10.1.jar',
];

function ensureKotlinCompiler() {
  fs.mkdirSync(kotlinDir, { recursive: true });
  const missing = KOTLIN_ARTIFACTS.map((a) => path.join(kotlinDir, path.basename(a)))
    .filter((f) => !fs.existsSync(f));
  if (missing.length === 0) return;
  const curlBin = isWindows ? 'curl.exe' : 'curl';
  for (const artifact of KOTLIN_ARTIFACTS) {
    const dest = path.join(kotlinDir, path.basename(artifact));
    if (fs.existsSync(dest)) continue;
    const url = `https://repo1.maven.org/maven2/${artifact}`;
    logColor(colors.gray, `[SETUP] Downloading ${path.basename(dest)} ...`);
    const dl = spawnSync(curlBin, ['-sL', '-o', dest, url], { encoding: 'utf-8' });
    if (dl.status !== 0 || !fs.existsSync(dest) || fs.statSync(dest).size === 0) {
      throw new Error(`Failed to download Kotlin artifact ${url}. Pre-seed ${kotlinDir} manually.`);
    }
  }
}

function compileKotlinSample() {
  ensureKotlinCompiler();
  const cpSep = isWindows ? ';' : ':';
  const sampleDir = path.join(rootDir, 'sample_kotlin');
  const classesDir = path.join(sampleDir, 'build', 'classes');
  if (fs.existsSync(classesDir)) fs.rmSync(classesDir, { recursive: true, force: true });
  fs.mkdirSync(classesDir, { recursive: true });

  const ktSources = collectFiles(path.join(sampleDir, 'src', 'main', 'kotlin'), (f) => f.endsWith('.kt'));
  if (ktSources.length === 0) throw new Error('No Kotlin sources found in sample_kotlin!');

  const kotlinCp = KOTLIN_ARTIFACTS.map((a) => path.join(kotlinDir, path.basename(a))).join(cpSep);
  const stdlibJar = path.join(kotlinDir, KOTLIN_STDLIB_JAR);
  const kc = spawnSync(javaBin,
    ['-cp', kotlinCp, 'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler',
     '-no-stdlib', '-no-reflect', '-cp', stdlibJar, '-d', classesDir, ...ktSources],
    { encoding: 'utf-8' });
  if (kc.status !== 0 || !fs.existsSync(path.join(classesDir, 'com', 'kotest', 'AppKt.class'))) {
    throw new Error(`kotlinc failed:\n${kc.stderr || kc.stdout}`);
  }

  const inputJar = path.join(testJarsDir, 'test_kotlin_input.jar');
  fs.writeFileSync(path.join(testJarsDir, 'kotlin_manifest.mf'), 'Main-Class: com.kotest.AppKt\n', 'utf-8');
  const jr = spawnSync(jarBin, ['-cfm', inputJar, path.join(testJarsDir, 'kotlin_manifest.mf'), '-C', classesDir, '.'], { encoding: 'utf-8' });
  if (jr.status !== 0) throw new Error(`Failed to package Kotlin input JAR:\n${jr.stderr || jr.stdout}`);
  return { inputJar, stdlibJar };
}

function runKotlinJar(jarPath, stdlibJar) {
  const cpSep = isWindows ? ';' : ':';
  const res = spawnSync(javaBin, ['-cp', `${jarPath}${cpSep}${stdlibJar}`, 'com.kotest.AppKt'], { encoding: 'utf-8' });
  const output = (res.stdout || '') + '\n' + (res.stderr || '');
  if (res.status !== 0) {
    throw new Error(`Kotlin JAR failed execution with exit code ${res.status}:\n${output}`);
  }
  return output;
}

function kotlinHasMetadata(jarPath) {
  // Look for the rendered annotation, not the bare name: orphaned constant
  // pool entries never contain '('.
  const entries = getJarEntries(jarPath).filter((e) => e.endsWith('.class'));
  for (const entry of entries) {
    const cls = entry.replace(/\//g, '.').replace(/\.class$/, '');
    const res = spawnSync(javapBin, ['-v', '-p', '-cp', jarPath, cls], { encoding: 'utf-8' });
    if ((res.stdout || '').includes('kotlin.Metadata(')) return true;
  }
  return false;
}

// TEST K1: Kotlin Sample Obfuscation & Execution
runTestCase('Kotlin Sample Obfuscation & Execution', () => {
  const { inputJar, stdlibJar } = compileKotlinSample();
  const outJar = path.join(testJarsDir, 'test_kotlin_obf.jar');
  runCli(['-i', inputJar, '-o', outJar, '--keep-main-class']);
  const out = runKotlinJar(outJar, stdlibJar);
  if (!out.includes('KOTLIN-SUITES-PASSED')) {
    throw new Error('Kotlin obfuscated JAR failed execution verification!');
  }
  logColor(colors.darkCyan, '   Kotlin data class, object, templates and branches obfuscated and executed.');
});

// TEST K2: Kotlin Metadata Stripped by Default (+ string secrecy)
runTestCase('Kotlin Metadata Stripped by Default', () => {
  const outJar = path.join(testJarsDir, 'test_kotlin_obf.jar');
  if (kotlinHasMetadata(outJar)) {
    throw new Error('kotlin/Metadata annotation survived default obfuscation (identifier leak)!');
  }
  const content = fs.readFileSync(outJar, 'latin1');
  for (const leaked of ['KOTLIN_VAULT_2025', 'describeSecret']) {
    if (content.includes(leaked)) {
      throw new Error(`Plaintext Kotlin leak in obfuscated JAR: '${leaked}'`);
    }
  }
  logColor(colors.darkCyan, '   kotlin/Metadata erased; literals and identifier names absent from archive.');
});

// TEST K3: Kotlin Metadata Preserved (--no-strip-kotlin-metadata)
runTestCase('Kotlin Metadata Preserved (--no-strip-kotlin-metadata)', () => {
  const { inputJar, stdlibJar } = compileKotlinSample();
  const outJar = path.join(testJarsDir, 'test_kotlin_metadata.jar');
  runCli(['-i', inputJar, '-o', outJar, '--keep-main-class', '--no-strip-kotlin-metadata']);
  if (!kotlinHasMetadata(outJar)) {
    throw new Error('kotlin/Metadata missing despite --no-strip-kotlin-metadata!');
  }
  const out = runKotlinJar(outJar, stdlibJar);
  if (!out.includes('KOTLIN-SUITES-PASSED')) {
    throw new Error('Metadata-preserving Kotlin JAR failed execution!');
  }
  logColor(colors.darkCyan, '   kotlin/Metadata preserved byte-identical and execution verified.');
});

// TEST K4: Kotlin With Stdlib Proxies (--import-java covers kotlin.*)
runTestCase('Kotlin With Stdlib Proxies (--import-java covers kotlin.*)', () => {
  const { inputJar, stdlibJar } = compileKotlinSample();
  const outJar = path.join(testJarsDir, 'test_kotlin_import_java.jar');
  runCli(['-i', inputJar, '-o', outJar, '--keep-main-class', '--import-java']);
  const out = runKotlinJar(outJar, stdlibJar);
  if (!out.includes('KOTLIN-SUITES-PASSED')) {
    throw new Error('Kotlin JAR with --import-java failed execution (Intrinsics proxies broken?)!');
  }
  logColor(colors.darkCyan, '   kotlin.jvm.internal.Intrinsics calls proxied and executed.');
});

console.log('');
logColor(colors.cyan, '============================================================');
const isSuccess = passedTests === totalTests;
logColor(
  isSuccess ? colors.green : colors.red,
  `Test Results: ${passedTests} / ${totalTests} Tests Passed`
);
logColor(colors.cyan, '============================================================');

if (!isSuccess) {
  process.exit(1);
}
process.exit(0);
