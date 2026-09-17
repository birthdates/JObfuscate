import React, { useState, useRef, useEffect } from "react";
import { ChevronDown, Check } from "lucide-react";

export interface SelectOption {
  value: string;
  label: string;
  sublabel?: string;
  badge?: string;
}

interface CustomSelectProps {
  value: string;
  onChange: (value: string) => void;
  options: SelectOption[];
  placeholder?: string;
  className?: string;
  menuWidth?: string;
}

export const CustomSelect: React.FC<CustomSelectProps> = ({
  value,
  onChange,
  options,
  placeholder = "Select...",
  className = "",
  menuWidth = "w-56",
}) => {
  const [isOpen, setIsOpen] = useState(false);
  const containerRef = useRef<HTMLDivElement>(null);

  const selectedOption = options.find((opt) => opt.value === value);

  useEffect(() => {
    const handleClickOutside = (event: MouseEvent) => {
      if (containerRef.current && !containerRef.current.contains(event.target as Node)) {
        setIsOpen(false);
      }
    };

    const handleKeyDown = (event: KeyboardEvent) => {
      if (event.key === "Escape") {
        setIsOpen(false);
      }
    };

    if (isOpen) {
      document.addEventListener("mousedown", handleClickOutside);
      document.addEventListener("keydown", handleKeyDown);
    }

    return () => {
      document.removeEventListener("mousedown", handleClickOutside);
      document.removeEventListener("keydown", handleKeyDown);
    };
  }, [isOpen]);

  return (
    <div className={`relative inline-block text-left ${className}`} ref={containerRef}>
      {/* Trigger Button */}
      <button
        type="button"
        onClick={() => setIsOpen(!isOpen)}
        className="flex items-center justify-between gap-2 bg-slate-950 hover:bg-slate-900 border border-slate-700/80 hover:border-slate-600 focus:border-blue-500 rounded-lg px-2.5 py-1 text-xs text-slate-200 font-medium transition-all shadow-sm cursor-pointer outline-none w-full"
      >
        <span className="truncate flex items-center gap-1.5">
          <span>{selectedOption ? selectedOption.label : placeholder}</span>
          {selectedOption?.sublabel && (
            <span className="text-[10px] text-slate-400 font-normal">
              {selectedOption.sublabel}
            </span>
          )}
          {selectedOption?.badge && (
            <span className="text-[9px] px-1.5 py-0.2 rounded bg-blue-500/20 text-blue-400 border border-blue-500/30">
              {selectedOption.badge}
            </span>
          )}
        </span>
        <ChevronDown
          className={`w-3.5 h-3.5 text-slate-400 transition-transform duration-200 flex-shrink-0 ${
            isOpen ? "rotate-180 text-blue-400" : ""
          }`}
        />
      </button>

      {/* Floating Dropdown Menu */}
      {isOpen && (
        <div
          className={`absolute left-0 mt-1.5 ${menuWidth} max-h-64 overflow-y-auto bg-slate-900/98 border border-slate-700/90 rounded-xl shadow-2xl shadow-black/80 backdrop-blur-md p-1 z-50 animate-in fade-in zoom-in-95 duration-100 space-y-0.5`}
        >
          {options.map((option) => {
            const isSelected = option.value === value;
            return (
              <button
                key={option.value}
                type="button"
                onClick={() => {
                  onChange(option.value);
                  setIsOpen(false);
                }}
                className={`w-full flex items-center justify-between px-2.5 py-1.5 rounded-lg text-xs transition-colors cursor-pointer text-left ${
                  isSelected
                    ? "bg-blue-600/20 text-blue-300 font-semibold border border-blue-500/30"
                    : "text-slate-300 hover:bg-slate-800 hover:text-white"
                }`}
              >
                <div className="flex items-center gap-1.5 truncate">
                  <span className="truncate">{option.label}</span>
                  {option.sublabel && (
                    <span className="text-[10px] text-slate-400 font-normal truncate">
                      {option.sublabel}
                    </span>
                  )}
                  {option.badge && (
                    <span className="text-[9px] px-1.5 py-0.2 rounded bg-slate-800 text-slate-400 border border-slate-700">
                      {option.badge}
                    </span>
                  )}
                </div>
                {isSelected && (
                  <Check className="w-3.5 h-3.5 text-blue-400 flex-shrink-0 ml-1.5" />
                )}
              </button>
            );
          })}
        </div>
      )}
    </div>
  );
};
