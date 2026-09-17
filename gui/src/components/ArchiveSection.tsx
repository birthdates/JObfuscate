import React, { useState } from "react";
import { FolderOpen, Save, FileCode, CheckCircle } from "lucide-react";

interface ArchiveSectionProps {
  inputJar: string;
  outputJar: string;
  onInputChange: (path: string) => void;
  onOutputChange: (path: string) => void;
  onBrowseInput: () => void;
  onBrowseOutput: () => void;
}

export const ArchiveSection: React.FC<ArchiveSectionProps> = ({
  inputJar,
  outputJar,
  onInputChange,
  onOutputChange,
  onBrowseInput,
  onBrowseOutput,
}) => {
  const [isDragging, setIsDragging] = useState(false);

  const handleDragOver = (e: React.DragEvent) => {
    e.preventDefault();
    setIsDragging(true);
  };

  const handleDragLeave = (e: React.DragEvent) => {
    e.preventDefault();
    setIsDragging(false);
  };

  const handleDrop = (e: React.DragEvent) => {
    e.preventDefault();
    setIsDragging(false);
    if (e.dataTransfer.files && e.dataTransfer.files.length > 0) {
      const file = e.dataTransfer.files[0];
      // In web/desktop webview, file path might be accessible
      const path = (file as any).path || file.name;
      onInputChange(path);
    }
  };

  return (
    <section className="bg-slate-900/90 border border-slate-800 rounded-2xl p-5 shadow-xl backdrop-blur-sm">
      <div className="flex items-center justify-between mb-3">
        <div className="flex items-center gap-2">
          <span className="w-2 h-2 rounded-full bg-blue-400 animate-pulse"></span>
          <h2 className="text-xs font-bold uppercase tracking-wider text-slate-300">
            Target Archive Selection
          </h2>
        </div>
        <span className="text-[11px] text-slate-500 font-mono">Supports: *.jar, *.war, *.zip</span>
      </div>

      {/* Visual Drop Area */}
      <div
        onDragOver={handleDragOver}
        onDragLeave={handleDragLeave}
        onDrop={handleDrop}
        onClick={onBrowseInput}
        className={`border-2 border-dashed rounded-xl p-5 text-center cursor-pointer transition-all ${
          isDragging
            ? "border-blue-500 bg-blue-500/10 scale-[0.99]"
            : "border-slate-700 hover:border-blue-500/80 bg-slate-950/40 group"
        }`}
      >
        <div className="flex flex-col items-center justify-center gap-2">
          <div className="w-11 h-11 rounded-full bg-blue-500/10 text-blue-400 flex items-center justify-center group-hover:scale-110 transition-transform">
            <FileCode className="w-5 h-5" />
          </div>
          <div>
            <p className="text-xs font-semibold text-slate-200">
              {inputJar ? (
                <span className="text-emerald-400 font-mono flex items-center justify-center gap-1">
                  <CheckCircle className="w-3.5 h-3.5" /> Selected: {inputJar.split(/[\\/]/).pop()}
                </span>
              ) : (
                <>Drag & drop compiled <span className="text-blue-400 font-mono">.jar</span> archive here</>
              )}
            </p>
            <p className="text-[11px] text-slate-500 mt-0.5">or click to browse local filesystem</p>
          </div>
        </div>
      </div>

      {/* Inputs for paths */}
      <div className="mt-4 space-y-3">
        <div>
          <label className="block text-xs font-semibold text-slate-400 mb-1">
            Input JAR Path <span className="text-red-400">*</span>
          </label>
          <div className="flex gap-2">
            <input
              type="text"
              value={inputJar}
              onChange={(e) => onInputChange(e.target.value)}
              placeholder="C:/path/to/application.jar"
              className="w-full bg-slate-950 border border-slate-800 rounded-lg px-3 py-1.5 text-xs font-mono text-slate-200 focus:outline-none focus:border-blue-500"
            />
            <button
              onClick={onBrowseInput}
              className="px-3 py-1.5 bg-slate-800 hover:bg-slate-700 text-xs font-medium text-slate-300 rounded-lg border border-slate-700 flex items-center gap-1.5 cursor-pointer whitespace-nowrap"
            >
              <FolderOpen className="w-3.5 h-3.5 text-blue-400" />
              Browse
            </button>
          </div>
        </div>

        <div>
          <label className="block text-xs font-semibold text-slate-400 mb-1">
            Output Destination (Defaults to <code className="text-blue-400 font-mono">&lt;input&gt;_obf.jar</code>)
          </label>
          <div className="flex gap-2">
            <input
              type="text"
              value={outputJar}
              onChange={(e) => onOutputChange(e.target.value)}
              placeholder="C:/path/to/application_obf.jar"
              className="w-full bg-slate-950 border border-slate-800 rounded-lg px-3 py-1.5 text-xs font-mono text-slate-200 focus:outline-none focus:border-blue-500"
            />
            <button
              onClick={onBrowseOutput}
              className="px-3 py-1.5 bg-slate-800 hover:bg-slate-700 text-xs font-medium text-slate-300 rounded-lg border border-slate-700 flex items-center gap-1.5 cursor-pointer whitespace-nowrap"
            >
              <Save className="w-3.5 h-3.5 text-blue-400" />
              Save As
            </button>
          </div>
        </div>
      </div>
    </section>
  );
};
