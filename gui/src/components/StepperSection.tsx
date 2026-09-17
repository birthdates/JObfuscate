import React from "react";
import { CheckCircle2, Clock } from "lucide-react";

export interface PassStepState {
  id: string;
  name: string;
  num: string;
  state: "pending" | "running" | "done" | "skipped";
  duration_ms?: number;
}

interface StepperSectionProps {
  steps: PassStepState[];
}

export const StepperSection: React.FC<StepperSectionProps> = ({ steps }) => {
  return (
    <div className="bg-slate-900/90 border border-slate-800 rounded-2xl p-5 shadow-xl space-y-3">
      <h3 className="text-xs font-bold uppercase tracking-wider text-slate-400 mb-2 flex items-center justify-between">
        <span>Pipeline Execution Stepper</span>
        <span className="text-[10px] text-slate-500 font-mono">Live Telemetry</span>
      </h3>

      <div className="space-y-1.5 text-xs">
        {steps.map((step) => {
          const isRunning = step.state === "running";
          const isDone = step.state === "done";
          const isSkipped = step.state === "skipped";

          return (
            <div
              key={step.id}
              className={`flex items-center justify-between p-2 rounded-lg transition-all border ${
                isRunning
                  ? "bg-blue-950/40 border-blue-500/50 shadow-[0_0_12px_rgba(59,130,246,0.3)]"
                  : isDone
                  ? "bg-slate-950/60 border-slate-800/80 text-slate-200"
                  : "bg-slate-950/30 border-slate-800/40 text-slate-500"
              }`}
            >
              <div className="flex items-center gap-2">
                {isRunning ? (
                  <span className="w-2.5 h-2.5 rounded-full bg-blue-400 animate-ping" />
                ) : isDone ? (
                  <CheckCircle2 className="w-3.5 h-3.5 text-emerald-400 flex-shrink-0" />
                ) : isSkipped ? (
                  <span className="w-2 h-2 rounded-full bg-slate-700" />
                ) : (
                  <span className="w-2 h-2 rounded-full bg-slate-600" />
                )}
                <span className="font-mono text-[11px] font-medium">
                  {step.num}. {step.name}
                </span>
              </div>

              <div>
                {isDone && step.duration_ms !== undefined ? (
                  <span className="duration text-[11px] text-emerald-400 font-mono font-semibold flex items-center gap-1">
                    <Clock className="w-3 h-3 text-emerald-500" />
                    {step.duration_ms} ms
                  </span>
                ) : isRunning ? (
                  <span className="text-[10px] text-blue-400 font-mono animate-pulse">Running...</span>
                ) : isSkipped ? (
                  <span className="text-[10px] text-slate-600 font-mono">Skipped</span>
                ) : (
                  <span className="text-[11px] text-slate-600 font-mono">-</span>
                )}
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
};
