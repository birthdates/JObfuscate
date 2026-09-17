import React, { useState } from "react";
import { SummaryEvent } from "../types/events";
import { ChevronDown, ChevronUp } from "lucide-react";

interface MetricsSectionProps {
  summary: SummaryEvent | null;
}

export const MetricsSection: React.FC<MetricsSectionProps> = ({ summary }) => {
  const [expanded, setExpanded] = useState(false);

  return (
    <div className="bg-slate-900/90 border border-slate-800 rounded-2xl p-5 shadow-xl">
      <div className="flex items-center justify-between mb-3">
        <h3 className="text-xs font-bold uppercase tracking-wider text-slate-400">
          Runtime Execution Metrics
        </h3>
        {summary && (
          <button
            onClick={() => setExpanded(!expanded)}
            className="text-[11px] text-blue-400 hover:text-blue-300 flex items-center gap-0.5 cursor-pointer"
          >
            {expanded ? "Show Less" : "Show All Details"}
            {expanded ? <ChevronUp className="w-3 h-3" /> : <ChevronDown className="w-3 h-3" />}
          </button>
        )}
      </div>

      <div className="grid grid-cols-2 gap-3 text-center">
        <div className="bg-slate-950/80 p-3 rounded-xl border border-slate-800">
          <div className="text-[10px] text-slate-500 uppercase font-semibold">Classes Scrambled</div>
          <div className="text-xl font-bold text-white mt-0.5">
            {summary ? summary.classes_renamed : 0}
          </div>
        </div>
        <div className="bg-slate-950/80 p-3 rounded-xl border border-slate-800">
          <div className="text-[10px] text-slate-500 uppercase font-semibold">Strings Encrypted</div>
          <div className="text-xl font-bold text-amber-400 mt-0.5">
            {summary ? summary.strings_encrypted : 0}
          </div>
        </div>
        <div className="bg-slate-950/80 p-3 rounded-xl border border-slate-800">
          <div className="text-[10px] text-slate-500 uppercase font-semibold">Reflection Proxies</div>
          <div className="text-xl font-bold text-purple-400 mt-0.5">
            {summary ? summary.reflection_proxies_created : 0}
          </div>
        </div>
        <div className="bg-slate-950/80 p-3 rounded-xl border border-slate-800">
          <div className="text-[10px] text-slate-500 uppercase font-semibold">Execution Time</div>
          <div className="text-xl font-bold text-emerald-400 mt-0.5">
            {summary ? `${summary.total_time_ms ?? summary.duration_ms ?? 0} ms` : "0 ms"}
          </div>
        </div>
      </div>

      {/* Expanded Metrics Details */}
      {expanded && summary && (
        <div className="mt-3 pt-3 border-t border-slate-800 grid grid-cols-2 sm:grid-cols-3 gap-2 text-center text-xs">
          <div className="bg-slate-950/50 p-2 rounded-lg border border-slate-800/60">
            <span className="text-[10px] text-slate-500 block">Methods Renamed</span>
            <span className="font-mono text-slate-200 font-semibold">{summary.methods_renamed}</span>
          </div>
          <div className="bg-slate-950/50 p-2 rounded-lg border border-slate-800/60">
            <span className="text-[10px] text-slate-500 block">Fields Renamed</span>
            <span className="font-mono text-slate-200 font-semibold">{summary.fields_renamed}</span>
          </div>
          <div className="bg-slate-950/50 p-2 rounded-lg border border-slate-800/60">
            <span className="text-[10px] text-slate-500 block">Numbers Obfuscated</span>
            <span className="font-mono text-cyan-400 font-semibold">{summary.numbers_obfuscated}</span>
          </div>
          <div className="bg-slate-950/50 p-2 rounded-lg border border-slate-800/60">
            <span className="text-[10px] text-slate-500 block">Members Privatized</span>
            <span className="font-mono text-emerald-400 font-semibold">{summary.members_privatized}</span>
          </div>
          <div className="bg-slate-950/50 p-2 rounded-lg border border-slate-800/60">
            <span className="text-[10px] text-slate-500 block">Methods Flattened</span>
            <span className="font-mono text-indigo-400 font-semibold">{summary.methods_flattened}</span>
          </div>
          <div className="bg-slate-950/50 p-2 rounded-lg border border-slate-800/60">
            <span className="text-[10px] text-slate-500 block">Debug Info Stripped</span>
            <span className="font-mono text-blue-400 font-semibold">{summary.debug_attributes_stripped}</span>
          </div>
        </div>
      )}
    </div>
  );
};
