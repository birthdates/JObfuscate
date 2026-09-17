use std::fs;
use std::io::{BufRead, BufReader};
use std::path::Path;
use std::process::{Command, Stdio};
use std::sync::{Arc, Mutex};
use tauri::{AppHandle, Emitter, State};

#[derive(Default)]
pub struct AppState {
    pub running_pid: Arc<Mutex<Option<u32>>>,
}

#[tauri::command]
fn pick_input_jar() -> Option<String> {
    let file = rfd::FileDialog::new()
        .add_filter("Java Archive (*.jar, *.war)", &["jar", "war", "zip"])
        .pick_file()?;
    Some(file.to_string_lossy().to_string())
}

#[tauri::command]
fn pick_output_jar(default_name: Option<String>) -> Option<String> {
    let mut dialog = rfd::FileDialog::new()
        .add_filter("Java Archive (*.jar)", &["jar", "war"]);
    if let Some(name) = default_name {
        dialog = dialog.set_file_name(&name);
    }
    let file = dialog.save_file()?;
    Some(file.to_string_lossy().to_string())
}

#[tauri::command]
fn pick_config_file() -> Option<String> {
    let file = rfd::FileDialog::new()
        .add_filter("JSON Profile (*.json)", &["json"])
        .pick_file()?;
    fs::read_to_string(file).ok()
}

#[tauri::command]
fn save_config_file(content: String, default_name: Option<String>) -> Option<String> {
    let mut dialog = rfd::FileDialog::new()
        .add_filter("JSON Profile (*.json)", &["json"]);
    if let Some(name) = default_name {
        dialog = dialog.set_file_name(&name);
    } else {
        dialog = dialog.set_file_name("jobfuscate-profile.json");
    }
    let file = dialog.save_file()?;
    fs::write(&file, content).ok()?;
    Some(file.to_string_lossy().to_string())
}

#[tauri::command]
fn detect_cli_binary(app: tauri::AppHandle) -> Result<String, String> {
    use tauri::Manager;

    let mut search_paths = Vec::new();

    // 1. Installed Resource directory (MSI, NSIS, DMG, AppImage)
    if let Ok(res_dir) = app.path().resource_dir() {
        search_paths.push(res_dir.join("bin").join("obfuscator_cli.exe"));
        search_paths.push(res_dir.join("bin").join("obfuscator_cli"));
        search_paths.push(res_dir.join("obfuscator_cli.exe"));
        search_paths.push(res_dir.join("obfuscator_cli"));
        search_paths.push(res_dir.join("resources").join("bin").join("obfuscator_cli.exe"));
        search_paths.push(res_dir.join("resources").join("bin").join("obfuscator_cli"));
    }

    // 2. Installed executable directory
    if let Ok(curr_exe) = std::env::current_exe() {
        if let Some(dir) = curr_exe.parent() {
            search_paths.push(dir.join("obfuscator_cli.exe"));
            search_paths.push(dir.join("obfuscator_cli"));
            search_paths.push(dir.join("bin").join("obfuscator_cli.exe"));
            search_paths.push(dir.join("bin").join("obfuscator_cli"));
            search_paths.push(dir.join("resources").join("bin").join("obfuscator_cli.exe"));
            search_paths.push(dir.join("resources").join("bin").join("obfuscator_cli"));
        }
    }

    // 3. Local build & development workspace candidates
    let candidates = [
        "../build/core/Release/obfuscator_cli.exe",
        "../../build/core/Release/obfuscator_cli.exe",
        "build/core/Release/obfuscator_cli.exe",
        "../build/core/obfuscator_cli",
        "../../build/core/obfuscator_cli",
        "build/core/obfuscator_cli",
        "bin/obfuscator_cli.exe",
        "bin/obfuscator_cli",
        "obfuscator_cli.exe",
        "obfuscator_cli",
    ];
    for c in candidates {
        search_paths.push(Path::new(c).to_path_buf());
    }

    for p in search_paths {
        if p.exists() {
            if let Ok(canon) = fs::canonicalize(&p) {
                let s = canon.to_string_lossy().to_string();
                let clean = if let Some(stripped) = s.strip_prefix(r"\\?\") {
                    stripped.to_string()
                } else {
                    s
                };
                return Ok(clean);
            }
            return Ok(p.to_string_lossy().to_string());
        }
    }

    Err("Could not automatically locate obfuscator_cli binary. Please build the project or configure binary path.".to_string())
}

#[tauri::command]
async fn run_obfuscation(
    app: AppHandle,
    state: State<'_, AppState>,
    cli_path: Option<String>,
    input_jar: Option<String>,
    output_jar: Option<String>,
    config_json: String,
) -> Result<i32, String> {
    let binary = match cli_path {
        Some(path) if Path::new(&path).exists() => path,
        _ => detect_cli_binary(app.clone())?,
    };

    let temp_dir = std::env::temp_dir();
    let temp_config_path = temp_dir.join(format!("jobfuscate_temp_{}.json", std::process::id()));
    fs::write(&temp_config_path, &config_json)
        .map_err(|e| format!("Failed to write temporary config: {}", e))?;

    let mut cmd = Command::new(&binary);
    cmd.arg("-c")
        .arg(&temp_config_path);

    if let Some(ref in_path) = input_jar {
        let trimmed = in_path.trim();
        if !trimmed.is_empty() {
            cmd.arg("-i").arg(trimmed);
        }
    }
    if let Some(ref out_path) = output_jar {
        let trimmed = out_path.trim();
        if !trimmed.is_empty() {
            cmd.arg("-o").arg(trimmed);
        }
    }

    cmd.arg("--json")
        .stdout(Stdio::piped())
        .stderr(Stdio::piped());

    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        cmd.creation_flags(0x08000000); // CREATE_NO_WINDOW
    }

    let mut child = cmd.spawn()
        .map_err(|e| format!("Failed to launch obfuscator_cli at '{}': {}", binary, e))?;

    let pid = child.id();
    {
        let mut running = state.running_pid.lock().unwrap();
        *running = Some(pid);
    }

    let stdout = child.stdout.take().ok_or("Failed to capture stdout")?;
    let stderr = child.stderr.take().ok_or("Failed to capture stderr")?;

    let app_clone = app.clone();
    let stdout_handle = std::thread::spawn(move || {
        let reader = BufReader::new(stdout);
        for line in reader.lines().map_while(Result::ok) {
            let _ = app_clone.emit("obf:event", &line);
        }
    });

    let app_err_clone = app.clone();
    let stderr_handle = std::thread::spawn(move || {
        let reader = BufReader::new(stderr);
        for line in reader.lines().map_while(Result::ok) {
            let _ = app_err_clone.emit("obf:stderr", &line);
        }
    });

    let status = child.wait().map_err(|e| format!("Process error: {}", e))?;
    let _ = stdout_handle.join();
    let _ = stderr_handle.join();

    {
        let mut running = state.running_pid.lock().unwrap();
        *running = None;
    }

    let _ = fs::remove_file(&temp_config_path);

    let code = status.code().unwrap_or(if status.success() { 0 } else { -1 });
    let _ = app.emit(
        "obf:done",
        serde_json::json!({
            "exit_code": code,
            "error": if status.success() { None } else { Some(format!("Process exited with code {}", code)) }
        }),
    );

    Ok(code)
}

#[tauri::command]
fn cancel_obfuscation(state: State<'_, AppState>) -> Result<(), String> {
    let pid_opt = {
        let mut running = state.running_pid.lock().unwrap();
        running.take()
    };

    if let Some(pid) = pid_opt {
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            let _ = Command::new("taskkill")
                .args(["/F", "/T", "/PID", &pid.to_string()])
                .creation_flags(0x08000000)
                .output();
        }
        #[cfg(not(windows))]
        {
            let _ = Command::new("kill")
                .args(["-9", &pid.to_string()])
                .output();
        }
    }

    Ok(())
}

#[tauri::command]
fn window_minimize(window: tauri::Window) {
    let _ = window.minimize();
}

#[tauri::command]
fn window_toggle_maximize(window: tauri::Window) -> bool {
    if let Ok(is_max) = window.is_maximized() {
        if is_max {
            let _ = window.unmaximize();
            false
        } else {
            let _ = window.maximize();
            true
        }
    } else {
        false
    }
}

#[tauri::command]
fn window_close(window: tauri::Window) {
    let _ = window.close();
}

#[tauri::command]
fn window_start_dragging(window: tauri::Window) {
    let _ = window.start_dragging();
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .manage(AppState::default())
        .plugin(tauri_plugin_opener::init())
        .invoke_handler(tauri::generate_handler![
            pick_input_jar,
            pick_output_jar,
            pick_config_file,
            save_config_file,
            detect_cli_binary,
            run_obfuscation,
            cancel_obfuscation,
            window_minimize,
            window_toggle_maximize,
            window_close,
            window_start_dragging
        ])
        .run(tauri::generate_context!())
        .expect("error while running JObfuscate application");
}
