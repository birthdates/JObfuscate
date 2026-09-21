/**
 * Utility for parsing mapping.txt files and retracing (deobfuscating)
 * obfuscated stack traces.
 */

export interface RetraceResult {
  output: string;
  mappedClassesCount: number;
  mappedMethodsCount: number;
}

export function retraceStackTrace(mappingContent: string, stackTrace: string): RetraceResult {
  const obfToOrigClass = new Map<string, string>();
  const classObfMethods = new Map<string, Map<string, string>>();
  const classObfFields = new Map<string, Map<string, string>>();

  let currentObfClass = '';
  let mappedMethodsCount = 0;

  // 1. Parse mapping.txt
  const lines = mappingContent.split(/\r?\n/);
  for (const rawLine of lines) {
    const line = rawLine.trimEnd();
    if (!line || line.startsWith('#')) continue;

    if (!line.startsWith(' ') && !line.startsWith('\t')) {
      // Class mapping line: orig_name -> obf_name:
      const arrowIdx = line.indexOf(' -> ');
      if (arrowIdx !== -1 && line.endsWith(':')) {
        const origCls = line.substring(0, arrowIdx).trim();
        const obfCls = line.substring(arrowIdx + 4, line.length - 1).trim();
        if (origCls && obfCls) {
          obfToOrigClass.set(obfCls, origCls);
          currentObfClass = obfCls;
          if (!classObfMethods.has(obfCls)) {
            classObfMethods.set(obfCls, new Map());
          }
          if (!classObfFields.has(obfCls)) {
            classObfFields.set(obfCls, new Map());
          }
        }
      }
    } else if (currentObfClass) {
      // Member mapping line: [type] orig -> obf
      const arrowIdx = line.indexOf(' -> ');
      if (arrowIdx === -1) continue;

      const left = line.substring(0, arrowIdx).trim();
      const obfName = line.substring(arrowIdx + 4).trim();

      const parenIdx = left.indexOf('(');
      if (parenIdx !== -1) {
        // Method: return_type orig_name(params)
        const beforeParen = left.substring(0, parenIdx);
        const lastSpace = beforeParen.lastIndexOf(' ');
        const origName = lastSpace !== -1 ? beforeParen.substring(lastSpace + 1) : beforeParen;
        classObfMethods.get(currentObfClass)?.set(obfName, origName);
        mappedMethodsCount++;
      } else {
        // Field: type orig_name
        const lastSpace = left.lastIndexOf(' ');
        const origName = lastSpace !== -1 ? left.substring(lastSpace + 1) : left;
        classObfFields.get(currentObfClass)?.set(obfName, origName);
      }
    }
  }

  // 2. Parse and retrace stack trace lines
  const traceLines = stackTrace.split(/\r?\n/);
  const resultLines: string[] = [];

  const atRegex = /^(\s*at\s+)([a-zA-Z0-9_$]+(?:\.[a-zA-Z0-9_$]+)*)\.([a-zA-Z0-9_$<>]+\b)\(([^)]*)\)(.*)$/;
  const exRegex = /^(\s*(?:Caused by:\s+)?)([a-zA-Z0-9_$]+(?:\.[a-zA-Z0-9_$]+)*)(:\s*.*)?$/;

  for (const line of traceLines) {
    const atMatch = atRegex.exec(line);
    if (atMatch) {
      const [, prefix, cls, meth, src, rest] = atMatch;

      let finalCls = cls;
      let finalMeth = meth;
      let finalSrc = src;

      if (obfToOrigClass.has(cls)) {
        finalCls = obfToOrigClass.get(cls)!;

        const methodsMap = classObfMethods.get(cls);
        if (methodsMap && methodsMap.has(meth)) {
          finalMeth = methodsMap.get(meth)!;
        }

        const lastDot = finalCls.lastIndexOf('.');
        let simpleCls = lastDot !== -1 ? finalCls.substring(lastDot + 1) : finalCls;
        const dollar = simpleCls.indexOf('$');
        if (dollar !== -1) {
          simpleCls = simpleCls.substring(0, dollar);
        }

        if (src === 'Unknown Source' || !src) {
          finalSrc = `${simpleCls}.java`;
        } else if (src.includes(':')) {
          const lineNum = src.substring(src.indexOf(':'));
          finalSrc = `${simpleCls}.java${lineNum}`;
        }
      }

      resultLines.push(`${prefix}${finalCls}.${finalMeth}(${finalSrc})${rest}`);
      continue;
    }

    const exMatch = exRegex.exec(line);
    if (exMatch) {
      const [, prefix, cls, msg] = exMatch;
      if (obfToOrigClass.has(cls)) {
        resultLines.push(`${prefix}${obfToOrigClass.get(cls)!}${msg || ''}`);
        continue;
      }
    }

    resultLines.push(line);
  }

  return {
    output: resultLines.join('\n'),
    mappedClassesCount: obfToOrigClass.size,
    mappedMethodsCount,
  };
}
