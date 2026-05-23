mod config;
mod download;
mod errors;
mod extras;
mod search;
mod url;

use std::path::PathBuf;
use std::process::exit;

use clap::{Parser, Subcommand};
use qobuz_api::api::{requests::set_requests_per_minute, service::QobuzApiService};
use tracing::error;
use tracing_subscriber::{EnvFilter, fmt};

use crate::errors::AppError;

#[derive(Parser)]
#[command(name = "streamer", about = "Qobuz music downloader")]
struct Cli {
    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand)]
enum Command {
    /// Authenticate with Qobuz (token-based)
    Login {
        #[arg(long)]
        user_id: String,
        #[arg(long)]
        token: String,
    },
    /// Download a track, album, artist, or playlist by URL or ID
    Download {
        /// Qobuz URL or bare ID
        url: String,
        /// Quality: mp3 | flac | flac-hi | flac-ultra  [default: from config]
        #[arg(short, long)]
        quality: Option<String>,
        /// Output directory  [default: from config]
        #[arg(short, long)]
        output: Option<PathBuf>,
    },
    /// Search the Qobuz catalog
    Search {
        query: String,
        /// Filter by type: albums | tracks | artists | playlists | all  [default: albums]
        #[arg(short = 't', long, default_value = "albums")]
        r#type: String,
        /// Output tab-separated values for machine consumption
        #[arg(long)]
        tsv: bool,
        /// Max results per type  [default: 20]
        #[arg(short = 'n', long, default_value = "20")]
        limit: u32,
    },
    /// Manage configuration
    Config {
        #[command(subcommand)]
        action: ConfigAction,
    },
}

#[derive(Subcommand)]
enum ConfigAction {
    /// Set the download directory
    SetDir { path: PathBuf },
    /// Set default quality: mp3 | flac | flac-hi | flac-ultra
    SetQuality { quality: String },
    /// Print current config path and contents
    Show,
}

fn main() {
    fmt().with_env_filter(EnvFilter::new("info")).init();

    if let Err(e) = run() {
        error!("{e}");
        exit(1);
    }
}

fn run() -> Result<(), AppError> {
    let cli = Cli::parse();
    let mut cfg = config::load()?;
    set_requests_per_minute(cfg.settings.requests_per_minute);

    match cli.command {
        Command::Login { user_id, token } => {
            cfg.credentials.user_id = user_id.clone();
            cfg.credentials.auth_token = token.clone();
            config::save(&cfg)?;

            // Verify the credentials work
            let mut api = build_api(&cfg)?;
            api.login_with_token(&user_id, &token)?;
            println!("Login successful. Config saved.");
        }

        Command::Download { url, quality, output } => {
            let target = url::parse(&url)
                .ok_or_else(|| AppError::Other(format!("Could not parse URL or ID: {url}")))?;

            let quality = quality
                .as_deref()
                .unwrap_or(&cfg.settings.quality)
                .to_string();
            let output_dir = output.unwrap_or_else(|| cfg.settings.download_dir.clone());

            let mut api = build_authenticated_api(&cfg)?;
            download::run(&mut api, target, &quality, &output_dir, cfg.settings.concurrency)?;
        }

        Command::Search { query, r#type, tsv, limit } => {
            let mut api = build_authenticated_api(&cfg)?;
            search::run(&mut api, &query, &r#type, tsv, limit)?;
        }

        Command::Config { action } => match action {
            ConfigAction::SetDir { path } => {
                cfg.settings.download_dir = path.clone();
                config::save(&cfg)?;
                println!("Download directory set to: {}", path.display());
            }
            ConfigAction::SetQuality { quality } => {
                cfg.settings.quality = quality.clone();
                config::save(&cfg)?;
                println!("Default quality set to: {quality}");
            }
            ConfigAction::Show => {
                let path = dirs::config_dir()
                    .unwrap_or_default()
                    .join("streamer")
                    .join("config.toml");
                println!("Config file: {}", path.display());
                println!("Download dir: {}", cfg.settings.download_dir.display());
                println!("Quality: {}", cfg.settings.quality);
                println!(
                    "Authenticated: {}",
                    !cfg.credentials.auth_token.is_empty()
                );
            }
        },
    }

    Ok(())
}

fn build_api(cfg: &config::Config) -> Result<QobuzApiService, AppError> {
    if cfg.credentials.app_id.is_empty() {
        return Err(AppError::Other(
            "app_id is not set in config. Add it to ~/.config/streamer/config.toml".to_string(),
        ));
    }
    Ok(QobuzApiService::with_credentials(
        &cfg.credentials.app_id,
        &cfg.credentials.app_secret,
    )?)
}

fn build_authenticated_api(cfg: &config::Config) -> Result<QobuzApiService, AppError> {
    if cfg.credentials.auth_token.is_empty() {
        return Err(AppError::NotAuthenticated);
    }
    let mut api = build_api(cfg)?;
    api.login_with_token(&cfg.credentials.user_id, &cfg.credentials.auth_token)?;
    Ok(api)
}
