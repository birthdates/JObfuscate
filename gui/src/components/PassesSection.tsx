import React from "react";
import { ObfuscatorConfig } from "../types/config";
import { Sliders } from "lucide-react";
import { CustomSelect, SelectOption } from "./CustomSelect";

const RENAME_MODE_OPTIONS: SelectOption[] = [
  { value: "long", label: "long", sublabel: "(Il1lI_0x9f)" },
  { value: "confusing", label: "confusing", sublabel: "(_0OoO)" },
  { value: "short", label: "short", sublabel: "(a, b, c)" },
  { value: "hex", label: "hex", sublabel: "(_a7f3c)" },
];

const STRING_ALGO_OPTIONS: SelectOption[] = [
  { value: "MultiLayerCrypto", label: "MultiLayer Crypto", sublabel: "(High Security)" },
  { value: "XorRollingKey", label: "Fast Rolling XOR", sublabel: "(Low Overhead)" },
];

interface PassesSectionProps {
  config: ObfuscatorConfig;
  onChangeConfig: (newConfig: ObfuscatorConfig) => void;
}

export const PassesSection: React.FC<PassesSectionProps> = ({ config, onChangeConfig }) => {
  const updateStrip = (field: keyof typeof config.strip, val: boolean) => {
    onChangeConfig({
      ...config,
      strip: { ...config.strip, [field]: val },
    });
  };

  const updateRename = (field: keyof typeof config.rename, val: any) => {
    onChangeConfig({
      ...config,
      rename: { ...config.rename, [field]: val },
    });
  };

  const updateStrings = (field: keyof typeof config.string_encryption, val: any) => {
    onChangeConfig({
      ...config,
      string_encryption: { ...config.string_encryption, [field]: val },
    });
  };

  const updateReflection = (field: keyof typeof config.reflection_proxy, val: any) => {
    onChangeConfig({
      ...config,
      reflection_proxy: { ...config.reflection_proxy, [field]: val },
    });
  };

  const updateNumbers = (field: keyof typeof config.number_obfuscation, val: any) => {
    onChangeConfig({
      ...config,
      number_obfuscation: { ...config.number_obfuscation, [field]: val },
    });
  };

  const updateControlFlow = (field: keyof typeof config.control_flow, val: any) => {
    onChangeConfig({
      ...config,
      control_flow: { ...config.control_flow, [field]: val },
    });
  };

  const updateIndy = (field: keyof typeof config.indy_call_sites, val: any) => {
    onChangeConfig({
      ...config,
      indy_call_sites: { ...config.indy_call_sites, [field]: val },
    });
  };

  const updateShrink = (field: keyof typeof config.shrink, val: any) => {
    onChangeConfig({
      ...config,
      shrink: { ...config.shrink, [field]: val },
    });
  };

  const updateClassEncryption = (field: keyof typeof config.class_encryption, val: any) => {
    onChangeConfig({
      ...config,
      class_encryption: { ...config.class_encryption, [field]: val },
    });
  };

  const updateCondy = (field: keyof typeof config.condy, val: any) => {
    onChangeConfig({
      ...config,
      condy: { ...config.condy, [field]: val },
    });
  };

  const updateDecompilerDeterrence = (field: keyof typeof config.decompiler_deterrence, val: any) => {
    onChangeConfig({
      ...config,
      decompiler_deterrence: { ...config.decompiler_deterrence, [field]: val },
    });
  };

  return (
    <section className="space-y-3">
      <div className="flex items-center justify-between">
        <h2 className="text-xs font-bold uppercase tracking-wider text-slate-300 flex items-center gap-2">
          <Sliders className="w-4 h-4 text-indigo-400" />
          Obfuscation Passes & Deep Hardening
        </h2>
        <span className="text-[11px] text-slate-500 font-mono">12 Configurable Passes</span>
      </div>

      {/* PASS 01: Debug Stripping */}
      <div className="bg-slate-900 border border-slate-800 rounded-xl p-4 transition-all hover:border-slate-700">
        <div className="flex items-center justify-between mb-2.5">
          <div className="flex items-center gap-3">
            <span className="w-7 h-7 rounded-lg bg-blue-500/10 text-blue-400 flex items-center justify-center font-bold text-xs">
              01
            </span>
            <div>
              <h3 className="text-xs font-bold text-white">Debug Stripping (StripDebug)</h3>
              <p className="text-[11px] text-slate-400">
                Strips LineNumberTable, LocalVariableTable, SourceFile, and debug attributes.
              </p>
            </div>
          </div>
          <label className="relative inline-flex items-center cursor-pointer">
            <input
              type="checkbox"
              checked={
                config.strip.strip_line_numbers ||
                config.strip.strip_local_variables ||
                config.strip.strip_source_file
              }
              onChange={(e) => {
                const checked = e.target.checked;
                onChangeConfig({
                  ...config,
                  strip: {
                    ...config.strip,
                    strip_line_numbers: checked,
                    strip_local_variables: checked,
                    strip_source_file: checked,
                    strip_source_debug: checked,
                    strip_parameters: checked,
                  },
                });
              }}
              className="sr-only peer"
            />
            <div className="w-11 h-6 bg-slate-800 rounded-full peer peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:rounded-full after:h-5 after:w-5 after:transition-all peer-checked:bg-blue-600"></div>
          </label>
        </div>
        <div className="grid grid-cols-2 md:grid-cols-4 gap-2 pt-2 border-t border-slate-800 text-[11px]">
          <label className="flex items-center gap-1.5 text-slate-300 cursor-pointer">
            <input
              type="checkbox"
              checked={config.strip.strip_line_numbers}
              onChange={(e) => updateStrip("strip_line_numbers", e.target.checked)}
              className="rounded bg-slate-950 border-slate-700 text-blue-600"
            />
            Line Numbers
          </label>
          <label className="flex items-center gap-1.5 text-slate-300 cursor-pointer">
            <input
              type="checkbox"
              checked={config.strip.strip_local_variables}
              onChange={(e) => updateStrip("strip_local_variables", e.target.checked)}
              className="rounded bg-slate-950 border-slate-700 text-blue-600"
            />
            Local Variables
          </label>
          <label className="flex items-center gap-1.5 text-slate-300 cursor-pointer">
            <input
              type="checkbox"
              checked={config.strip.strip_source_file}
              onChange={(e) => updateStrip("strip_source_file", e.target.checked)}
              className="rounded bg-slate-950 border-slate-700 text-blue-600"
            />
            Source File
          </label>
          <label className="flex items-center gap-1.5 text-slate-300 cursor-pointer">
            <input
              type="checkbox"
              checked={config.strip.strip_signatures}
              onChange={(e) => updateStrip("strip_signatures", e.target.checked)}
              className="rounded bg-slate-950 border-slate-700 text-blue-600"
            />
            Signatures (--strip-signatures)
          </label>
          <label className="flex items-center gap-1.5 text-slate-300 cursor-pointer" title="Drop kotlin.Metadata name tables (leak original identifiers)">
            <input
              type="checkbox"
              checked={config.strip.strip_kotlin_metadata}
              onChange={(e) => updateStrip("strip_kotlin_metadata", e.target.checked)}
              className="rounded bg-slate-950 border-slate-700 text-blue-600"
            />
            Kotlin Metadata (--strip-kotlin-metadata)
          </label>
        </div>
      </div>

      {/* PASS 02: Access Reducer (Default: ON) */}
      <div className="bg-slate-900 border border-slate-800 rounded-xl p-4 transition-all hover:border-slate-700">
        <div className="flex items-center justify-between">
          <div className="flex items-center gap-3">
            <span className="w-7 h-7 rounded-lg bg-emerald-500/10 text-emerald-400 flex items-center justify-center font-bold text-xs">
              02
            </span>
            <div>
              <div className="flex items-center gap-2">
                <h3 className="text-xs font-bold text-white">Access Reduction (Auto-Privatization)</h3>
                <span className="text-[10px] bg-emerald-500/20 text-emerald-300 px-1.5 py-0.5 rounded border border-emerald-500/30 font-semibold font-mono">
                  Default: ON
                </span>
              </div>
              <p className="text-[11px] text-slate-400">
                Demotes unused public/protected methods and fields directly to private.
              </p>
            </div>
          </div>
          <label className="relative inline-flex items-center cursor-pointer">
            <input
              type="checkbox"
              checked={config.access_reducer.enabled}
              onChange={(e) =>
                onChangeConfig({
                  ...config,
                  access_reducer: { enabled: e.target.checked },
                })
              }
              className="sr-only peer"
            />
            <div className="w-11 h-6 bg-slate-800 rounded-full peer peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:rounded-full after:h-5 after:w-5 after:transition-all peer-checked:bg-emerald-600"></div>
          </label>
        </div>
      </div>

      {/* PASS 03: Dynamic Reflection Proxy */}
      <div className="bg-slate-900 border border-slate-800 rounded-xl p-4 transition-all hover:border-slate-700">
        <div className="flex items-center justify-between mb-3">
          <div className="flex items-center gap-3">
            <span className="w-7 h-7 rounded-lg bg-purple-500/10 text-purple-400 flex items-center justify-center font-bold text-xs">
              03
            </span>
            <div>
              <h3 className="text-xs font-bold text-white">Dynamic Reflection Proxy (ReflectionProxy)</h3>
              <p className="text-[11px] text-slate-400">
                Rewrites external invocations into runtime reflection, sanitizing Constant Pool imports.
              </p>
            </div>
          </div>
          <label className="relative inline-flex items-center cursor-pointer">
            <input
              type="checkbox"
              checked={config.reflection_proxy.enabled}
              onChange={(e) => updateReflection("enabled", e.target.checked)}
              className="sr-only peer"
            />
            <div className="w-11 h-6 bg-slate-800 rounded-full peer peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:rounded-full after:h-5 after:w-5 after:transition-all peer-checked:bg-purple-600"></div>
          </label>
        </div>
        <div className="pt-2 border-t border-slate-800 space-y-2 text-xs">
          <div>
            <label className="block text-[11px] text-slate-400 font-medium mb-1">
              Packages to Proxy via Reflection (<code className="text-purple-400 font-mono">--import-packages</code>)
            </label>
            <input
              type="text"
              value={config.reflection_proxy.import_packages.join(", ")}
              onChange={(e) =>
                updateReflection(
                  "import_packages",
                  e.target.value
                    .split(",")
                    .map((s) => s.trim())
                    .filter(Boolean)
                )
              }
              placeholder="* or com.*"
              className="w-full bg-slate-950 border border-slate-800 rounded-lg px-2.5 py-1 text-xs text-slate-200 font-mono focus:outline-none focus:border-purple-500"
            />
            <span className="text-[10px] text-slate-500">
              Default: <code className="text-slate-400">*</code> (proxies all external packages, wiping decompiler imports)
            </span>
          </div>
          <div className="flex items-center gap-2 pt-1">
            <input
              type="checkbox"
              id="chk-import-java"
              checked={config.reflection_proxy.proxy_java_imports}
              onChange={(e) => updateReflection("proxy_java_imports", e.target.checked)}
              className="rounded bg-slate-950 border-slate-700 text-purple-600 focus:ring-0 cursor-pointer"
            />
            <label htmlFor="chk-import-java" className="text-[11px] text-slate-300 cursor-pointer">
              Proxy standard <code className="text-purple-400 font-mono">java.*</code>,{" "}
              <code className="text-purple-400 font-mono">javax.*</code>, and{" "}
              <code className="text-purple-400 font-mono">kotlin.*</code> APIs (<code className="text-purple-400 font-mono">--import-java</code>)
            </label>
          </div>
          <div className="flex flex-wrap items-center gap-2 pt-1">
            <span className="text-[11px] text-slate-400 font-medium">Shared lookup helpers:</span>
            <span className="text-[10px] text-slate-500 font-mono hidden sm:inline">
              (--mh-lookup-min / max)
            </span>
            <div className="flex items-center gap-1.5 bg-slate-950 border border-slate-800 rounded-lg px-2.5 py-1">
              <span className="text-[10px] uppercase font-bold text-slate-500 tracking-wider">Min</span>
              <input
                type="number"
                min={1}
                max={config.reflection_proxy.mh_lookup_helpers_max || 16}
                value={config.reflection_proxy.mh_lookup_helpers_min}
                onChange={(e) => {
                  const val = Math.max(1, parseInt(e.target.value) || 1);
                  onChangeConfig({
                    ...config,
                    reflection_proxy: {
                      ...config.reflection_proxy,
                      mh_lookup_helpers_min: val,
                      mh_lookup_helpers_max: Math.max(val, config.reflection_proxy.mh_lookup_helpers_max),
                    },
                  });
                }}
                className="w-12 bg-slate-900 border border-slate-700 rounded px-1.5 py-0.5 text-xs text-purple-300 font-mono text-center focus:outline-none focus:border-purple-500"
              />
              <span className="text-slate-600 font-mono text-xs">–</span>
              <span className="text-[10px] uppercase font-bold text-slate-500 tracking-wider">Max</span>
              <input
                type="number"
                min={config.reflection_proxy.mh_lookup_helpers_min || 1}
                max={16}
                value={config.reflection_proxy.mh_lookup_helpers_max}
                onChange={(e) => {
                  const val = Math.max(1, parseInt(e.target.value) || 1);
                  onChangeConfig({
                    ...config,
                    reflection_proxy: {
                      ...config.reflection_proxy,
                      mh_lookup_helpers_max: val,
                      mh_lookup_helpers_min: Math.min(val, config.reflection_proxy.mh_lookup_helpers_min),
                    },
                  });
                }}
                className="w-12 bg-slate-900 border border-slate-700 rounded px-1.5 py-0.5 text-xs text-purple-300 font-mono text-center focus:outline-none focus:border-purple-500"
              />
              <span className="text-[11px] text-slate-400 font-mono pl-1">helpers in cache class</span>
            </div>
          </div>
        </div>
      </div>

      {/* PASS 04: Number Obfuscation */}
      <div className="bg-slate-900 border border-slate-800 rounded-xl p-4 transition-all hover:border-slate-700">
        <div className="flex items-center justify-between mb-2">
          <div className="flex items-center gap-3">
            <span className="w-7 h-7 rounded-lg bg-cyan-500/10 text-cyan-400 flex items-center justify-center font-bold text-xs">
              04
            </span>
            <div>
              <h3 className="text-xs font-bold text-white">Number Obfuscation (NumberObfuscation)</h3>
              <p className="text-[11px] text-slate-400">
                Splits integer constants into equivalent bitwise, XOR shifts, and arithmetic identity expressions.
              </p>
            </div>
          </div>
          <label className="relative inline-flex items-center cursor-pointer">
            <input
              type="checkbox"
              checked={config.number_obfuscation.enabled}
              onChange={(e) => updateNumbers("enabled", e.target.checked)}
              className="sr-only peer"
            />
            <div className="w-11 h-6 bg-slate-800 rounded-full peer peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:rounded-full after:h-5 after:w-5 after:transition-all peer-checked:bg-cyan-600"></div>
          </label>
        </div>
        <div className="flex items-center gap-3 pt-2 border-t border-slate-800 text-xs">
          <span className="text-slate-400 text-[11px]">Intensity:</span>
          <input
            type="range"
            min="10"
            max="100"
            value={config.number_obfuscation.intensity}
            onChange={(e) => updateNumbers("intensity", parseInt(e.target.value))}
            className="w-full accent-cyan-500 h-1 bg-slate-800 rounded-lg cursor-pointer"
          />
          <span className="text-[11px] font-mono text-cyan-400 font-semibold w-10 text-right">
            {config.number_obfuscation.intensity}%
          </span>
        </div>
      </div>

      {/* PASS 05: Identifier Renaming */}
      <div className="bg-slate-900 border border-slate-800 rounded-xl p-4 transition-all hover:border-slate-700">
        <div className="flex items-center justify-between mb-3">
          <div className="flex items-center gap-3">
            <span className="w-7 h-7 rounded-lg bg-blue-500/10 text-blue-400 flex items-center justify-center font-bold text-xs">
              05
            </span>
            <div>
              <h3 className="text-xs font-bold text-white">Identifier Renaming (RenamePass)</h3>
              <p className="text-[11px] text-slate-400">
                Scrambles class, method, field, and package names into confusing homoglyphs.
              </p>
            </div>
          </div>
          <label className="relative inline-flex items-center cursor-pointer">
            <input
              type="checkbox"
              checked={config.rename.enabled}
              onChange={(e) => updateRename("enabled", e.target.checked)}
              className="sr-only peer"
            />
            <div className="w-11 h-6 bg-slate-800 rounded-full peer peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:rounded-full after:h-5 after:w-5 after:transition-all peer-checked:bg-blue-600"></div>
          </label>
        </div>
        <div className="grid grid-cols-2 md:grid-cols-4 gap-2 pt-2 border-t border-slate-800 text-[11px]">
          <label className="flex items-center gap-1.5 text-slate-300 cursor-pointer">
            <input
              type="checkbox"
              checked={config.rename.rename_classes}
              onChange={(e) => updateRename("rename_classes", e.target.checked)}
              className="rounded bg-slate-950 border-slate-700 text-blue-600"
            />
            Classes (--rename-classes)
          </label>
          <label className="flex items-center gap-1.5 text-slate-300 cursor-pointer">
            <input
              type="checkbox"
              checked={config.rename.rename_methods && config.rename.rename_fields}
              onChange={(e) => {
                const val = e.target.checked;
                onChangeConfig({
                  ...config,
                  rename: {
                    ...config.rename,
                    rename_methods: val,
                    rename_fields: val,
                  },
                });
              }}
              className="rounded bg-slate-950 border-slate-700 text-blue-600"
            />
            Methods & Fields
          </label>
          <label className="flex items-center gap-1.5 text-slate-300 cursor-pointer">
            <input
              type="checkbox"
              checked={config.rename.obfuscate_enums}
              onChange={(e) => updateRename("obfuscate_enums", e.target.checked)}
              className="rounded bg-slate-950 border-slate-700 text-blue-600"
            />
            Enums (--obfuscate-enums)
          </label>
          <label className="flex items-center gap-1.5 text-slate-300 cursor-pointer">
            <input
              type="checkbox"
              checked={config.rename.remove_packages}
              onChange={(e) => updateRename("remove_packages", e.target.checked)}
              className="rounded bg-slate-950 border-slate-700 text-blue-600"
            />
            Flatten (--remove-packages)
          </label>
        </div>
        <div className="grid grid-cols-1 md:grid-cols-4 gap-3 mt-2.5 pt-2 border-t border-slate-800 text-xs">
          <div className="flex items-center justify-between gap-2">
            <span className="text-slate-400 text-[11px]">Mode (--rename-mode):</span>
            <CustomSelect
              value={config.rename.mode}
              onChange={(val) => updateRename("mode", val as any)}
              options={RENAME_MODE_OPTIONS}
              menuWidth="w-52"
            />
          </div>
          <div className="flex items-center gap-2">
            <input
              type="checkbox"
              id="chk-keep-main"
              checked={config.rename.keep_main_class}
              onChange={(e) => updateRename("keep_main_class", e.target.checked)}
              className="rounded bg-slate-950 border-slate-700 text-blue-600 cursor-pointer"
            />
            <label htmlFor="chk-keep-main" className="text-[11px] text-slate-300 cursor-pointer">
              Preserve Main-Class (--keep-main-class)
            </label>
          </div>
          <div className="flex items-center gap-2">
            <input
              type="checkbox"
              id="chk-scramble-methods"
              checked={config.rename.scramble_methods}
              onChange={(e) => updateRename("scramble_methods", e.target.checked)}
              className="rounded bg-slate-950 border-slate-700 text-blue-600 cursor-pointer"
            />
            <label htmlFor="chk-scramble-methods" className="text-[11px] text-slate-300 cursor-pointer">
              Scramble Methods (--scramble-methods)
            </label>
          </div>
          <div className="flex items-center gap-2">
            <input
              type="checkbox"
              id="chk-remap-resources"
              checked={config.rename.remap_resources}
              onChange={(e) => updateRename("remap_resources", e.target.checked)}
              className="rounded bg-slate-950 border-slate-700 text-blue-600 cursor-pointer"
            />
            <label htmlFor="chk-remap-resources" className="text-[11px] text-slate-300 cursor-pointer" title="Automatically rewrites renamed class names in plugin.yml, paper-plugin.yml, bungee.yml, velocity-plugin.json, and META-INF/services">
              Remap Descriptors (plugin.yml)
            </label>
          </div>
        </div>
      </div>

      {/* PASS 06: String Encryption */}
      <div className="bg-slate-900 border border-slate-800 rounded-xl p-4 transition-all hover:border-slate-700">
        <div className="flex items-center justify-between mb-2">
          <div className="flex items-center gap-3">
            <span className="w-7 h-7 rounded-lg bg-amber-500/10 text-amber-400 flex items-center justify-center font-bold text-xs">
              06
            </span>
            <div>
              <h3 className="text-xs font-bold text-white">String Encryption (StringEncrypt)</h3>
              <p className="text-[11px] text-slate-400">
                Multi-cipher XOR encryption with dynamic decryptor injection and annotation string support.
              </p>
            </div>
          </div>
          <label className="relative inline-flex items-center cursor-pointer">
            <input
              type="checkbox"
              checked={config.string_encryption.enabled}
              onChange={(e) => updateStrings("enabled", e.target.checked)}
              className="sr-only peer"
            />
            <div className="w-11 h-6 bg-slate-800 rounded-full peer peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:rounded-full after:h-5 after:w-5 after:transition-all peer-checked:bg-amber-600"></div>
          </label>
        </div>
        <div className="grid grid-cols-1 md:grid-cols-2 gap-2 pt-2 border-t border-slate-800 text-[11px] text-slate-300">
          <label className="flex items-center gap-1.5 cursor-pointer">
            <input
              type="checkbox"
              checked={config.string_encryption.encrypt_annotation_strings}
              onChange={(e) => updateStrings("encrypt_annotation_strings", e.target.checked)}
              className="rounded bg-slate-950 border-slate-700 text-amber-500 cursor-pointer"
            />
            Encrypt Annotation Strings (--encrypt-annotation-strings)
          </label>
          <label className="flex items-center gap-1.5 cursor-pointer">
            <input
              type="checkbox"
              checked={config.string_encryption.randomize_decryptor_names}
              onChange={(e) => updateStrings("randomize_decryptor_names", e.target.checked)}
              className="rounded bg-slate-950 border-slate-700 text-amber-500 cursor-pointer"
            />
            Randomize Decryptor Names (--randomize-decryptor-names)
          </label>
          <label className="flex items-center gap-1.5 cursor-pointer" title="Build each string decryption key on the stack from split parts (ixor/iadd) instead of pushing a single constant">
            <input
              type="checkbox"
              checked={config.string_encryption.split_keys}
              onChange={(e) => updateStrings("split_keys", e.target.checked)}
              className="rounded bg-slate-950 border-slate-700 text-amber-500 cursor-pointer"
            />
            Split Key Construction (--split-keys)
          </label>
        </div>

        {/* Algorithm Selector */}
        <div className="flex items-center justify-between gap-2 pt-2 border-t border-slate-800">
          <span className="text-slate-400 text-[11px]">Algorithm (--string-algo):</span>
          <CustomSelect
            value={config.string_encryption.algorithm || "MultiLayerCrypto"}
            onChange={(val) => updateStrings("algorithm", val as any)}
            options={STRING_ALGO_OPTIONS}
            menuWidth="w-64"
          />
        </div>

        {/* Configurable Decryptor Batching */}
        <div className="flex flex-wrap items-center justify-between gap-3 mt-2.5 pt-2 border-t border-slate-800/80 text-xs">
          <div className="flex items-center gap-2">
            <span className="text-slate-400 text-[11px] font-medium">Batching:</span>
            <span className="text-[10px] text-slate-500 font-mono hidden sm:inline">
              (--strings-per-decryptor-min / max)
            </span>
          </div>

          <div className="flex flex-wrap items-center gap-2">
            <div className="flex items-center gap-1.5 bg-slate-950 border border-slate-800 rounded-lg px-2.5 py-1">
              <span className="text-[10px] uppercase font-bold text-slate-500 tracking-wider">Min</span>
              <input
                type="number"
                min={1}
                max={config.string_encryption.strings_per_decryptor_max || 100}
                value={config.string_encryption.strings_per_decryptor_min}
                onChange={(e) => {
                  const val = Math.max(1, parseInt(e.target.value) || 1);
                  onChangeConfig({
                    ...config,
                    string_encryption: {
                      ...config.string_encryption,
                      strings_per_decryptor_min: val,
                      strings_per_decryptor_max: Math.max(val, config.string_encryption.strings_per_decryptor_max),
                    },
                  });
                }}
                className="w-12 bg-slate-900 border border-slate-700 rounded px-1.5 py-0.5 text-xs text-amber-300 font-mono text-center focus:outline-none focus:border-amber-500"
              />
              <span className="text-slate-600 font-mono text-xs">–</span>
              <span className="text-[10px] uppercase font-bold text-slate-500 tracking-wider">Max</span>
              <input
                type="number"
                min={config.string_encryption.strings_per_decryptor_min || 1}
                max={500}
                value={config.string_encryption.strings_per_decryptor_max}
                onChange={(e) => {
                  const val = Math.max(1, parseInt(e.target.value) || 1);
                  onChangeConfig({
                    ...config,
                    string_encryption: {
                      ...config.string_encryption,
                      strings_per_decryptor_max: val,
                      strings_per_decryptor_min: Math.min(val, config.string_encryption.strings_per_decryptor_min),
                    },
                  });
                }}
                className="w-12 bg-slate-900 border border-slate-700 rounded px-1.5 py-0.5 text-xs text-amber-300 font-mono text-center focus:outline-none focus:border-amber-500"
              />
              <span className="text-[11px] text-slate-400 font-mono pl-1">strings / decryptor</span>
            </div>

            {/* Quick Preset Buttons */}
            <div className="flex items-center gap-1">
              {[
                { label: "1:1", min: 1, max: 1, title: "1 decryptor per string (Isolated / Maximum Dispersion)" },
                { label: "5–10", min: 5, max: 10, title: "5–10 strings per decryptor (Balanced Default)" },
                { label: "15–25", min: 15, max: 25, title: "15–25 strings per decryptor (Dense Batching / Low Overhead)" },
              ].map((preset) => {
                const isActive =
                  config.string_encryption.strings_per_decryptor_min === preset.min &&
                  config.string_encryption.strings_per_decryptor_max === preset.max;
                return (
                  <button
                    key={preset.label}
                    type="button"
                    title={preset.title}
                    onClick={() =>
                      onChangeConfig({
                        ...config,
                        string_encryption: {
                          ...config.string_encryption,
                          strings_per_decryptor_min: preset.min,
                          strings_per_decryptor_max: preset.max,
                        },
                      })
                    }
                    className={`px-2 py-1 rounded text-[10px] font-mono border transition-all ${
                      isActive
                        ? "bg-amber-500/20 border-amber-500/60 text-amber-300 font-bold"
                        : "bg-slate-950 border-slate-800 text-slate-400 hover:border-slate-700 hover:text-slate-300"
                    }`}
                  >
                    {preset.label}
                  </button>
                );
              })}
            </div>
          </div>
        </div>
      </div>

      {/* PASS 07: Control Flow Obfuscation */}
      <div className="bg-slate-900 border border-slate-800 rounded-xl p-4 transition-all hover:border-slate-700">
        <div className="flex items-center justify-between mb-2">
          <div className="flex items-center gap-3">
            <span className="w-7 h-7 rounded-lg bg-indigo-500/10 text-indigo-400 flex items-center justify-center font-bold text-xs">
              07
            </span>
            <div>
              <h3 className="text-xs font-bold text-white">Control Flow & Decompiler Killers (ControlFlow)</h3>
              <p className="text-[11px] text-slate-400">
                Chenxi Wang switch flattening, MBA transitions, and decompiler traps (irreducible loops & overlapping exception handlers).
              </p>
            </div>
          </div>
          <label className="relative inline-flex items-center cursor-pointer">
            <input
              type="checkbox"
              checked={config.control_flow.enabled}
              onChange={(e) => updateControlFlow("enabled", e.target.checked)}
              className="sr-only peer"
            />
            <div className="w-11 h-6 bg-slate-800 rounded-full peer peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:rounded-full after:h-5 after:w-5 after:transition-all peer-checked:bg-indigo-600"></div>
          </label>
        </div>

        <div className="grid grid-cols-1 md:grid-cols-3 gap-2 pt-2 border-t border-slate-800 text-[11px] text-slate-300">
          <label className="flex items-center gap-1.5 cursor-pointer">
            <input
              type="checkbox"
              checked={config.control_flow.flatten_blocks}
              onChange={(e) => updateControlFlow("flatten_blocks", e.target.checked)}
              className="rounded bg-slate-950 border-slate-700 text-indigo-500 cursor-pointer"
            />
            Switch Flattening (--flatten-control-flow)
          </label>
          <label className="flex items-center gap-1.5 cursor-pointer">
            <input
              type="checkbox"
              checked={config.control_flow.decompiler_traps}
              onChange={(e) => updateControlFlow("decompiler_traps", e.target.checked)}
              className="rounded bg-slate-950 border-slate-700 text-indigo-500 cursor-pointer"
            />
            Decompiler Traps (--decompiler-traps)
          </label>
          <label className="flex items-center gap-1.5 cursor-pointer">
            <input
              type="checkbox"
              checked={config.control_flow.insert_opaque_predicates}
              onChange={(e) => updateControlFlow("insert_opaque_predicates", e.target.checked)}
              className="rounded bg-slate-950 border-slate-700 text-indigo-500 cursor-pointer"
            />
            Opaque Predicates
          </label>
          <label className="flex items-center gap-1.5 cursor-pointer" title="Route decoy edges through try/catch handlers with dead irreducible loops">
            <input
              type="checkbox"
              checked={config.control_flow.exception_dispatch}
              onChange={(e) => updateControlFlow("exception_dispatch", e.target.checked)}
              className="rounded bg-slate-950 border-slate-700 text-indigo-500 cursor-pointer"
            />
            Exception Dispatch (--exception-dispatch)
          </label>
        </div>

        <div className="flex items-center gap-3 pt-2 border-t border-slate-800 text-xs mt-2">
          <span className="text-slate-400 text-[11px]">Flattening Intensity:</span>
          <input
            type="range"
            min="20"
            max="100"
            value={config.control_flow.flattening_intensity}
            onChange={(e) => updateControlFlow("flattening_intensity", parseInt(e.target.value))}
            className="w-full accent-indigo-500 h-1 bg-slate-800 rounded-lg cursor-pointer"
          />
          <span className="text-[11px] font-mono text-indigo-400 font-semibold w-10 text-right">
            {config.control_flow.flattening_intensity}%
          </span>
        </div>
      </div>

      {/* PASS 08: InvokeDynamic Call Sites */}
      <div className="bg-slate-900 border border-slate-800 rounded-xl p-4 transition-all hover:border-slate-700">
        <div className="flex items-center justify-between mb-2">
          <div className="flex items-center gap-3">
            <span className="w-7 h-7 rounded-lg bg-pink-500/10 text-pink-400 flex items-center justify-center font-bold text-xs">
              08
            </span>
            <div>
              <h3 className="text-xs font-bold text-white">Dynamic InvokeDynamic Call Sites (IndyCallSites)</h3>
              <p className="text-[11px] text-slate-400">
                Converts static, virtual, and interface invocations to invokedynamic with encrypted runtime BSM caching.
              </p>
            </div>
          </div>
          <label className="relative inline-flex items-center cursor-pointer">
            <input
              type="checkbox"
              checked={config.indy_call_sites.enabled}
              onChange={(e) => updateIndy("enabled", e.target.checked)}
              className="sr-only peer"
            />
            <div className="w-11 h-6 bg-slate-800 rounded-full peer peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:rounded-full after:h-5 after:w-5 after:transition-all peer-checked:bg-pink-600"></div>
          </label>
        </div>
      </div>

      {/* PASS 09: Code Shrinking & Tree Shaking */}
      <div className="bg-slate-900 border border-slate-800 rounded-xl p-4 transition-all hover:border-slate-700">
        <div className="flex items-center justify-between mb-2">
          <div className="flex items-center gap-3">
            <span className="w-7 h-7 rounded-lg bg-emerald-500/10 text-emerald-400 flex items-center justify-center font-bold text-xs">
              09
            </span>
            <div>
              <h3 className="text-xs font-bold text-white">Code Shrinking & Tree Shaking (ShrinkerPass)</h3>
              <p className="text-[11px] text-slate-400">
                Detects and eliminates dead classes, unreachable methods, unused fields, and non-essential attributes.
              </p>
            </div>
          </div>
          <label className="relative inline-flex items-center cursor-pointer">
            <input
              type="checkbox"
              checked={config.shrink.enabled}
              onChange={(e) => updateShrink("enabled", e.target.checked)}
              className="sr-only peer"
            />
            <div className="w-11 h-6 bg-slate-800 rounded-full peer peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:rounded-full after:h-5 after:w-5 after:transition-all peer-checked:bg-emerald-600"></div>
          </label>
        </div>

        <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-2 pt-2 border-t border-slate-800 text-[11px] text-slate-300">
          <label className="flex items-center gap-1.5 cursor-pointer">
            <input
              type="checkbox"
              checked={config.shrink.shrink_classes}
              onChange={(e) => updateShrink("shrink_classes", e.target.checked)}
              className="rounded bg-slate-950 border-slate-700 text-emerald-500 cursor-pointer"
            />
            Dead Class Removal
          </label>
          <label className="flex items-center gap-1.5 cursor-pointer">
            <input
              type="checkbox"
              checked={config.shrink.shrink_methods}
              onChange={(e) => updateShrink("shrink_methods", e.target.checked)}
              className="rounded bg-slate-950 border-slate-700 text-emerald-500 cursor-pointer"
            />
            Dead Method Pruning
          </label>
          <label className="flex items-center gap-1.5 cursor-pointer">
            <input
              type="checkbox"
              checked={config.shrink.shrink_fields}
              onChange={(e) => updateShrink("shrink_fields", e.target.checked)}
              className="rounded bg-slate-950 border-slate-700 text-emerald-500 cursor-pointer"
            />
            Unused Field Pruning
          </label>
          <label className="flex items-center gap-1.5 cursor-pointer">
            <input
              type="checkbox"
              checked={config.shrink.remove_unused_attributes}
              onChange={(e) => updateShrink("remove_unused_attributes", e.target.checked)}
              className="rounded bg-slate-950 border-slate-700 text-emerald-500 cursor-pointer"
            />
            Attribute Stripping
          </label>
        </div>
      </div>

      {/* PASS 10: Class Encryption & In-Memory ClassLoader */}
      <div className="bg-slate-900 border border-slate-800 rounded-xl p-4 transition-all hover:border-slate-700">
        <div className="flex items-center justify-between mb-2">
          <div className="flex items-center gap-3">
            <span className="w-7 h-7 rounded-lg bg-violet-500/10 text-violet-400 flex items-center justify-center font-bold text-xs">
              10
            </span>
            <div>
              <h3 className="text-xs font-bold text-white">Class Encryption & In-Memory Launcher (JarCrypter)</h3>
              <p className="text-[11px] text-slate-400">
                Encrypts all classes into a protected binary container; boots via an in-memory ClassLoader so decompilers see 0 classes.
              </p>
            </div>
          </div>
          <label className="relative inline-flex items-center cursor-pointer">
            <input
              type="checkbox"
              checked={config.class_encryption.enabled}
              onChange={(e) => updateClassEncryption("enabled", e.target.checked)}
              className="sr-only peer"
            />
            <div className="w-11 h-6 bg-slate-800 rounded-full peer peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:rounded-full after:h-5 after:w-5 after:transition-all peer-checked:bg-violet-600"></div>
          </label>
        </div>

        <div className="grid grid-cols-1 md:grid-cols-3 gap-3 pt-2 border-t border-slate-800 text-[11px] text-slate-300">
          <div>
            <label className="block text-[10px] text-slate-400 mb-1">Launcher Class</label>
            <input
              type="text"
              value={config.class_encryption.launcher_class}
              onChange={(e) => updateClassEncryption("launcher_class", e.target.value)}
              placeholder="Bootstrap"
              className="w-full bg-slate-950 border border-slate-800 rounded px-2 py-1 text-xs text-white font-mono focus:border-violet-500 outline-none"
            />
          </div>
          <div>
            <label className="block text-[10px] text-slate-400 mb-1">Resource Path</label>
            <input
              type="text"
              value={config.class_encryption.resource_name}
              onChange={(e) => updateClassEncryption("resource_name", e.target.value)}
              placeholder="META-INF/.data/app.bin"
              className="w-full bg-slate-950 border border-slate-800 rounded px-2 py-1 text-xs text-white font-mono focus:border-violet-500 outline-none"
            />
          </div>
          <div>
            <label className="block text-[10px] text-slate-400 mb-1">Main-Class Override</label>
            <input
              type="text"
              value={config.class_encryption.main_class}
              onChange={(e) => updateClassEncryption("main_class", e.target.value)}
              placeholder="Auto-detect from manifest"
              className="w-full bg-slate-950 border border-slate-800 rounded px-2 py-1 text-xs text-white font-mono focus:border-violet-500 outline-none"
            />
          </div>
        </div>

        <div className="flex items-center gap-1.5 pt-2 border-t border-slate-800 text-[11px] text-slate-300 mt-2">
          <label className="flex items-center gap-1.5 cursor-pointer">
            <input
              type="checkbox"
              checked={config.class_encryption.memory_scrubbing}
              onChange={(e) => updateClassEncryption("memory_scrubbing", e.target.checked)}
              className="rounded bg-slate-950 border-slate-700 text-violet-500 cursor-pointer"
            />
            Memory Scrubbing (Zeroes out decrypted byte arrays in RAM after defineClass)
          </label>
        </div>
      </div>

      {/* PASS 11: ConstantDynamic (Condy) Call Sites */}
      <div className="bg-slate-900 border border-slate-800 rounded-xl p-4 transition-all hover:border-slate-700">
        <div className="flex items-center justify-between">
          <div className="flex items-center gap-3">
            <span className="w-7 h-7 rounded-lg bg-teal-500/10 text-teal-400 flex items-center justify-center font-bold text-xs">
              11
            </span>
            <div>
              <h3 className="text-xs font-bold text-white">ConstantDynamic (Condy) Call Sites (Java 11+)</h3>
              <p className="text-[11px] text-slate-400">
                Replaces constant literals with dynamic CONSTANT_Dynamic resolution bootstrap methods, confounding decompilers while remaining 100% JVM compliant.
              </p>
            </div>
          </div>
          <label className="relative inline-flex items-center cursor-pointer">
            <input
              type="checkbox"
              checked={config.condy.enabled}
              onChange={(e) => updateCondy("enabled", e.target.checked)}
              className="sr-only peer"
            />
            <div className="w-11 h-6 bg-slate-800 rounded-full peer peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:rounded-full after:h-5 after:w-5 after:transition-all peer-checked:bg-teal-600"></div>
          </label>
        </div>
      </div>

      {/* PASS 12: Standards-Compliant Decompiler Deterrence Suite */}
      <div className="bg-slate-900 border border-slate-800 rounded-xl p-4 transition-all hover:border-slate-700">
        <div className="flex items-center justify-between mb-2">
          <div className="flex items-center gap-3">
            <span className="w-7 h-7 rounded-lg bg-rose-500/10 text-rose-400 flex items-center justify-center font-bold text-xs">
              12
            </span>
            <div>
              <h3 className="text-xs font-bold text-white">Standards-Compliant Decompiler Deterrence Suite</h3>
              <p className="text-[11px] text-slate-400">
                Confound decompilers with strictly valid JVMS constructs: return-type overloading, synthetic bridge flags, phantom inner classes, and custom attributes.
              </p>
            </div>
          </div>
          <label className="relative inline-flex items-center cursor-pointer">
            <input
              type="checkbox"
              checked={config.decompiler_deterrence.enabled}
              onChange={(e) => updateDecompilerDeterrence("enabled", e.target.checked)}
              className="sr-only peer"
            />
            <div className="w-11 h-6 bg-slate-800 rounded-full peer peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:rounded-full after:h-5 after:w-5 after:transition-all peer-checked:bg-rose-600"></div>
          </label>
        </div>

        <div className="grid grid-cols-1 md:grid-cols-2 gap-3 pt-2 border-t border-slate-800 text-[11px] text-slate-300">
          <label className="flex items-center gap-1.5 cursor-pointer">
            <input
              type="checkbox"
              checked={config.decompiler_deterrence.overload_return_types}
              onChange={(e) => updateDecompilerDeterrence("overload_return_types", e.target.checked)}
              className="rounded bg-slate-950 border-slate-700 text-rose-500 cursor-pointer"
            />
            Overload Return Types (JVMS §4.3.3 vs JLS §8.4.2)
          </label>

          <label className="flex items-center gap-1.5 cursor-pointer">
            <input
              type="checkbox"
              checked={config.decompiler_deterrence.synthetic_bridge_tags}
              onChange={(e) => updateDecompilerDeterrence("synthetic_bridge_tags", e.target.checked)}
              className="rounded bg-slate-950 border-slate-700 text-rose-500 cursor-pointer"
            />
            Synthetic & Bridge Flags (ACC_SYNTHETIC | ACC_BRIDGE)
          </label>

          <label className="flex items-center gap-1.5 cursor-pointer">
            <input
              type="checkbox"
              checked={config.decompiler_deterrence.phantom_inner_classes}
              onChange={(e) => updateDecompilerDeterrence("phantom_inner_classes", e.target.checked)}
              className="rounded bg-slate-950 border-slate-700 text-rose-500 cursor-pointer"
            />
            Phantom InnerClasses (JVMS §4.7.6)
          </label>

          <label className="flex items-center gap-1.5 cursor-pointer">
            <input
              type="checkbox"
              checked={config.decompiler_deterrence.custom_attributes}
              onChange={(e) => updateDecompilerDeterrence("custom_attributes", e.target.checked)}
              className="rounded bg-slate-950 border-slate-700 text-rose-500 cursor-pointer"
            />
            Custom Attributes (JVMS §4.7.1 $JObfDeterrent)
          </label>

          <label className="flex items-center gap-1.5 cursor-pointer">
            <input
              type="checkbox"
              checked={config.decompiler_deterrence.scramble_method_order}
              onChange={(e) => updateDecompilerDeterrence("scramble_method_order", e.target.checked)}
              className="rounded bg-slate-950 border-slate-700 text-rose-500 cursor-pointer"
            />
            Scramble Method Order (Disorients sequential decompilation)
          </label>
        </div>
      </div>
    </section>
  );
};
