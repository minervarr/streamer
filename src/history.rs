use std::path::PathBuf;

use chrono::{DateTime, Utc};
use rusqlite::{Connection, params};
use serde::{Deserialize, Serialize};

use crate::errors::AppError;

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct DownloadRecord {
    #[serde(skip_deserializing)]
    pub id: i64,
    pub timestamp: i64,
    pub qobuz_id: String,
    #[serde(rename = "type")]
    pub item_type: String,
    pub title: Option<String>,
    pub artist_name: Option<String>,
    pub artist_id: Option<i64>,
    pub track_count: Option<i32>,
    pub quality: String,
    pub format_id: i32,
    pub output_dir: Option<String>,
    pub country: Option<String>,
    pub success: bool,
}

#[derive(Serialize, Deserialize)]
pub struct HistoryExport {
    pub version: u32,
    pub exported_at: String,
    pub records: Vec<DownloadRecord>,
}

fn db_path() -> PathBuf {
    dirs::config_dir()
        .unwrap_or_else(|| PathBuf::from("."))
        .join("streamer")
        .join("history.db")
}

fn open_db() -> Result<Connection, AppError> {
    let path = db_path();
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent)?;
    }
    let conn = Connection::open(&path)
        .map_err(|e| AppError::Other(format!("Database error: {e}")))?;
    conn.execute_batch(
        "CREATE TABLE IF NOT EXISTS downloads (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp INTEGER NOT NULL,
            qobuz_id TEXT NOT NULL,
            item_type TEXT NOT NULL,
            title TEXT,
            artist_name TEXT,
            artist_id INTEGER,
            track_count INTEGER,
            quality TEXT NOT NULL,
            format_id INTEGER NOT NULL,
            output_dir TEXT,
            country TEXT,
            success INTEGER NOT NULL DEFAULT 1
        );"
    ).map_err(|e| AppError::Other(format!("Schema error: {e}")))?;
    Ok(conn)
}

pub fn record(
    qobuz_id: &str,
    item_type: &str,
    title: Option<&str>,
    artist_name: Option<&str>,
    artist_id: Option<i64>,
    track_count: Option<i32>,
    quality: &str,
    format_id: i32,
    output_dir: Option<&str>,
    country: Option<&str>,
    success: bool,
) -> Result<(), AppError> {
    let conn = open_db()?;
    let now = Utc::now().timestamp();
    conn.execute(
        "INSERT INTO downloads (timestamp, qobuz_id, item_type, title, artist_name, artist_id, track_count, quality, format_id, output_dir, country, success)
         VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12)",
        params![now, qobuz_id, item_type, title, artist_name, artist_id, track_count, quality, format_id, output_dir, country, success as i32],
    ).map_err(|e| AppError::Other(format!("Insert failed: {e}")))?;
    Ok(())
}

pub fn list_recent(limit: u32) -> Result<Vec<DownloadRecord>, AppError> {
    let conn = open_db()?;
    let mut stmt = conn.prepare(
        "SELECT id, timestamp, qobuz_id, item_type, title, artist_name, artist_id, track_count, quality, format_id, output_dir, country, success
         FROM downloads ORDER BY timestamp DESC LIMIT ?1"
    ).map_err(|e| AppError::Other(format!("Query error: {e}")))?;

    let rows = stmt.query_map(params![limit], |row| {
        Ok(DownloadRecord {
            id: row.get(0)?,
            timestamp: row.get(1)?,
            qobuz_id: row.get(2)?,
            item_type: row.get(3)?,
            title: row.get(4)?,
            artist_name: row.get(5)?,
            artist_id: row.get(6)?,
            track_count: row.get(7)?,
            quality: row.get(8)?,
            format_id: row.get(9)?,
            output_dir: row.get(10)?,
            country: row.get(11)?,
            success: row.get::<_, i32>(12)? != 0,
        })
    }).map_err(|e| AppError::Other(format!("Query error: {e}")))?;

    let mut records = Vec::new();
    for row in rows {
        records.push(row.map_err(|e| AppError::Other(format!("Row error: {e}")))?);
    }
    Ok(records)
}

pub fn export_json() -> Result<String, AppError> {
    let conn = open_db()?;
    let mut stmt = conn.prepare(
        "SELECT id, timestamp, qobuz_id, item_type, title, artist_name, artist_id, track_count, quality, format_id, output_dir, country, success
         FROM downloads ORDER BY timestamp ASC"
    ).map_err(|e| AppError::Other(format!("Query error: {e}")))?;

    let rows = stmt.query_map([], |row| {
        Ok(DownloadRecord {
            id: row.get(0)?,
            timestamp: row.get(1)?,
            qobuz_id: row.get(2)?,
            item_type: row.get(3)?,
            title: row.get(4)?,
            artist_name: row.get(5)?,
            artist_id: row.get(6)?,
            track_count: row.get(7)?,
            quality: row.get(8)?,
            format_id: row.get(9)?,
            output_dir: row.get(10)?,
            country: row.get(11)?,
            success: row.get::<_, i32>(12)? != 0,
        })
    }).map_err(|e| AppError::Other(format!("Query error: {e}")))?;

    let mut records = Vec::new();
    for row in rows {
        records.push(row.map_err(|e| AppError::Other(format!("Row error: {e}")))?);
    }

    let export = HistoryExport {
        version: 1,
        exported_at: Utc::now().to_rfc3339(),
        records,
    };

    serde_json::to_string_pretty(&export)
        .map_err(|e| AppError::Other(format!("JSON serialize error: {e}")))
}

pub fn import_json(json: &str) -> Result<u32, AppError> {
    let export: HistoryExport = serde_json::from_str(json)
        .map_err(|e| AppError::Other(format!("JSON parse error: {e}")))?;

    let conn = open_db()?;
    let mut imported = 0u32;

    for rec in &export.records {
        // Skip duplicates (same qobuz_id + timestamp)
        let exists: bool = conn.query_row(
            "SELECT COUNT(*) > 0 FROM downloads WHERE qobuz_id = ?1 AND timestamp = ?2",
            params![rec.qobuz_id, rec.timestamp],
            |row| row.get(0),
        ).unwrap_or(false);

        if exists {
            continue;
        }

        conn.execute(
            "INSERT INTO downloads (timestamp, qobuz_id, item_type, title, artist_name, artist_id, track_count, quality, format_id, output_dir, country, success)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12)",
            params![rec.timestamp, rec.qobuz_id, rec.item_type, rec.title, rec.artist_name, rec.artist_id, rec.track_count, rec.quality, rec.format_id, rec.output_dir, rec.country, rec.success as i32],
        ).map_err(|e| AppError::Other(format!("Import insert failed: {e}")))?;
        imported += 1;
    }

    Ok(imported)
}

pub fn clear() -> Result<u32, AppError> {
    let conn = open_db()?;
    let count: u32 = conn.query_row("SELECT COUNT(*) FROM downloads", [], |row| row.get(0))
        .map_err(|e| AppError::Other(format!("Count error: {e}")))?;
    conn.execute("DELETE FROM downloads", [])
        .map_err(|e| AppError::Other(format!("Clear error: {e}")))?;
    Ok(count)
}

pub fn format_record_line(rec: &DownloadRecord) -> String {
    let dt = DateTime::from_timestamp(rec.timestamp, 0)
        .map(|d| d.format("%Y-%m-%d %H:%M").to_string())
        .unwrap_or_else(|| "?".into());
    let status = if rec.success { crate::i18n::t("history_ok") } else { crate::i18n::t("history_fail") };
    let title = rec.title.as_deref().unwrap_or("?");
    let artist = rec.artist_name.as_deref().unwrap_or("?");
    format!("[{dt}] {status}  {artist} — {title}  ({}, {})", rec.item_type, rec.quality)
}
