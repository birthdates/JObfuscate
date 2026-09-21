import React from "react";
import { Filter } from "lucide-react";

interface ScopingSectionProps {
  targetPackages: string[];
  excludePackages: string[];
  excludeClasses: string[];
  keepRules: string[];
  ignoreDependencies: boolean;
  onChangeTargetPackages: (pkgs: string[]) => void;
  onChangeExcludePackages: (pkgs: string[]) => void;
  onChangeExcludeClasses: (classes: string[]) => void;
  onChangeKeepRules: (rules: string[]) => void;
  onChangeIgnoreDependencies: (val: boolean) => void;
}

export const ScopingSection: React.FC<ScopingSectionProps> = ({
  targetPackages,
  excludePackages,
  excludeClasses,
  keepRules,
  ignoreDependencies,
  onChangeTargetPackages,
  onChangeExcludePackages,
  onChangeExcludeClasses,
  onChangeKeepRules,
  onChangeIgnoreDependencies,
}) => {
  return (
    <section className="bg-slate-900/90 border border-slate-800 rounded-2xl p-5 shadow-xl">
      <h2 className="text-xs font-bold uppercase tracking-wider text-slate-300 mb-3 flex items-center gap-2">
        <Filter className="w-4 h-4 text-cyan-400" />
        Target Scoping & Package Filtering
      </h2>

      <div className="grid grid-cols-1 md:grid-cols-2 gap-3.5">
        <div>
          <label className="block text-xs text-slate-400 font-medium mb-1">
            In-Scope Packages (<code className="text-blue-400 font-mono">-p / --packages</code>)
          </label>
          <input
            type="text"
            value={targetPackages.join(", ")}
            onChange={(e) =>
              onChangeTargetPackages(
                e.target.value
                  .split(",")
                  .map((s) => s.trim())
                  .filter(Boolean)
              )
            }
            placeholder="* or com.myapp.*"
            className="w-full bg-slate-950 border border-slate-800 rounded-lg px-3 py-1.5 text-xs text-slate-200 font-mono focus:outline-none focus:border-blue-500"
          />
          <span className="text-[10px] text-slate-500">
            Default: <code className="text-slate-400">*</code> (targets all classes across the archive)
          </span>
        </div>

        <div>
          <label className="block text-xs text-slate-400 font-medium mb-1">
            Excluded Packages (<code className="text-blue-400 font-mono">-x / --exclude-packages</code>)
          </label>
          <input
            type="text"
            value={excludePackages.join(", ")}
            onChange={(e) =>
              onChangeExcludePackages(
                e.target.value
                  .split(",")
                  .map((s) => s.trim())
                  .filter(Boolean)
              )
            }
            placeholder="org.slf4j.*, com.testapp.api.*"
            className="w-full bg-slate-950 border border-slate-800 rounded-lg px-3 py-1.5 text-xs text-slate-200 font-mono focus:outline-none focus:border-blue-500"
          />
          <span className="text-[10px] text-slate-500">Comma-separated package wildcards to preserve</span>
        </div>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-2 gap-3.5 mt-3">
        <div>
          <label className="block text-xs text-slate-400 font-medium mb-1">
            Excluded Specific Classes (<code className="text-blue-400 font-mono">--exclude-classes</code>)
          </label>
          <input
            type="text"
            value={excludeClasses.join(", ")}
            onChange={(e) =>
              onChangeExcludeClasses(
                e.target.value
                  .split(",")
                  .map((s) => s.trim())
                  .filter(Boolean)
              )
            }
            placeholder="com.testapp.Config, com.testapp.Plugin"
            className="w-full bg-slate-950 border border-slate-800 rounded-lg px-3 py-1.5 text-xs text-slate-200 font-mono focus:outline-none focus:border-blue-500"
          />
          <span className="text-[10px] text-slate-500">Classes left in original plaintext bytecode</span>
        </div>

        <div className="flex items-center pt-5">
          <label className="flex items-center gap-2 text-xs text-slate-300 cursor-pointer">
            <input
              type="checkbox"
              checked={ignoreDependencies}
              onChange={(e) => onChangeIgnoreDependencies(e.target.checked)}
              className="rounded bg-slate-950 border-slate-700 text-blue-600 focus:ring-0 cursor-pointer"
            />
            <span>
              Auto-ignore shaded dependencies (<code className="text-[11px] text-slate-400 font-mono">--ignore-dependencies</code>)
            </span>
          </label>
        </div>
      </div>

      <div className="mt-3">
        <label className="block text-xs text-slate-400 font-medium mb-1">
          Keep Rules (<code className="text-blue-400 font-mono">--keep / --keep-file</code>, one per line)
        </label>
        <textarea
          value={keepRules.join("\n")}
          onChange={(e) =>
            onChangeKeepRules(
              e.target.value
                .split("\n")
                .map((s) => s.trim())
                .filter(Boolean)
            )
          }
          placeholder={"-keep class com.myapp.Plugin { *; }\n-keepclassmembers class com.myapp.Api { public *; }"}
          rows={3}
          spellCheck={false}
          className="w-full bg-slate-950 border border-slate-800 rounded-lg px-3 py-1.5 text-xs text-slate-200 font-mono focus:outline-none focus:border-blue-500"
        />
        <span className="text-[10px] text-slate-500">
          ProGuard-style: matching classes/members are exempt from shrinking and renaming
        </span>
      </div>
    </section>
  );
};
