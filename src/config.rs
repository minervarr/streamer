use std::{fs, path::PathBuf};

use serde::{Deserialize, Serialize};

use crate::errors::AppError;

#[derive(Debug, Serialize, Deserialize, Default, Clone)]
pub struct Account {
    pub country:    String,
    #[serde(default)]
    pub email:      String,
    pub app_id:     String,
    pub app_secret: String,
    pub user_id:    String,
    pub auth_token: String,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct Settings {
    pub download_dir: PathBuf,
    pub quality: String,
    /// Max Qobuz API requests per minute. 0 = unlimited.
    #[serde(default = "default_requests_per_minute")]
    pub requests_per_minute: u32,
    /// Max concurrent track downloads. Higher = faster on good connections.
    #[serde(default = "default_concurrency")]
    pub concurrency: usize,
    /// UI language: "en" | "es". Auto-detected from OS if omitted.
    #[serde(default)]
    pub language: Option<String>,
}

fn default_requests_per_minute() -> u32 { 0 }
fn default_concurrency() -> usize { 4 }

impl Default for Settings {
    fn default() -> Self {
        Self {
            download_dir: dirs::audio_dir()
                .or_else(dirs::home_dir)
                .unwrap_or_else(|| PathBuf::from(".")),
            quality: "flac".to_string(),
            requests_per_minute: default_requests_per_minute(),
            concurrency: default_concurrency(),
            language: None,
        }
    }
}

#[derive(Debug, Serialize, Deserialize, Default)]
pub struct Config {
    #[serde(default, rename = "accounts")]
    pub accounts: Vec<Account>,
    #[serde(default)]
    pub settings: Settings,
}

fn config_path() -> PathBuf {
    dirs::config_dir()
        .unwrap_or_else(|| PathBuf::from("."))
        .join("streamer")
        .join("config.toml")
}

pub fn load() -> Result<Config, AppError> {
    let path = config_path();
    if !path.exists() {
        return Ok(Config::default());
    }
    let text = fs::read_to_string(&path)?;
    let mut cfg: Config = toml::from_str(&text)?;

    let dir = cfg.settings.download_dir.to_string_lossy();
    let dir = dir.trim_matches('\'').trim_matches('"');
    cfg.settings.download_dir = PathBuf::from(dir);

    Ok(cfg)
}

pub fn save(config: &Config) -> Result<(), AppError> {
    let path = config_path();
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)?;
    }
    let text = toml::to_string_pretty(config)?;
    fs::write(&path, text)?;
    Ok(())
}
