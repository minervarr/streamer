use std::sync::OnceLock;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Lang {
    En,
    Es,
}

static LANG: OnceLock<Lang> = OnceLock::new();

pub fn init(config_lang: Option<&str>) {
    let lang = match config_lang {
        Some(s) => parse_lang(s).unwrap_or_else(detect_os_lang),
        None => detect_os_lang(),
    };
    let _ = LANG.set(lang);
}

pub fn current() -> Lang {
    *LANG.get().unwrap_or(&Lang::En)
}

fn parse_lang(s: &str) -> Option<Lang> {
    match s.to_ascii_lowercase().as_str() {
        "es" | "español" | "spanish" => Some(Lang::Es),
        "en" | "english" => Some(Lang::En),
        _ => None,
    }
}

fn detect_os_lang() -> Lang {
    #[cfg(windows)]
    {
        unsafe extern "system" {
            fn GetUserDefaultUILanguage() -> u16;
        }
        let lcid = unsafe { GetUserDefaultUILanguage() };
        let primary = lcid & 0x3FF;
        if primary == 0x0A { return Lang::Es; } // LANG_SPANISH
    }
    #[cfg(not(windows))]
    {
        if let Ok(lang) = std::env::var("LANG") {
            if lang.starts_with("es") { return Lang::Es; }
        }
    }
    Lang::En
}

macro_rules! define_messages {
    ( $( $key:ident => ($en:expr, $es:expr) ),* $(,)? ) => {
        pub fn t(key: &str) -> &'static str {
            let lang = current();
            match key {
                $( stringify!($key) => match lang {
                    Lang::En => $en,
                    Lang::Es => $es,
                }, )*
                _ => "??",
            }
        }
    };
}

define_messages! {
    // ── App description ──
    app_about => ("Qobuz music downloader", "Descargador de música Qobuz"),

    // ── Subcommand descriptions ──
    cmd_login => (
        "Authenticate with Qobuz (token-based); creates or updates the account slot for --country",
        "Autenticar con Qobuz (por token); crea o actualiza la cuenta para --country"
    ),
    cmd_download => (
        "Download a track, album, artist, or playlist by URL or ID",
        "Descargar una pista, álbum, artista o lista de reproducción por URL o ID"
    ),
    cmd_search => (
        "Search the Qobuz catalog",
        "Buscar en el catálogo de Qobuz"
    ),
    cmd_token_login => (
        "Save credentials obtained from the browser (WebView2 login flow); fetches app_secret automatically",
        "Guardar credenciales obtenidas del navegador (flujo WebView2); obtiene app_secret automáticamente"
    ),
    cmd_web_login => (
        "Authenticate with Qobuz via email+password; auto-fetches app credentials from web player",
        "Autenticar con Qobuz por email+contraseña; obtiene credenciales automáticamente del reproductor web"
    ),
    cmd_fetch_app_id => (
        "Print app_id from the Qobuz web player bundle (used by the GUI before showing the login dialog)",
        "Mostrar app_id del reproductor web de Qobuz (usado por la GUI antes de mostrar el diálogo de inicio de sesión)"
    ),
    cmd_oauth_login => (
        "Complete OAuth login: exchange authorization code for credentials and save to config",
        "Completar inicio de sesión OAuth: intercambiar código de autorización por credenciales y guardar en configuración"
    ),
    cmd_config => (
        "Manage configuration",
        "Administrar configuración"
    ),
    cmd_history => (
        "View, export, or import download history",
        "Ver, exportar o importar historial de descargas"
    ),

    // ── Argument descriptions ──
    arg_country => (
        "Country code to associate with this account (e.g. US, CN). Defaults to first slot.",
        "Código de país para asociar con esta cuenta (ej. US, CN). Por defecto el primer slot."
    ),
    arg_country_short => (
        "Country code to associate with this account (e.g. US, CN).",
        "Código de país para asociar con esta cuenta (ej. US, CN)."
    ),
    arg_url => (
        "Qobuz URL or bare ID",
        "URL de Qobuz o ID"
    ),
    arg_quality => (
        "Quality: mp3 | flac | flac-hi | flac-ultra  [default: from config]",
        "Calidad: mp3 | flac | flac-hi | flac-ultra  [por defecto: de configuración]"
    ),
    arg_output => (
        "Output directory  [default: from config]",
        "Directorio de salida  [por defecto: de configuración]"
    ),
    arg_account => (
        "Account index to use for download (0-based)  [default: 0]",
        "Índice de cuenta a usar para descarga (base 0)  [por defecto: 0]"
    ),
    arg_search_type => (
        "Filter by type: albums | tracks | artists | playlists | all  [default: albums]",
        "Filtrar por tipo: albums | tracks | artists | playlists | all  [por defecto: albums]"
    ),
    arg_tsv => (
        "Output tab-separated values for machine consumption",
        "Salida en valores separados por tabulación para procesamiento automático"
    ),
    arg_limit => (
        "Max results per type  [default: 20]",
        "Máximo de resultados por tipo  [por defecto: 20]"
    ),
    arg_account_short => (
        "Account index to use (0-based)  [default: 0]",
        "Índice de cuenta a usar (base 0)  [por defecto: 0]"
    ),
    arg_tsv_history => (
        "Output as tab-separated values (for GUI parsing)",
        "Salida como valores separados por tabulación (para la GUI)"
    ),
    arg_history_limit => (
        "Number of recent records to show",
        "Número de registros recientes a mostrar"
    ),

    // ── ConfigAction descriptions ──
    cmd_set_dir => (
        "Set the download directory",
        "Establecer el directorio de descargas"
    ),
    cmd_set_quality => (
        "Set default quality: mp3 | flac | flac-hi | flac-ultra",
        "Establecer calidad por defecto: mp3 | flac | flac-hi | flac-ultra"
    ),
    cmd_show => (
        "Print current config path and contents",
        "Mostrar la ruta y contenido de la configuración actual"
    ),

    // ── HistoryAction descriptions ──
    cmd_export => (
        "Export history as JSON",
        "Exportar historial como JSON"
    ),
    arg_export_path => (
        "Output file path (prints to stdout if omitted)",
        "Ruta del archivo de salida (imprime en stdout si se omite)"
    ),
    cmd_import => (
        "Import history from a JSON file",
        "Importar historial desde un archivo JSON"
    ),
    cmd_clear => (
        "Delete all history records",
        "Eliminar todos los registros del historial"
    ),

    // ── Console output ──
    login_success => ("Login successful. Config saved.", "Inicio de sesión exitoso. Configuración guardada."),
    fetching_app_secret => ("Fetching app secret from Qobuz web player...", "Obteniendo app secret del reproductor web de Qobuz..."),
    ok => ("ok", "ok"),
    fetching_credentials => ("Fetching app credentials and authenticating...", "Obteniendo credenciales y autenticando..."),
    config_saved => ("Config saved.", "Configuración guardada."),
    exchanging_oauth => ("Exchanging OAuth code for credentials...", "Intercambiando código OAuth por credenciales..."),
    no_history => ("No download history.", "No hay historial de descargas."),
    settings_saved => ("Settings saved.", "Configuración guardada."),

    // ── Download messages ──
    downloading_track => ("Downloading track", "Descargando pista"),
    downloading_album => ("Downloading album", "Descargando álbum"),
    downloading_artist => ("Downloading artist", "Descargando artista"),
    downloading_playlist => ("Downloading playlist", "Descargando lista de reproducción"),
    full_discography => ("full discography", "discografía completa"),
    downloaded_tracks => ("Downloaded", "Descargadas"),
    tracks_suffix => ("track(s).", "pista(s)."),
    saved => ("Saved:", "Guardado:"),
    warning_could_not_save => ("Warning: could not save", "Advertencia: no se pudo guardar"),
    warning_could_not_read => ("Warning: could not read", "Advertencia: no se pudo leer"),
    warning_could_not_download => ("Warning: could not download", "Advertencia: no se pudo descargar"),
    warning_album_extras => ("Warning: could not fetch album extras:", "Advertencia: no se pudieron obtener extras del álbum:"),
    warning_artist_extras => ("Warning: could not fetch artist extras:", "Advertencia: no se pudieron obtener extras del artista:"),

    // ── Search results ──
    search_tracks => ("Tracks", "Pistas"),
    search_albums => ("Albums", "Álbumes"),
    search_artists => ("Artists", "Artistas"),
    search_playlists => ("Playlists", "Listas de reproducción"),
    results_suffix => ("results", "resultados"),

    // ── Config show ──
    config_file => ("Config file:", "Archivo de configuración:"),
    download_dir_label => ("Download dir:", "Directorio de descargas:"),
    quality_label => ("Quality:", "Calidad:"),
    accounts_label => ("Accounts:", "Cuentas:"),
    authenticated => ("authenticated", "autenticado"),
    download_dir_set => ("Download directory set to:", "Directorio de descargas establecido en:"),
    default_quality_set => ("Default quality set to:", "Calidad por defecto establecida en:"),
    records_shown => ("record(s) shown.", "registro(s) mostrados."),
    exported_to => ("Exported to", "Exportado a"),
    imported_records => ("Imported", "Importados"),
    new_records => ("new record(s).", "registro(s) nuevos."),
    deleted_records => ("Deleted", "Eliminados"),
    records_suffix => ("record(s).", "registro(s)."),

    // ── Quality labels ──
    quality_mp3 => ("MP3 320kbps", "MP3 320kbps"),
    quality_flac => ("FLAC 16-bit", "FLAC 16-bit"),
    quality_flac_hi => ("FLAC 24-bit", "FLAC 24-bit"),
    quality_flac_ultra => ("FLAC 24-bit Hi-Res", "FLAC 24-bit Hi-Res"),

    // ── Errors ──
    err_not_authenticated => (
        "Not authenticated \u{2014} run `streamer login` first",
        "No autenticado \u{2014} ejecuta `streamer login` primero"
    ),
    err_app_id_not_set => (
        "app_id is not set for this account. Edit it in Settings.",
        "app_id no está configurado para esta cuenta. Edítalo en Configuración."
    ),

    // ── History format ──
    history_ok => ("OK", "OK"),
    history_fail => ("FAIL", "FALLO"),
}
