export type RenameMode = "long" | "confusing" | "short" | "hex";
export type StringAlgorithm = "MultiLayerCrypto" | "XorRollingKey";

export interface StripOptions {
  strip_line_numbers: boolean;
  strip_local_variables: boolean;
  strip_source_file: boolean;
  strip_source_debug: boolean;
  strip_signatures: boolean;
  strip_parameters: boolean;
  strip_kotlin_metadata: boolean;
}

export interface RenameOptions {
  enabled: boolean;
  rename_classes: boolean;
  rename_packages: boolean;
  remove_packages: boolean;
  rename_methods: boolean;
  rename_fields: boolean;
  obfuscate_enums: boolean;
  keep_main_class: boolean;
  scramble_methods: boolean;
  remap_resources: boolean;
  resource_patterns: string[];
  mode: RenameMode;
  exclusions: string[];
}

export interface StringEncryptionOptions {
  enabled: boolean;
  algorithm: StringAlgorithm;
  key_seed: number;
  encrypt_indy_concat: boolean;
  randomize_decryptor_names: boolean;
  strings_per_decryptor_min: number;
  strings_per_decryptor_max: number;
  encrypt_annotation_strings: boolean;
  split_keys: boolean;
  exclusions: string[];
}

export interface AccessReducerOptions {
  enabled: boolean;
}

export interface ControlFlowOptions {
  enabled: boolean;
  insert_opaque_predicates: boolean;
  flatten_blocks: boolean;
  flattening_intensity: number;
  decompiler_traps: boolean;
  exception_dispatch: boolean;
}

export interface IndyCallSiteOptions {
  enabled: boolean;
  target_packages: string[];
}

export interface ReflectionProxyOptions {
  enabled: boolean;
  proxy_invocations: boolean;
  proxy_java_imports: boolean;
  import_packages: string[];
  mh_lookup_helpers_min: number;
  mh_lookup_helpers_max: number;
}

export interface NumberObfuscationOptions {
  enabled: boolean;
  intensity: number;
}

export interface ShrinkOptions {
  enabled: boolean;
  shrink_classes: boolean;
  shrink_methods: boolean;
  shrink_fields: boolean;
  remove_unused_attributes: boolean;
}

export interface ClassEncryptionOptions {
  enabled: boolean;
  launcher_class: string;
  resource_name: string;
  main_class: string;
  memory_scrubbing: boolean;
}

export interface CondyOptions {
  enabled: boolean;
}

export interface DecompilerDeterrenceOptions {
  enabled: boolean;
  overload_return_types: boolean;
  synthetic_bridge_tags: boolean;
  phantom_inner_classes: boolean;
  custom_attributes: boolean;
  scramble_method_order: boolean;
}

export interface ObfuscatorConfig {
  profile_name: string;
  target_packages: string[];
  exclude_packages: string[];
  exclude_classes: string[];
  keep_rules: string[];
  ignore_dependencies: boolean;
  strip: StripOptions;
  shrink: ShrinkOptions;
  class_encryption: ClassEncryptionOptions;
  rename: RenameOptions;
  string_encryption: StringEncryptionOptions;
  access_reducer: AccessReducerOptions;
  control_flow: ControlFlowOptions;
  reflection_proxy: ReflectionProxyOptions;
  number_obfuscation: NumberObfuscationOptions;
  indy_call_sites: IndyCallSiteOptions;
  condy: CondyOptions;
  decompiler_deterrence: DecompilerDeterrenceOptions;
  input_jar: string;
  output_jar: string;
  mapping_file?: string;
  seed: number | null;
  verbose: boolean;
}

export interface UserProfile {
  id: string;
  name: string;
  description: string;
  isBuiltIn?: boolean;
  config: ObfuscatorConfig;
}
