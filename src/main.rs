mod config;
mod download;
mod errors;
mod extras;
mod history;
pub mod i18n;
mod search;
mod url;

use std::path::PathBuf;
use std::process::exit;

use clap::{Parser, Subcommand};
use qobuz_api::api::{auth, requests::set_requests_per_minute, service::QobuzApiService};
use qobuz_api::credentials::web::{extract_from_web_player, extract_from_web_player_full};
use tracing::error;
use tracing_subscriber::{EnvFilter, fmt};

use crate::config::Account;
use crate::errors::AppError;
use crate::i18n::t;

#[derive(Parser)]
#[command(name = "streamer", about = "Qobuz music downloader")]
struct Cli {
    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand)]
enum Command {
    /// Authenticate with Qobuz (token-based); creates or updates the account slot for --country
    Login {
        #[arg(long)]
        user_id: String,
        #[arg(long)]
        token: String,
        /// Country code to associate with this account (e.g. US, CN). Defaults to first slot.
        #[arg(long, default_value = "")]
        country: String,
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
        /// Account index to use for download (0-based)  [default: 0]
        #[arg(long, default_value_t = 0)]
        account: usize,
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
        /// Account index to use (0-based)  [default: 0]
        #[arg(long, default_value_t = 0)]
        account: usize,
    },
    /// Save credentials obtained from the browser (WebView2 login flow); fetches app_secret automatically
    TokenLogin {
        #[arg(long)] user_id: String,
        #[arg(long)] token:   String,
        #[arg(long)] app_id:  String,
        #[arg(long, default_value = "")] country: String,
        #[arg(long, default_value = "")] email:   String,
    },
    /// Authenticate with Qobuz via email+password; auto-fetches app credentials from web player
    WebLogin {
        #[arg(long)]
        email: String,
        #[arg(long)]
        password: String,
        /// Country code to associate with this account (e.g. US, CN).
        #[arg(long, default_value = "")]
        country: String,
    },
    /// Print app_id from the Qobuz web player bundle (used by the GUI before showing the login dialog)
    FetchAppId,
    /// Complete OAuth login: exchange authorization code for credentials and save to config
    OauthLogin {
        #[arg(long)] code:    String,
        #[arg(long, default_value = "")] country: String,
        #[arg(long, default_value = "")] email:   String,
    },
    /// Manage configuration
    Config {
        #[command(subcommand)]
        action: ConfigAction,
    },
    /// View, export, or import download history
    History {
        /// Output as tab-separated values (for GUI parsing)
        #[arg(long)]
        tsv: bool,
        /// Number of recent records to show
        #[arg(short = 'n', long, default_value = "200")]
        limit: u32,
        #[command(subcommand)]
        action: Option<HistoryAction>,
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

#[derive(Subcommand)]
enum HistoryAction {
    /// Export history as JSON
    Export {
        /// Output file path (prints to stdout if omitted)
        path: Option<PathBuf>,
    },
    /// Import history from a JSON file
    Import { path: PathBuf },
    /// Delete all history records
    Clear,
}

fn main() {
    fmt().with_ansi(false).with_env_filter(EnvFilter::new(
        "warn,streamer=info,qobuz_api::api::auth=info,lofty=error"
    )).init();

    if let Err(e) = run() {
        error!("{e}");
        exit(1);
    }
}

fn run() -> Result<(), AppError> {
    let cfg_pre = config::load()?;
    i18n::init(cfg_pre.settings.language.as_deref());
    drop(cfg_pre);

    let cli = Cli::parse();
    let mut cfg = config::load()?;
    set_requests_per_minute(cfg.settings.requests_per_minute);

    match cli.command {
        Command::Login { user_id, token, country } => {
            // Find existing account by country or add new one
            let slot = if country.is_empty() {
                cfg.accounts.first_mut()
            } else {
                cfg.accounts.iter_mut().find(|a| a.country.eq_ignore_ascii_case(&country))
            };

            if let Some(acct) = slot {
                acct.user_id    = user_id.clone();
                acct.auth_token = token.clone();
            } else {
                cfg.accounts.push(Account {
                    country,
                    user_id: user_id.clone(),
                    auth_token: token.clone(),
                    ..Account::default()
                });
            }
            config::save(&cfg)?;

            // Verify credentials with the updated account
            let acct = cfg.accounts.iter()
                .find(|a| a.user_id == user_id)
                .ok_or_else(|| AppError::Other("Account not found after save".into()))?;
            let mut api = build_api(acct)?;
            api.login_with_token(&user_id, &token)?;
            println!("{}", t("login_success"));
        }

        Command::Download { url, quality, output, account } => {
            let acct = cfg.accounts.get(account)
                .ok_or_else(|| AppError::Other(format!("No account at index {account}")))?
                .clone();

            let target = url::parse(&url)
                .ok_or_else(|| AppError::Other(format!("Could not parse URL or ID: {url}")))?;

            let quality = quality
                .as_deref()
                .unwrap_or(&cfg.settings.quality)
                .to_string();

            let base_dir = output.unwrap_or_else(|| cfg.settings.download_dir.clone());
            // Prepend country subdirectory when set
            let output_dir = if acct.country.is_empty() {
                base_dir
            } else {
                base_dir.join(&acct.country)
            };

            let country_ref = if acct.country.is_empty() { None } else { Some(acct.country.as_str()) };
            let mut api = build_authenticated_api(&acct)?;
            download::run(&mut api, target, &quality, &output_dir, cfg.settings.concurrency, country_ref)?;
        }

        Command::Search { query, r#type, tsv, limit, account } => {
            let acct = cfg.accounts.get(account)
                .ok_or_else(|| AppError::Other(format!("No account at index {account}")))?
                .clone();
            let mut api = build_authenticated_api(&acct)?;
            search::run(&mut api, &query, &r#type, tsv, limit)?;
        }

        Command::TokenLogin { user_id, token, app_id, country, email } => {
            // Fetch app_secret from web player (app_id is already known from browser JS)
            println!("{}", t("fetching_app_secret"));
            let rt = tokio::runtime::Runtime::new()?;
            let (_fetched_app_id, app_secret) = rt.block_on(extract_from_web_player())
                .unwrap_or_else(|_| (String::new(), String::new()));

            let slot = if country.is_empty() {
                cfg.accounts.first_mut()
            } else {
                cfg.accounts.iter_mut().find(|a| a.country.eq_ignore_ascii_case(&country))
            };

            if let Some(acct) = slot {
                acct.email      = email;
                acct.app_id     = app_id;
                acct.app_secret = app_secret;
                acct.user_id    = user_id;
                acct.auth_token = token;
            } else {
                cfg.accounts.push(Account {
                    country, email, app_id, app_secret, user_id, auth_token: token,
                    ..Account::default()
                });
            }
            config::save(&cfg)?;
            println!("{}", t("ok"));
        }

        Command::WebLogin { email, password, country } => {
            println!("{}", t("fetching_credentials"));
            let (app_id, app_secret, auth_token, user_id_i64) =
                auth::web_login(&email, &password)?;
            let user_id = user_id_i64.to_string();
            println!("{} {email} (user_id={user_id})", t("authenticated"));

            let slot = if country.is_empty() {
                cfg.accounts.first_mut()
            } else {
                cfg.accounts.iter_mut().find(|a| a.country.eq_ignore_ascii_case(&country))
            };

            if let Some(acct) = slot {
                acct.email      = email;
                acct.app_id     = app_id;
                acct.app_secret = app_secret;
                acct.user_id    = user_id;
                acct.auth_token = auth_token;
            } else {
                cfg.accounts.push(Account {
                    country,
                    email,
                    app_id,
                    app_secret,
                    user_id,
                    auth_token,
                    ..Account::default()
                });
            }
            config::save(&cfg)?;
            println!("{}", t("config_saved"));
        }

        Command::FetchAppId => {
            let rt = tokio::runtime::Runtime::new()?;
            let (app_id, _, _) = rt.block_on(extract_from_web_player_full())
                .map_err(|e| AppError::Other(format!("Failed to fetch app_id: {e}")))?;
            println!("{app_id}");
        }

        Command::OauthLogin { code, country, email } => {
            println!("{}", t("exchanging_oauth"));
            let (app_id, app_secret, user_auth_token, user_id, api_country) = auth::oauth_login(&code)
                .map_err(|e| AppError::Other(format!("OAuth login failed: {e}")))?;

            // Use the country from the API if the user didn't specify one
            let country = if country.is_empty() { api_country } else { country };

            let slot = if country.is_empty() {
                cfg.accounts.first_mut()
            } else {
                cfg.accounts.iter_mut().find(|a| a.country.eq_ignore_ascii_case(&country))
            };

            if let Some(acct) = slot {
                acct.email      = email;
                acct.app_id     = app_id;
                acct.app_secret = app_secret;
                acct.user_id    = user_id;
                acct.auth_token = user_auth_token;
            } else {
                cfg.accounts.push(Account {
                    country, email, app_id, app_secret, user_id, auth_token: user_auth_token,
                    ..Account::default()
                });
            }
            config::save(&cfg)?;
            println!("{}", t("ok"));
        }

        Command::History { tsv, limit, action } => match action {
            None => {
                let records = history::list_recent(limit)?;
                if tsv {
                    for rec in &records {
                        println!("{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}",
                            rec.timestamp,
                            rec.qobuz_id,
                            rec.item_type,
                            rec.title.as_deref().unwrap_or(""),
                            rec.artist_name.as_deref().unwrap_or(""),
                            rec.quality,
                            rec.country.as_deref().unwrap_or(""),
                            if rec.success { "OK" } else { "FAIL" });
                    }
                } else if records.is_empty() {
                    println!("{}", t("no_history"));
                } else {
                    for rec in &records {
                        println!("{}", history::format_record_line(rec));
                    }
                    println!("\n{} {}", records.len(), t("records_shown"));
                }
            }
            Some(HistoryAction::Export { path }) => {
                let json = history::export_json()?;
                if let Some(p) = path {
                    std::fs::write(&p, &json)?;
                    println!("{} {}", t("exported_to"), p.display());
                } else {
                    println!("{json}");
                }
            }
            Some(HistoryAction::Import { path }) => {
                let json = std::fs::read_to_string(&path)?;
                let count = history::import_json(&json)?;
                println!("{} {count} {}", t("imported_records"), t("new_records"));
            }
            Some(HistoryAction::Clear) => {
                let count = history::clear()?;
                println!("{} {count} {}", t("deleted_records"), t("records_suffix"));
            }
        },

        Command::Config { action } => match action {
            ConfigAction::SetDir { path } => {
                cfg.settings.download_dir = path.clone();
                config::save(&cfg)?;
                println!("{} {}", t("download_dir_set"), path.display());
            }
            ConfigAction::SetQuality { quality } => {
                cfg.settings.quality = quality.clone();
                config::save(&cfg)?;
                println!("{} {quality}", t("default_quality_set"));
            }
            ConfigAction::Show => {
                let path = dirs::config_dir()
                    .unwrap_or_default()
                    .join("streamer")
                    .join("config.toml");
                println!("{} {}", t("config_file"), path.display());
                println!("{} {}", t("download_dir_label"), cfg.settings.download_dir.display());
                println!("{} {}", t("quality_label"), cfg.settings.quality);
                println!("{} {}", t("accounts_label"), cfg.accounts.len());
                for (i, a) in cfg.accounts.iter().enumerate() {
                    println!("  [{}] country={} {}={}", i, a.country, t("authenticated"), !a.auth_token.is_empty());
                }
            }
        },
    }

    Ok(())
}

fn build_api(acct: &Account) -> Result<QobuzApiService, AppError> {
    if acct.app_id.is_empty() {
        return Err(AppError::Other(t("err_app_id_not_set").to_string()));
    }
    Ok(QobuzApiService::with_credentials(&acct.app_id, &acct.app_secret)?)
}

fn build_authenticated_api(acct: &Account) -> Result<QobuzApiService, AppError> {
    if acct.auth_token.is_empty() {
        return Err(AppError::NotAuthenticated);
    }
    let mut api = build_api(acct)?;
    api.login_with_token(&acct.user_id, &acct.auth_token)?;
    Ok(api)
}
