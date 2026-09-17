import React, { useState, useRef, useEffect } from "react";
import { Copy, Trash2, Check, Terminal } from "lucide-react";

interface ConsoleSectionProps {
  logs: string[];
  onClear: () => void;
}

export const ConsoleSection: React.FC<ConsoleSectionProps> = ({ logs, onClear }) => {
  const [copied, setCopied] = useState(false);
  const [filter, setFilter] = useState("");
  const scrollRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (scrollRef.current) {
      scrollRef.current.scrollTop = scrollRef.current.scrollHeight;
    }
  }, [logs]);

  const handleCopy = () => {
    navigator.clipboard.writeText(logs.join("\n"));
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  };

  const filteredLogs = filter
    ? logs.filter((l) => l.toLowerCase().includes(filter.toLowerCase()))
    : logs;

  return (
    <div className="bg-slate-950 border border-slate-800 rounded-2xl p-4 shadow-xl font-mono text-[11px]">
      <div className="flex items-center justify-between border-b border-slate-800 pb-2 mb-2 text-slate-400 gap-2 flex-wrap">
        <span className="flex items-center gap-1.5 text-xs font-semibold text-slate-300">
          <Terminal className="w-3.5 h-3.5 text-blue-400" />
          <span>IPC NDJSON Stream (<code className="text-blue-400">--json</code>)</span>
        </span>

        <div className="flex items-center gap-2">
          <input
            type="text"
            placeholder="Filter logs..."
            value={filter}
            onChange={(e) => setFilter(e.target.value)}
            className="bg-slate-900 border border-slate-800 rounded px-2 py-0.5 text-[10px] text-slate-300 focus:outline-none focus:border-blue-500 w-28"
          />
          <button
            onClick={handleCopy}
            title="Copy all logs"
            className="hover:text-white transition-colors text-[10px] flex items-center gap-1 cursor-pointer"
          >
            {copied ? <Check className="w-3 h-3 text-emerald-400" /> : <Copy className="w-3 h-3" />}
            <span>{copied ? "Copied" : "Copy"}</span>
          </button>
          <button
            onClick={onClear}
            title="Clear terminal"
            className="hover:text-red-400 transition-colors text-[10px] flex items-center gap-1 cursor-pointer"
          >
            <Trash2 className="w-3 h-3" />
            <span>Clear</span>
          </button>
        </div>
      </div>

      <div ref={scrollRef} className="h-36 overflow-y-auto space-y-1 text-slate-400 pr-1 select-text">
        {filteredLogs.length === 0 ? (
          <div className="text-slate-600">// Waiting for process dispatch...</div>
        ) : (
          filteredLogs.map((log, idx) => {
            let color = "text-slate-300";
            if (log.includes('"type":"pass_start"')) color = "text-blue-300";
            else if (log.includes('"type":"pass_complete"')) color = "text-emerald-300";
            else if (log.includes('"type":"summary"')) color = "text-amber-300 font-semibold";
            else if (log.includes('"error"') || log.toLowerCase().includes("error")) color = "text-red-400";

            return (
              <div key={idx} className={`${color} break-all`}>
                <span className="text-blue-500 mr-1">&gt;</span>
                {log}
              </div>
            );
          })
        )}
      </div>
    </div>
  );
};
