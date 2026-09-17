import { ObfuscatorConfig, UserProfile } from "../types/config";

export const createDefaultConfig = (profileName = "Default"): ObfuscatorConfig => ({
  profile_name: profileName,
  target_packages: ["*"],
  exclude_packages: [],
  exclude_classes: [],
  ignore_dependencies: false,
  strip: {
    strip_line_numbers: true,
    strip_local_variables: true,
    strip_source_file: true,
    strip_source_debug: true,
    strip_signatures: false,
    strip_parameters: true,
  },
  rename: {
    enabled: true,
    rename_classes: true,
    rename_packages: true,
    remove_packages: false,
    rename_methods: true,
    rename_fields: true,
    obfuscate_enums: true,
    keep_main_class: false,
    mode: "long",
    exclusions: [],
  },
  string_encryption: {
    enabled: true,
    algorithm: "MultiLayerCrypto",
    key_seed: 0,
    encrypt_indy_concat: true,
    randomize_decryptor_names: true,
    strings_per_decryptor_min: 5,
    strings_per_decryptor_max: 10,
    encrypt_annotation_strings: true,
    exclusions: [],
  },
  access_reducer: {
    enabled: true, // Default ON
  },
  control_flow: {
    enabled: true,
    insert_opaque_predicates: true,
    flatten_blocks: true,
    flattening_intensity: 70,
  },
  reflection_proxy: {
    enabled: true,
    proxy_invocations: true,
    proxy_java_imports: false,
    import_packages: ["*"],
  },
  number_obfuscation: {
    enabled: true,
    intensity: 70,
  },
  input_jar: "",
  output_jar: "",
  seed: null,
  verbose: false,
});

export const BUILT_IN_PROFILES: UserProfile[] = [
  {
    id: "max-security",
    name: "Max Security",
    description: "All passes active: Aggressive homoglyphs, dynamic reflection, string encryption, control flow flattening, and number splitting.",
    isBuiltIn: true,
    config: {
      ...createDefaultConfig("Max Security"),
    },
  },
  {
    id: "balanced",
    name: "Balanced Production",
    description: "Production safe: Renaming, string encryption, number splitting, and debug stripping without control-flow flattening or dynamic reflection.",
    isBuiltIn: true,
    config: {
      ...createDefaultConfig("Balanced Production"),
      reflection_proxy: {
        enabled: false,
        proxy_invocations: false,
        proxy_java_imports: false,
        import_packages: ["*"],
      },
      control_flow: {
        enabled: false,
        insert_opaque_predicates: false,
        flatten_blocks: false,
        flattening_intensity: 50,
      },
    },
  },
  {
    id: "stealth",
    name: "Stealth (Strings Only)",
    description: "Minimal footprint: Multi-layer string encryption and debug stripping only, maintaining original class and method names.",
    isBuiltIn: true,
    config: {
      ...createDefaultConfig("Stealth"),
      access_reducer: { enabled: false },
      rename: {
        enabled: false,
        rename_classes: false,
        rename_packages: false,
        remove_packages: false,
        rename_methods: false,
        rename_fields: false,
        obfuscate_enums: false,
        keep_main_class: true,
        mode: "long",
        exclusions: [],
      },
      reflection_proxy: {
        enabled: false,
        proxy_invocations: false,
        proxy_java_imports: false,
        import_packages: ["*"],
      },
      control_flow: {
        enabled: false,
        insert_opaque_predicates: false,
        flatten_blocks: false,
        flattening_intensity: 50,
      },
      number_obfuscation: {
        enabled: false,
        intensity: 50,
      },
    },
  },
  {
    id: "library-safe",
    name: "Library / API Mode",
    description: "Preserves public APIs: Disables access reduction, keeps generic signatures, and protects public class contracts.",
    isBuiltIn: true,
    config: {
      ...createDefaultConfig("Library Mode"),
      access_reducer: { enabled: false },
      strip: {
        strip_line_numbers: true,
        strip_local_variables: true,
        strip_source_file: true,
        strip_source_debug: true,
        strip_signatures: false, // Keep signatures for library generics!
        strip_parameters: false,
      },
      rename: {
        enabled: true,
        rename_classes: false, // Don't rename library classes
        rename_packages: false,
        remove_packages: false,
        rename_methods: true,
        rename_fields: true,
        obfuscate_enums: false,
        keep_main_class: true,
        mode: "long",
        exclusions: [],
      },
      reflection_proxy: {
        enabled: false,
        proxy_invocations: false,
        proxy_java_imports: false,
        import_packages: ["*"],
      },
    },
  },
];
