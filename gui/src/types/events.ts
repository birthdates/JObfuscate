export interface PassStartEvent {
  type: "pass_start";
  pass: string;
  description?: string;
}

export interface ProgressEvent {
  type: "progress";
  stage?: string;
  phase?: string;
  percent: number;
  current?: number;
  processed?: number;
  total: number;
  item?: string;
}

export interface PassCompleteEvent {
  type: "pass_complete";
  pass: string;
  duration_ms: number;
}

export interface LogEvent {
  type: "log";
  level: string;
  message: string;
}

export interface SummaryStats {
  classes_processed: number;
  classes_renamed: number;
  methods_renamed: number;
  fields_renamed: number;
  strings_encrypted: number;
  methods_flattened: number;
  reflection_proxies_created: number;
  numbers_obfuscated: number;
  members_privatized: number;
  debug_attributes_stripped: number;
  original_size_bytes?: number;
  output_size_bytes?: number;
}

export interface SummaryEvent extends SummaryStats {
  type: "summary";
  total_time_ms?: number;
  duration_ms?: number;
}

export interface CompleteEvent {
  type: "complete";
  status: "success" | "failed";
  duration_ms?: number;
  message?: string;
  output_jar?: string;
  stats?: SummaryStats;
}

export interface ErrorEvent {
  type: "error";
  message: string;
}

export interface InitEvent {
  type: "init";
  input?: string;
  output?: string;
  classes?: number;
}

export type NDJsonEvent =
  | PassStartEvent
  | ProgressEvent
  | PassCompleteEvent
  | LogEvent
  | SummaryEvent
  | CompleteEvent
  | ErrorEvent
  | InitEvent;

export type ObfEvent = NDJsonEvent;
