import React, { useState } from "react";
import { retraceStackTrace, RetraceResult } from "../utils/retrace";
import {
  Search,
  Zap,
  Upload,
  RefreshCw,
  CheckCircle2,
  Copy,
  Check,
  Trash2,
  FileCode,
  FileText,
} from "lucide-react";

const SAMPLE_MAPPING = `# compiler: JObfuscate
# compiler_version: 1.0.0
# mapping_format: JObfuscate

com.testapp.Main -> a.a:
    java.lang.String APP_VERSION -> a
    void main(java.lang.String[]) -> main
    void execute() -> a
com.testapp.service.CryptoService -> a.b:
    byte[] masterKey -> a
    void <init>() -> <init>
    byte[] decryptPayload(byte[],byte[]) -> a
    java.lang.String getAlgorithm() -> b
com.testapp.network.HttpClient -> a.c:
    int timeoutMs -> a
    void connect(java.lang.String) -> a
    java.lang.String sendRequest(java.lang.String,byte[]) -> a
`;

const SAMPLE_STACKTRACE = `java.lang.SecurityException: Invalid payload authentication tag
\tat a.b.a(Unknown Source)
\tat a.c.a(Unknown Source)
\tat a.a.a(Unknown Source)
\tat a.a.main(Unknown Source)
Caused by: java.lang.IllegalArgumentException: Key length mismatch
\tat a.b.a(Unknown Source)
`;

export const RetraceSection: React.FC = () => {
  const [mappingContent, setMappingContent] = useState<string>("");
  const [obfStackTrace, setObfStackTrace] = useState<string>("");
  const [result, setResult] = useState<RetraceResult | null>(null);
  const [copied, setCopied] = useState<boolean>(false);

  const handleDeobfuscate = () => {
    if (!mappingContent.trim() && !obfStackTrace.trim()) return;
    const res = retraceStackTrace(mappingContent, obfStackTrace);
    setResult(res);
  };

  const handleLoadSample = () => {
    setMappingContent(SAMPLE_MAPPING);
    setObfStackTrace(SAMPLE_STACKTRACE);
    const res = retraceStackTrace(SAMPLE_MAPPING, SAMPLE_STACKTRACE);
    setResult(res);
  };

  const handleFileUpload = (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = (event) => {
      const text = event.target?.result as string;
      setMappingContent(text);
      if (obfStackTrace.trim()) {
        const res = retraceStackTrace(text, obfStackTrace);
        setResult(res);
      }
    };
    reader.readAsText(file);
  };

  const handleCopy = () => {
    if (!result?.output) return;
    navigator.clipboard.writeText(result.output);
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  };

  return (
    <div className="space-y-6">
      {/* Retrace Controls Card */}
      <div className="bg-slate-900/90 border border-slate-800 rounded-2xl p-5 shadow-2xl backdrop-blur-sm space-y-5">
        <div className="flex flex-wrap items-center justify-between gap-3 border-b border-slate-800/80 pb-4">
          <div>
            <h2 className="text-sm font-bold tracking-tight text-white flex items-center gap-2">
              <Search className="w-4 h-4 text-blue-400" />
              <span>Stack Trace Deobfuscator & Retrace</span>
            </h2>
            <p className="text-xs text-slate-400 mt-0.5">
              Restore human-readable classes, methods, and source files from production crash traces using a <code className="text-emerald-400 font-mono">mapping.txt</code> file.
            </p>
          </div>

          <div className="flex items-center gap-2">
            <button
              onClick={handleLoadSample}
              className="px-3 py-1.5 rounded-lg text-xs bg-blue-500/10 hover:bg-blue-500/20 text-blue-300 border border-blue-500/30 font-medium transition-colors flex items-center gap-1.5 cursor-pointer"
            >
              <Zap className="w-3.5 h-3.5 text-blue-400" />
              <span>Load Sample Trace</span>
            </button>
            <label className="px-3 py-1.5 rounded-lg text-xs bg-slate-900 hover:bg-slate-800 text-slate-300 border border-slate-700 font-medium transition-colors flex items-center gap-1.5 cursor-pointer">
              <Upload className="w-3.5 h-3.5 text-blue-400" />
              <span>Upload mapping.txt</span>
              <input type="file" accept=".txt" onChange={handleFileUpload} className="hidden" />
            </label>
          </div>
        </div>

        <div className="grid grid-cols-1 lg:grid-cols-2 gap-4">
          {/* Mapping Input */}
          <div className="space-y-1.5">
            <div className="flex items-center justify-between text-xs">
              <label className="font-semibold text-slate-300 flex items-center gap-1.5">
                <FileCode className="w-3.5 h-3.5 text-cyan-400" />
                <span>1. Symbol Mapping (<code className="text-blue-400 font-mono">mapping.txt</code>)</span>
              </label>
              {result && (
                <span className="text-[11px] font-mono text-cyan-400">
                  {result.mappedClassesCount} classes, {result.mappedMethodsCount} methods mapped
                </span>
              )}
            </div>
            <textarea
              value={mappingContent}
              onChange={(e) => setMappingContent(e.target.value)}
              placeholder="Paste mapping.txt content here or click 'Upload mapping.txt'..."
              rows={10}
              className="w-full bg-slate-950 border border-slate-800 rounded-xl px-3 py-2.5 text-xs font-mono text-slate-200 focus:outline-none focus:border-blue-500 resize-y leading-relaxed placeholder:text-slate-600"
            />
          </div>

          {/* Obfuscated Stack Trace Input */}
          <div className="space-y-1.5">
            <div className="flex items-center justify-between text-xs">
              <label className="font-semibold text-slate-300 flex items-center gap-1.5">
                <FileText className="w-3.5 h-3.5 text-rose-400" />
                <span>2. Obfuscated Stack Trace</span>
              </label>
              {obfStackTrace && (
                <button
                  onClick={() => { setObfStackTrace(""); setResult(null); }}
                  className="text-[11px] text-slate-500 hover:text-red-400 transition-colors flex items-center gap-1 cursor-pointer"
                >
                  <Trash2 className="w-3 h-3" />
                  <span>Clear</span>
                </button>
              )}
            </div>
            <textarea
              value={obfStackTrace}
              onChange={(e) => setObfStackTrace(e.target.value)}
              placeholder={`Paste raw runtime stack trace from log/console here:\nat a.b.c.a(Unknown Source)\nCaused by: a.b.d: key error`}
              rows={10}
              className="w-full bg-slate-950 border border-slate-800 rounded-xl px-3 py-2.5 text-xs font-mono text-rose-300 focus:outline-none focus:border-rose-500 resize-y leading-relaxed placeholder:text-slate-600"
            />
          </div>
        </div>

        <div className="pt-2 flex justify-center">
          <button
            onClick={handleDeobfuscate}
            disabled={!mappingContent.trim() || !obfStackTrace.trim()}
            className={`py-3 px-8 rounded-xl font-bold text-xs uppercase tracking-wider flex items-center gap-2 transition-all cursor-pointer border ${
              !mappingContent.trim() || !obfStackTrace.trim()
                ? "bg-slate-950 text-slate-600 border-slate-800 cursor-not-allowed"
                : "bg-gradient-to-r from-blue-600 via-indigo-600 to-blue-500 hover:from-blue-500 hover:to-indigo-500 text-white border-blue-400/30 shadow-lg shadow-blue-600/30 active:scale-[0.99]"
            }`}
          >
            <RefreshCw className="w-4 h-4" />
            <span>Deobfuscate Stack Trace</span>
          </button>
        </div>
      </div>

      {/* Deobfuscated Output Card */}
      {result && (
        <div className="bg-slate-900/90 border border-emerald-500/30 rounded-2xl p-5 shadow-2xl backdrop-blur-sm space-y-3">
          <div className="flex items-center justify-between border-b border-slate-800/80 pb-3">
            <div className="flex items-center gap-2">
              <CheckCircle2 className="w-4 h-4 text-emerald-400" />
              <h3 className="text-xs font-bold uppercase tracking-wider text-emerald-400">
                Deobfuscated Stack Trace Output
              </h3>
            </div>
            <button
              onClick={handleCopy}
              className={`px-3 py-1.5 rounded-lg text-xs font-medium transition-all flex items-center gap-1.5 cursor-pointer border ${
                copied
                  ? "bg-emerald-600 text-white border-emerald-500 shadow-md shadow-emerald-600/20"
                  : "bg-emerald-500/10 hover:bg-emerald-500/20 text-emerald-300 border-emerald-500/30"
              }`}
            >
              {copied ? <Check className="w-3.5 h-3.5" /> : <Copy className="w-3.5 h-3.5" />}
              <span>{copied ? "Copied!" : "Copy Trace"}</span>
            </button>
          </div>

          <pre className="p-4 bg-slate-950 border border-slate-800 rounded-xl text-xs font-mono text-emerald-300 leading-relaxed overflow-x-auto whitespace-pre-wrap break-words select-text">
            {result.output || "// (No stack trace lines returned)"}
          </pre>
        </div>
      )}
    </div>
  );
};
