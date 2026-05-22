//! Fetches supplementary album/artist metadata not captured by the qobuz-api models.

use std::time::{SystemTime, UNIX_EPOCH};

use qobuz_api::{api::{requests::build_url_with_params, service::QobuzApiService}, signing::sign_request};
use serde::Deserialize;

use crate::errors::AppError;

// ── Album extras ─────────────────────────────────────────────────────────────

#[derive(Debug, Deserialize)]
pub struct AlbumExtras {
    pub description: Option<String>,
    #[serde(default)]
    pub goodies: Option<Vec<Goody>>,
}

#[derive(Debug, Deserialize)]
pub struct Goody {
    #[serde(default)]
    pub url: String,
    #[serde(default)]
    pub original_url: String,
    pub file_format_id: Option<u32>,
}

impl Goody {
    pub fn best_url(&self) -> &str {
        if !self.original_url.is_empty() {
            &self.original_url
        } else {
            &self.url
        }
    }

    pub fn is_pdf(&self) -> bool {
        self.url.ends_with(".pdf") || self.original_url.ends_with(".pdf")
            || self.file_format_id == Some(21)
    }
}

// ── Artist extras ─────────────────────────────────────────────────────────────

#[derive(Debug, Deserialize)]
pub struct ArtistExtras {
    pub biography: Option<Bio>,
    pub image: Option<ArtistImage>,
}

#[derive(Debug, Deserialize)]
pub struct Bio {
    pub text: Option<String>,
    pub summary: Option<String>,
}

impl Bio {
    pub fn best_text(&self) -> Option<&str> {
        self.text.as_deref().or(self.summary.as_deref())
    }
}

#[derive(Debug, Deserialize)]
pub struct ArtistImage {
    pub mega: Option<String>,
    #[serde(rename = "extralarge")]
    pub extra_large: Option<String>,
    pub large: Option<String>,
    pub medium: Option<String>,
}

impl ArtistImage {
    pub fn best_url(&self) -> Option<&str> {
        self.mega
            .as_deref()
            .or(self.extra_large.as_deref())
            .or(self.large.as_deref())
            .or(self.medium.as_deref())
    }
}

// ── Fetch helpers ─────────────────────────────────────────────────────────────

fn now_ts() -> String {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs()
        .to_string()
}

fn signed_url(base_url: &str, endpoint: &str, mut params: Vec<(String, String)>, app_id: &str, app_secret: &str) -> String {
    params.push(("app_id".to_string(), app_id.to_string()));
    params.push(("request_ts".to_string(), now_ts()));
    let sig = sign_request("GET", endpoint, &mut params, app_secret);
    params.push(("request_sig".to_string(), sig));
    build_url_with_params(base_url, endpoint, &params)
}

fn get_json<T: serde::de::DeserializeOwned>(url: &str, auth_token: &str) -> Result<T, AppError> {
    let resp = reqwest::blocking::Client::new()
        .get(url)
        .header("X-User-Auth-Token", auth_token)
        .send()
        .map_err(|e| AppError::Other(e.to_string()))?;

    resp.json::<T>().map_err(|e| AppError::Other(e.to_string()))
}

pub fn fetch_album_extras(service: &QobuzApiService, album_id: &str) -> Result<AlbumExtras, AppError> {
    let token = service.require_auth_token().map_err(|e| AppError::Other(e.to_string()))?;
    let url = signed_url(
        service.base_url(),
        "/album/get",
        vec![("album_id".to_string(), album_id.to_string())],
        &service.app_id,
        service.app_secret(),
    );
    get_json(&url, token)
}

pub fn fetch_artist_extras(service: &QobuzApiService, artist_id: i32) -> Result<ArtistExtras, AppError> {
    let token = service.require_auth_token().map_err(|e| AppError::Other(e.to_string()))?;
    let url = signed_url(
        service.base_url(),
        "/artist/get",
        vec![("artist_id".to_string(), artist_id.to_string())],
        &service.app_id,
        service.app_secret(),
    );
    get_json(&url, token)
}
