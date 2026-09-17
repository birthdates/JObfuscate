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
