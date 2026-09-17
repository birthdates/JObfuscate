export type RenameMode = "long" | "confusing" | "short" | "hex";
export type StringAlgorithm = "MultiLayerCrypto" | "XorRollingKey";

export interface StripOptions {
  strip_line_numbers: boolean;
  strip_local_variables: boolean;
  strip_source_file: boolean;
  strip_source_debug: boolean;
  strip_signatures: boolean;
  strip_parameters: boolean;
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
}

export interface ReflectionProxyOptions {
  enabled: boolean;
  proxy_invocations: boolean;
  proxy_java_imports: boolean;
  import_packages: string[];
}

export interface NumberObfuscationOptions {
  enabled: boolean;
  intensity: number;
}

export interface ObfuscatorConfig {
  profile_name: string;
  target_packages: string[];
  exclude_packages: string[];
  exclude_classes: string[];
  ignore_dependencies: boolean;
  strip: StripOptions;
  rename: RenameOptions;
  string_encryption: StringEncryptionOptions;
  access_reducer: AccessReducerOptions;
  control_flow: ControlFlowOptions;
  reflection_proxy: ReflectionProxyOptions;
  number_obfuscation: NumberObfuscationOptions;
  input_jar: string;
  output_jar: string;
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
