import React, { useState, useEffect, useRef } from "react";
import { invoke } from "@tauri-apps/api/core";
import { listen, UnlistenFn } from "@tauri-apps/api/event";
import { getCurrentWebview } from "@tauri-apps/api/webview";
import { ObfuscatorConfig, UserProfile } from "./types/config";
import { ObfEvent, SummaryEvent } from "./types/events";
import { BUILT_IN_PROFILES } from "./utils/defaults";
import { Shield, Search } from "lucide-react";
import "./App.css";

import { Header } from "./components/Header";
import { TitleBar } from "./components/TitleBar";
import { ArchiveSection } from "./components/ArchiveSection";
import { ScopingSection } from "./components/ScopingSection";
import { PassesSection } from "./components/PassesSection";
import { ExecutionSection } from "./components/ExecutionSection";
import { StepperSection, PassStepState } from "./components/StepperSection";
import { MetricsSection } from "./components/MetricsSection";
import { ConsoleSection } from "./components/ConsoleSection";
import { Footer } from "./components/Footer";
import { ProfileModal } from "./components/ProfileModal";
import { RetraceSection } from "./components/RetraceSection";

const PROFILES_STORAGE_KEY = "jobfuscate_profiles_v1";

const INITIAL_STEPS: PassStepState[] = [
  { id: "StripDebug", name: "Strip Debug Attributes", num: "01", state: "pending" },
  { id: "AccessReducer", name: "Access Reducer", num: "02", state: "pending" },
  { id: "ReflectionProxy", name: "Dynamic Reflection Proxies", num: "03", state: "pending" },
  { id: "NumberObfuscation", name: "Multi-Type Number Splitting", num: "04", state: "pending" },
  { id: "Rename", name: "Symbol Scrambler (Homoglyph)", num: "05", state: "pending" },
  { id: "StringEncrypt", name: "String Encryption (Multi-Layer)", num: "06", state: "pending" },
  { id: "ControlFlow", name: "Control Flow Flattening & Opaque Predicates", num: "07", state: "pending" },
];

const isPassEnabled = (passId: string, cfg: ObfuscatorConfig): boolean => {
  switch (passId) {
    case "StripDebug":
      return !!(
        cfg.strip.strip_line_numbers ||
        cfg.strip.strip_local_variables ||
        cfg.strip.strip_source_file ||
        cfg.strip.strip_source_debug ||
        cfg.strip.strip_kotlin_metadata
      );
    case "AccessReducer":
      return !!cfg.access_reducer?.enabled;
    case "ReflectionProxy":
      return !!cfg.reflection_proxy?.enabled;
    case "NumberObfuscation":
      return !!cfg.number_obfuscation?.enabled;
    case "Rename":
      return !!cfg.rename?.enabled;
    case "StringEncrypt":
      return !!cfg.string_encryption?.enabled;
    case "ControlFlow":
      return !!cfg.control_flow?.enabled;
    default:
      return true;
  }
};

const isTauriEnv = (): boolean => {
  return typeof window !== "undefined" && (!!(window as any).__TAURI_INTERNALS__ || !!(window as any).__TAURI__);
};

export const App: React.FC = () => {
  // Profiles
  const [profiles, setProfiles] = useState<UserProfile[]>(() => {
    try {
      const saved = localStorage.getItem(PROFILES_STORAGE_KEY);
      if (saved) {
        const parsed = JSON.parse(saved);
        if (Array.isArray(parsed) && parsed.length > 0) {
          return parsed;
        }
      }
    } catch {
      // ignore
    }
    return BUILT_IN_PROFILES;
  });

  const [activeProfileId, setActiveProfileId] = useState<string>("max-security");
  const [config, setConfig] = useState<ObfuscatorConfig>(() => {
    const p = BUILT_IN_PROFILES.find((p) => p.id === "max-security") || BUILT_IN_PROFILES[0];
    return p.config;
  });

  // Active view tab
  const [activeTab, setActiveTab] = useState<"obfuscate" | "retrace">("obfuscate");

  // Archive paths
  const [inputJar, setInputJar] = useState<string>("");
  const [outputJar, setOutputJar] = useState<string>("");
  const [mappingFile, setMappingFile] = useState<string>("");
  const [isDraggingFile, setIsDraggingFile] = useState<boolean>(false);

  // Telemetry & Execution state
  const [status, setStatus] = useState<"idle" | "running" | "success" | "error">("idle");
  const [activePhase, setActivePhase] = useState<string>("Ready");
  const [progressPercent, setProgressPercent] = useState<number>(0);
  const [steps, setSteps] = useState<PassStepState[]>(() =>
    INITIAL_STEPS.map((s) => ({
      ...s,
      state: isPassEnabled(s.id, config) ? "pending" : "skipped",
    }))
  );
  const [summary, setSummary] = useState<SummaryEvent | null>(null);
  const [logs, setLogs] = useState<string[]>([]);
  const [cliBinaryPath, setCliBinaryPath] = useState<string | null>(null);
  const [cliError, setCliError] = useState<string | null>(null);
  const [isProfileModalOpen, setIsProfileModalOpen] = useState<boolean>(false);

  // Sync steps state with config when idle
  useEffect(() => {
    if (status === "idle") {
      setSteps(
        INITIAL_STEPS.map((s) => ({
          ...s,
          state: isPassEnabled(s.id, config) ? "pending" : "skipped",
          duration_ms: undefined,
        }))
      );
    }
  }, [config, status]);

  // Auto-derivation tracker
  const wasOutputAutoDerived = useRef<boolean>(true);

  // Detect CLI binary on startup
  useEffect(() => {
    if (isTauriEnv()) {
      invoke<string>("detect_cli_binary")
        .then((path) => {
          setCliBinaryPath(path);
          setCliError(null);
        })
        .catch((err) => {
          setCliBinaryPath(null);
          setCliError(String(err));
        });
    } else {
      setCliBinaryPath("(Web Preview Mode)");
    }
  }, []);

  // Tauri event listeners
  useEffect(() => {
    if (!isTauriEnv()) return;

    let unlistenEvent: UnlistenFn | null = null;
    let unlistenStderr: UnlistenFn | null = null;
    let unlistenDone: UnlistenFn | null = null;

    const setupListeners = async () => {
      unlistenEvent = await listen<string>("obf:event", (ev) => {
        const raw = ev.payload;
        setLogs((prev) => [...prev, raw]);

        try {
          const parsed = JSON.parse(raw) as ObfEvent;
          handleObfEvent(parsed);
        } catch {
          // Non-JSON line
        }
      });

      unlistenStderr = await listen<string>("obf:stderr", (ev) => {
        setLogs((prev) => [...prev, `[stderr] ${ev.payload}`]);
      });

      unlistenDone = await listen<any>("obf:done", (ev) => {
        const exitCode =
          typeof ev.payload === "number"
            ? ev.payload
            : (ev.payload?.exit_code ?? (ev.payload?.code ?? null));
        const errorMsg = typeof ev.payload === "object" ? ev.payload?.error : null;

        if (exitCode === 0) {
          setStatus("success");
          setActivePhase("Obfuscation complete!");
          setProgressPercent(100);
          setSteps((prev) =>
            prev.map((s) => (s.state === "running" || s.state === "pending" ? { ...s, state: "done" } : s))
          );
        } else {
          // If complete event already marked it as success, do not override with error
          setStatus((prev) => (prev === "success" ? "success" : "error"));
          setActivePhase((prev) =>
            prev === "Obfuscation finished successfully!" || prev === "Obfuscation complete!"
              ? prev
              : (errorMsg || `Process exited with code ${exitCode}`)
          );
        }
      });
    };

    setupListeners();

    return () => {
      if (unlistenEvent) unlistenEvent();
      if (unlistenStderr) unlistenStderr();
      if (unlistenDone) unlistenDone();
    };
  }, []);

  const handleObfEvent = (event: ObfEvent) => {
    switch (event.type) {
      case "init":
        setActivePhase(`Loaded ${event.classes} classes from archive`);
        break;

      case "pass_start":
        setActivePhase(`Executing pass: ${event.pass}`);
        setSteps((prev) => {
          let found = false;
          return prev.map((s) => {
            if (s.id === event.pass) {
              found = true;
              return { ...s, state: "running" };
            }
            if (!found && s.state === "running") {
              return { ...s, state: "done" };
            }
            return s;
          });
        });
        break;

      case "pass_complete":
        setSteps((prev) =>
          prev.map((s) =>
            s.id === event.pass
              ? { ...s, state: "done", duration_ms: event.duration_ms }
              : s
          )
        );
        break;

      case "progress":
        setProgressPercent(Math.round(event.percent));
        setActivePhase(`${event.stage} (${event.current}/${event.total})`);
        break;

      case "summary":
        setSummary(event);
        break;

      case "complete":
        if (event.status === "success") {
          setStatus("success");
          setProgressPercent(100);
          setActivePhase("Obfuscation finished successfully!");
          if (event.stats) {
            setSummary({
              type: "summary",
              total_time_ms: event.duration_ms ?? 0,
              duration_ms: event.duration_ms ?? 0,
              ...event.stats,
            });
          }
          setSteps((prev) =>
            prev.map((s) => (s.state === "pending" || s.state === "running" ? { ...s, state: "done" } : s))
          );
        } else {
          setStatus("error");
          setActivePhase(`Failed: ${event.message || "Unknown error"}`);
        }
        break;

      case "error":
        setStatus("error");
        setActivePhase(`Error: ${event.message}`);
        break;
    }
  };

  // Profile Switching
  const handleSelectProfile = (id: string) => {
    const selected = profiles.find((p) => p.id === id);
    if (!selected) return;
    setActiveProfileId(id);
    setConfig({
      ...selected.config,
      target_packages: config.target_packages,
      exclude_packages: config.exclude_packages,
      exclude_classes: config.exclude_classes,
      keep_rules: config.keep_rules,
      ignore_dependencies: config.ignore_dependencies,
    });
  };

  // Create Custom Profile
  const handleSaveProfile = (name: string, description: string) => {
    const newProfile: UserProfile = {
      id: `profile-${Date.now()}`,
      name,
      description,
      isBuiltIn: false,
      config: {
        ...config,
        profile_name: name,
      },
    };
    const updated = [...profiles, newProfile];
    setProfiles(updated);
    setActiveProfileId(newProfile.id);
    try {
      localStorage.setItem(PROFILES_STORAGE_KEY, JSON.stringify(updated));
    } catch {
      // ignore
    }
  };

  // Input Jar Handling & Auto-Derivation
  const handleInputChange = (path: string) => {
    setInputJar(path);
    if (wasOutputAutoDerived.current || !outputJar) {
      if (path.toLowerCase().endsWith(".jar")) {
        const derived = path.replace(/\.jar$/i, "_obf.jar");
        setOutputJar(derived);
      } else if (path) {
        setOutputJar(`${path}_obf.jar`);
      }
      wasOutputAutoDerived.current = true;
    }
  };

  const handleOutputChange = (path: string) => {
    setOutputJar(path);
    wasOutputAutoDerived.current = false;
  };

  const handleBrowseInput = async () => {
    if (isTauriEnv()) {
      try {
        const res = await invoke<string | null>("pick_input_jar");
        if (res) {
          handleInputChange(res);
        }
      } catch (err) {
        console.error("Failed to open file picker:", err);
      }
    } else {
      const mock = prompt("Enter path to input JAR:", inputJar || "app.jar");
      if (mock) handleInputChange(mock);
    }
  };

  const handleBrowseOutput = async () => {
    if (isTauriEnv()) {
      try {
        const res = await invoke<string | null>("pick_output_jar", {
          defaultPath: outputJar || undefined,
        });
        if (res) {
          handleOutputChange(res);
        }
      } catch (err) {
        console.error("Failed to open save picker:", err);
      }
    } else {
      const mock = prompt("Enter path to output JAR:", outputJar || "app_obf.jar");
      if (mock) handleOutputChange(mock);
    }
  };

  const handleBrowseMapping = async () => {
    if (isTauriEnv()) {
      try {
        const res = await invoke<string | null>("pick_output_jar", {
          defaultPath: mappingFile || (outputJar ? outputJar.replace(/\.jar$/i, "_mapping.txt") : "mapping.txt"),
        });
        if (res) {
          setMappingFile(res);
        }
      } catch (err) {
        console.error("Failed to open save picker:", err);
      }
    } else {
      const mock = prompt("Enter path to export mapping file:", mappingFile || "mapping.txt");
      if (mock) setMappingFile(mock);
    }
  };

  const handleImportJson = async () => {
    if (isTauriEnv()) {
      try {
        const content = await invoke<string | null>("pick_config_file");
        if (content) {
          const parsed = JSON.parse(content);
          setConfig(parsed);
        }
      } catch (err) {
        alert("Failed to import configuration: " + String(err));
      }
    } else {
      const input = document.createElement("input");
      input.type = "file";
      input.accept = ".json";
      input.onchange = (e) => {
        const file = (e.target as HTMLInputElement).files?.[0];
        if (file) {
          const reader = new FileReader();
          reader.onload = () => {
            try {
              const parsed = JSON.parse(reader.result as string);
              setConfig(parsed);
            } catch (err) {
              alert("Invalid JSON: " + String(err));
            }
          };
          reader.readAsText(file);
        }
      };
      input.click();
    }
  };

  // Tauri native drag-drop listener for JAR archives
  useEffect(() => {
    if (!isTauriEnv()) return;

    let unlisten: UnlistenFn | null = null;
    getCurrentWebview()
      .onDragDropEvent((event) => {
        if (event.payload.type === "enter" || event.payload.type === "over") {
          setIsDraggingFile(true);
        } else if (event.payload.type === "leave") {
          setIsDraggingFile(false);
        } else if (event.payload.type === "drop") {
          setIsDraggingFile(false);
          if (event.payload.paths && event.payload.paths.length > 0) {
            const droppedPath = event.payload.paths[0];
            const lower = droppedPath.toLowerCase();
            if (lower.endsWith(".jar") || lower.endsWith(".war") || lower.endsWith(".zip")) {
              handleInputChange(droppedPath);
            }
          }
        }
      })
      .then((fn) => {
        unlisten = fn;
      })
      .catch((err) => {
        console.error("Failed to register drag-drop listener:", err);
      });

    return () => {
      if (unlisten) unlisten();
    };
  }, []);

  const handleExportJson = async () => {
    const jsonStr = JSON.stringify(config, null, 2);
    if (isTauriEnv()) {
      try {
        await invoke<string | null>("save_config_file", {
          content: jsonStr,
          defaultName: `${config.profile_name || "jobfuscate_config"}.json`,
        });
      } catch (err) {
        alert("Failed to export configuration: " + String(err));
      }
    } else {
      const blob = new Blob([jsonStr], { type: "application/json" });
      const url = URL.createObjectURL(blob);
      const a = document.createElement("a");
      a.href = url;
      a.download = `${config.profile_name || "jobfuscate_config"}.json`;
      a.click();
      URL.revokeObjectURL(url);
    }
  };

  // Execution
  const handleStartObfuscation = async () => {
    if (!inputJar) {
      alert("Please specify an input JAR archive before running.");
      return;
    }
    if (!outputJar) {
      alert("Please specify an output JAR archive.");
      return;
    }

    setStatus("running");
    setProgressPercent(5);
    setActivePhase("Initializing pipeline dispatch...");
    setSummary(null);
    setLogs([]);
    setSteps(
      INITIAL_STEPS.map((s) => ({
        ...s,
        state: isPassEnabled(s.id, config) ? "pending" : "skipped",
        duration_ms: undefined,
      }))
    );

    if (isTauriEnv()) {
      try {
        const runConfig = {
          ...config,
          input_jar: inputJar,
          output_jar: outputJar,
          mapping_file: mappingFile || config.mapping_file || undefined,
        };
        await invoke("run_obfuscation", {
          inputJar,
          outputJar,
          configJson: JSON.stringify(runConfig),
        });
      } catch (err) {
        setStatus("error");
        setActivePhase(`Error starting process: ${String(err)}`);
        setLogs((prev) => [...prev, `[ERROR] ${String(err)}`]);
      }
    } else {
      // Web preview simulation
      setLogs((prev) => [
        ...prev,
        JSON.stringify({ type: "init", input: inputJar, output: outputJar, classes: 24 }),
      ]);
      let currentStepIdx = 0;

      const simInterval = setInterval(() => {
        if (currentStepIdx < INITIAL_STEPS.length) {
          const step = INITIAL_STEPS[currentStepIdx];
          const duration = Math.floor(Math.random() * 40) + 15;
          setLogs((prev) => [
            ...prev,
            JSON.stringify({ type: "pass_start", pass: step.id }),
            JSON.stringify({
              type: "progress",
              stage: step.name,
              current: currentStepIdx + 1,
              total: INITIAL_STEPS.length,
              percent: Math.round(((currentStepIdx + 1) / INITIAL_STEPS.length) * 100),
              item: `Class_${currentStepIdx + 1}.class`,
            }),
            JSON.stringify({ type: "pass_complete", pass: step.id, duration_ms: duration }),
          ]);
          setSteps((prev) =>
            prev.map((s, idx) => {
              if (idx === currentStepIdx) return { ...s, state: "done", duration_ms: duration };
              if (idx === currentStepIdx + 1) return { ...s, state: "running" };
              return s;
            })
          );
          setProgressPercent(Math.round(((currentStepIdx + 1) / INITIAL_STEPS.length) * 100));
          currentStepIdx++;
        } else {
          clearInterval(simInterval);
          const mockSummary: SummaryEvent = {
            type: "summary",
            classes_processed: 24,
            classes_renamed: 24,
            methods_renamed: 82,
            fields_renamed: 34,
            strings_encrypted: 244,
            methods_flattened: 52,
            methods_exception_dispatched: 31,
            reflection_proxies_created: 73,
            numbers_obfuscated: 355,
            members_privatized: 14,
            debug_attributes_stripped: 184,
            total_time_ms: 120,
          };
          setSummary(mockSummary);
          setLogs((prev) => [
            ...prev,
            JSON.stringify(mockSummary),
            JSON.stringify({ type: "complete", status: "success", output_jar: outputJar }),
          ]);
          setStatus("success");
          setActivePhase("Obfuscation finished successfully!");
        }
      }, 350);
    }
  };

  const handleCancelObfuscation = async () => {
    if (isTauriEnv()) {
      try {
        await invoke("cancel_obfuscation");
      } catch (err) {
        console.error("Cancel failed:", err);
      }
    }
    setStatus("idle");
    setActivePhase("Cancelled by user");
  };

  return (
    <div className="min-h-screen bg-slate-950 text-slate-100 flex flex-col justify-between selection:bg-blue-600 selection:text-white">
      {/* Frameless Custom Window Controls Bar */}
      <TitleBar />

      <div className="flex-1 max-w-7xl w-full mx-auto px-4 sm:px-6 lg:px-8 pt-6 pb-8 space-y-8">
        {/* Top Header */}
        <Header
          activeProfileId={activeProfileId}
          profiles={profiles}
          onSelectProfile={handleSelectProfile}
          onCreateProfile={() => setIsProfileModalOpen(true)}
          onImportJson={handleImportJson}
          onExportJson={handleExportJson}
          cliBinaryPath={cliBinaryPath}
          cliError={cliError}
        />

        {/* Navigation Tabs */}
        <div className="flex items-center gap-2 border-b border-slate-800 pb-3">
          <button
            onClick={() => setActiveTab("obfuscate")}
            className={`px-4 py-2 rounded-xl text-xs font-semibold flex items-center gap-2 transition-all cursor-pointer ${
              activeTab === "obfuscate"
                ? "bg-blue-600/20 text-blue-400 border border-blue-500/30 shadow-lg shadow-blue-500/10"
                : "text-slate-400 hover:text-slate-200 hover:bg-slate-900 border border-transparent"
            }`}
          >
            <Shield className="w-3.5 h-3.5" />
            <span>Obfuscator Studio</span>
          </button>
          <button
            onClick={() => setActiveTab("retrace")}
            className={`px-4 py-2 rounded-xl text-xs font-semibold flex items-center gap-2 transition-all cursor-pointer ${
              activeTab === "retrace"
                ? "bg-emerald-600/20 text-emerald-400 border border-emerald-500/30 shadow-lg shadow-emerald-500/10"
                : "text-slate-400 hover:text-slate-200 hover:bg-slate-900 border border-transparent"
            }`}
          >
            <Search className="w-3.5 h-3.5" />
            <span>Stack Trace Deobfuscator (Retrace)</span>
          </button>
        </div>

        {activeTab === "obfuscate" ? (
          /* Main Grid: Settings & Execution */
          <main className="grid grid-cols-1 lg:grid-cols-12 gap-6">
            {/* Left Column: Archives & Config Passes (8 cols) */}
            <div className="lg:col-span-7 xl:col-span-8 space-y-6">
              <ArchiveSection
                inputJar={inputJar}
                outputJar={outputJar}
                mappingFile={mappingFile}
                isDragging={isDraggingFile}
                onInputChange={handleInputChange}
                onOutputChange={handleOutputChange}
                onMappingChange={setMappingFile}
                onBrowseInput={handleBrowseInput}
                onBrowseOutput={handleBrowseOutput}
                onBrowseMapping={handleBrowseMapping}
              />

              <ScopingSection
                targetPackages={config.target_packages}
                excludePackages={config.exclude_packages}
                excludeClasses={config.exclude_classes}
                keepRules={config.keep_rules}
                ignoreDependencies={config.ignore_dependencies}
                onChangeTargetPackages={(pkgs) => setConfig({ ...config, target_packages: pkgs })}
                onChangeExcludePackages={(pkgs) => setConfig({ ...config, exclude_packages: pkgs })}
                onChangeExcludeClasses={(classes) => setConfig({ ...config, exclude_classes: classes })}
                onChangeKeepRules={(rules) => setConfig({ ...config, keep_rules: rules })}
                onChangeIgnoreDependencies={(val) => setConfig({ ...config, ignore_dependencies: val })}
              />

              <PassesSection config={config} onChangeConfig={setConfig} />
            </div>

            {/* Right Column: Execution, Stepper, Metrics & Console (4 cols) */}
            <div className="lg:col-span-5 xl:col-span-4 space-y-6">
              <ExecutionSection
                isRunning={status === "running"}
                activePhase={activePhase}
                progressPercent={progressPercent}
                status={status}
                onStart={handleStartObfuscation}
                onCancel={handleCancelObfuscation}
              />

              <StepperSection steps={steps} />

              <MetricsSection summary={summary} />

              <ConsoleSection logs={logs} onClear={() => setLogs([])} />
            </div>
          </main>
        ) : (
          <RetraceSection />
        )}

        {/* Global Footer */}
        <Footer />
      </div>

      {/* Save Profile Modal */}
      <ProfileModal
        isOpen={isProfileModalOpen}
        onClose={() => setIsProfileModalOpen(false)}
        onSave={handleSaveProfile}
      />
    </div>
  );
};

export default App;
