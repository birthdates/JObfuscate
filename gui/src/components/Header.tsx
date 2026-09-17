import React from "react";
import { UserProfile } from "../types/config";
import { Download, Upload, Plus, Bookmark, CheckCircle2, AlertCircle } from "lucide-react";
import logoSvg from "../assets/logo.svg";

import { CustomSelect, SelectOption } from "./CustomSelect";

interface HeaderProps {
  activeProfileId: string;
  profiles: UserProfile[];
  onSelectProfile: (id: string) => void;
  onCreateProfile: () => void;
  onImportJson: () => void;
  onExportJson: () => void;
  cliBinaryPath: string | null;
  cliError: string | null;
}

export const Header: React.FC<HeaderProps> = ({
  activeProfileId,
  profiles,
  onSelectProfile,
  onCreateProfile,
  onImportJson,
  onExportJson,
  cliBinaryPath,
  cliError,
}) => {
  const profileOptions: SelectOption[] = profiles.map((p) => ({
    value: p.id,
    label: p.name,
    badge: p.isBuiltIn ? "Built-in" : "Custom",
  }));
  return (
    <header className="flex flex-wrap items-center justify-between border-b border-slate-800/80 pb-4 gap-4">
      {/* Brand & Identity */}
      <div className="flex items-center gap-3.5">
        <div className="h-11 w-11 flex-shrink-0 drop-shadow-[0_0_12px_rgba(56,189,248,0.4)]" style={{ width: "44px", height: "44px", maxWidth: "44px", maxHeight: "44px" }}>
          <img src={logoSvg} alt="JObfuscate Logo" className="w-full h-full object-contain" style={{ width: "44px", height: "44px" }} />
        </div>
        <div>
          <div className="flex items-center gap-2">
            <h1 className="text-xl font-bold tracking-tight text-white">JObfuscate</h1>
            <span className="text-xs px-2 py-0.5 rounded-full bg-blue-500/20 text-blue-400 border border-blue-500/30 font-semibold font-mono">
              Java 8 – 25
            </span>
          </div>
          <p className="text-xs text-slate-400">
            Enterprise bytecode hardening & decompiler neutralization suite
          </p>
        </div>
      </div>

      {/* Profile Management & Quick Actions */}
      <div className="flex items-center gap-3 flex-wrap">
        {/* Profile Selector Dropdown */}
        <div className="flex items-center bg-slate-900 border border-slate-800 rounded-lg px-2 py-1 gap-2 text-xs">
          <Bookmark className="w-3.5 h-3.5 text-blue-400" />
          <span className="text-slate-400 font-medium">Profile:</span>
          <CustomSelect
            value={activeProfileId}
            onChange={onSelectProfile}
            options={profileOptions}
            menuWidth="w-60"
          />
          <button
            onClick={onCreateProfile}
            title="Save current settings as new profile"
            className="p-1 hover:bg-slate-800 rounded text-slate-400 hover:text-white transition-colors"
          >
            <Plus className="w-3.5 h-3.5 text-emerald-400" />
          </button>
        </div>

        {/* Import & Export JSON */}
        <div className="flex items-center gap-2">
          <button
            onClick={onImportJson}
            className="px-3 py-1.5 rounded-lg text-xs bg-slate-900 hover:bg-slate-800 text-slate-300 border border-slate-700 font-medium transition-colors flex items-center gap-1.5 cursor-pointer"
          >
            <Upload className="w-3.5 h-3.5 text-blue-400" />
            Import JSON
          </button>
          <button
            onClick={onExportJson}
            className="px-3 py-1.5 rounded-lg text-xs bg-slate-900 hover:bg-slate-800 text-slate-300 border border-slate-700 font-medium transition-colors flex items-center gap-1.5 cursor-pointer"
          >
            <Download className="w-3.5 h-3.5 text-blue-400" />
            Export JSON
          </button>

          {/* Engine Status Indicator */}
          <div
            className="flex items-center gap-1.5 px-2.5 py-1.5 rounded-lg bg-slate-900 border border-slate-800 text-xs"
            title={cliBinaryPath || cliError || "Checking engine status..."}
          >
            {cliBinaryPath ? (
              <>
                <CheckCircle2 className="w-3.5 h-3.5 text-emerald-400" />
                <span className="font-mono text-slate-300">CLI Ready</span>
              </>
            ) : (
              <>
                <AlertCircle className="w-3.5 h-3.5 text-amber-400" />
                <span className="font-mono text-amber-300">CLI Missing</span>
              </>
            )}
          </div>
        </div>
      </div>
    </header>
  );
};
