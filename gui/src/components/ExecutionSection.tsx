import React from "react";
import { Play, Square, Loader2 } from "lucide-react";

interface ExecutionSectionProps {
  isRunning: boolean;
  activePhase: string;
  progressPercent: number;
  status: "idle" | "running" | "success" | "error";
  onStart: () => void;
  onCancel: () => void;
}

export const ExecutionSection: React.FC<ExecutionSectionProps> = ({
  isRunning,
  activePhase,
  progressPercent,
  status,
  onStart,
  onCancel,
}) => {
  return (
    <div className="bg-slate-900/90 border border-slate-800 rounded-2xl p-5 shadow-2xl space-y-4">
      <div className="flex items-center justify-between">
        <h2 className="text-xs font-bold uppercase tracking-wider text-slate-300">
          Action Control Center
        </h2>
        <span
          className={`text-xs px-2.5 py-0.5 rounded-full font-semibold border ${
            status === "running"
              ? "bg-blue-500/20 text-blue-400 border-blue-500/30 animate-pulse"
              : status === "success"
              ? "bg-emerald-500/20 text-emerald-400 border-emerald-500/30"
              : status === "error"
              ? "bg-red-500/20 text-red-400 border-red-500/30"
              : "bg-slate-800 text-slate-400 border-slate-700"
          }`}
        >
          {status === "running"
            ? "Obfuscating..."
            : status === "success"
            ? "Complete"
            : status === "error"
            ? "Failed"
            : "Idle"}
        </span>
      </div>

      {/* Signature Red Start Button */}
      {!isRunning ? (
        <button
          onClick={onStart}
          className="w-full py-4 px-4 rounded-xl font-bold text-sm bg-gradient-to-r from-red-600 via-rose-600 to-red-500 hover:from-red-500 hover:to-rose-500 text-white shadow-lg shadow-red-600/30 transition-all flex items-center justify-center gap-2.5 group cursor-pointer border border-red-500/40 active:scale-[0.99]"
        >
          <Play className="w-4 h-4 fill-white text-white group-hover:scale-110 transition-transform" />
          <span className="tracking-wide uppercase text-xs font-extrabold">
            START OBFUSCATION
          </span>
        </button>
      ) : (
        <button
          onClick={onCancel}
          className="w-full py-4 px-4 rounded-xl font-bold text-sm bg-slate-950 hover:bg-red-950/80 text-red-400 border border-red-500/60 shadow-lg shadow-red-950/40 transition-all flex items-center justify-center gap-2.5 cursor-pointer active:scale-[0.99]"
        >
          <Square className="w-4 h-4 fill-red-400 text-red-400" />
          <span className="tracking-wide uppercase text-xs font-extrabold">
            CANCEL EXECUTION
          </span>
        </button>
      )}

      {/* Animated Progress Bar */}
      <div className="space-y-1.5">
        <div className="flex justify-between text-xs font-mono">
          <span className="text-slate-400 text-[11px] truncate max-w-[260px]">
            {isRunning ? (
              <span className="flex items-center gap-1.5 text-blue-400">
                <Loader2 className="w-3 h-3 animate-spin" />
                {activePhase}
              </span>
            ) : (
              activePhase || "Ready to execute"
            )}
          </span>
          <span className="text-blue-400 font-semibold">{progressPercent}%</span>
        </div>
        <div className="w-full h-2.5 bg-slate-950 rounded-full overflow-hidden p-0.5 border border-slate-800">
          <div
            className="h-full bg-gradient-to-r from-blue-500 via-indigo-500 to-cyan-400 rounded-full transition-all duration-300"
            style={{ width: `${progressPercent}%` }}
          />
        </div>
      </div>
    </div>
  );
};
