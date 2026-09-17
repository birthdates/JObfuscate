import React, { useState } from "react";
import { invoke } from "@tauri-apps/api/core";
import { Minus, Square, Copy, X } from "lucide-react";
import logoSvg from "../assets/logo.svg";

export const TitleBar: React.FC = () => {
  const [isMaximized, setIsMaximized] = useState(false);

  const handleMinimize = () => {
    invoke("window_minimize").catch((err) => console.log("Minimize:", err));
  };

  const handleToggleMaximize = async () => {
    try {
      const isMax = await invoke<boolean>("window_toggle_maximize");
      setIsMaximized(isMax);
    } catch (err) {
      console.log("Toggle maximize:", err);
      setIsMaximized(!isMaximized);
    }
  };

  const handleClose = () => {
    invoke("window_close").catch((err) => console.log("Close:", err));
  };

  const handleDragStart = (e: React.MouseEvent) => {
    if (e.button === 0 && !(e.target as HTMLElement).closest("button")) {
      invoke("window_start_dragging").catch(() => {});
    }
  };

  return (
    <div
      data-tauri-drag-region
      onMouseDown={handleDragStart}
      onDoubleClick={(e) => {
        if (!(e.target as HTMLElement).closest("button")) {
          handleToggleMaximize();
        }
      }}
      className="sticky top-0 left-0 right-0 z-50 w-full h-9 bg-slate-950/98 border-b border-slate-800/80 flex items-center justify-between select-none backdrop-blur-md"
    >
      {/* Left: Window Brand & Title */}
      <div
        data-tauri-drag-region
        className="flex items-center gap-2.5 px-3.5 h-full cursor-default select-none"
      >
        <img
          src={logoSvg}
          alt="JObfuscate"
          className="w-4 h-4 pointer-events-none drop-shadow-[0_0_8px_rgba(56,189,248,0.5)]"
          style={{ width: "16px", height: "16px" }}
        />
        <span className="text-xs font-semibold text-slate-200 tracking-wide">JObfuscate</span>
        <span className="text-[10px] text-slate-500 font-mono hidden sm:inline-block">
          Java 8 – 25 Bytecode Obfuscator
        </span>
      </div>

      {/* Middle: Draggable window region */}
      <div
        data-tauri-drag-region
        className="flex-1 h-full cursor-default"
      />

      {/* Right: Window Controls (- [] X) */}
      <div className="flex items-center h-full">
        <button
          type="button"
          onClick={handleMinimize}
          title="Minimize"
          className="w-11 h-full flex items-center justify-center text-slate-400 hover:text-slate-100 hover:bg-slate-850 transition-colors cursor-pointer"
        >
          <Minus className="w-3.5 h-3.5" />
        </button>

        <button
          type="button"
          onClick={handleToggleMaximize}
          title={isMaximized ? "Restore" : "Maximize"}
          className="w-11 h-full flex items-center justify-center text-slate-400 hover:text-slate-100 hover:bg-slate-850 transition-colors cursor-pointer"
        >
          {isMaximized ? (
            <Copy className="w-3 h-3 rotate-90" />
          ) : (
            <Square className="w-3 h-3" />
          )}
        </button>

        <button
          type="button"
          onClick={handleClose}
          title="Close"
          className="w-12 h-full flex items-center justify-center text-slate-400 hover:text-white hover:bg-red-600 transition-colors cursor-pointer group"
        >
          <X className="w-4 h-4 group-hover:scale-110 transition-transform" />
        </button>
      </div>
    </div>
  );
};
