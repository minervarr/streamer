#include "i18n.hh"

#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace i18n {

enum class Lang { En, Es };
static Lang g_lang = Lang::En;

static Lang detect_os() {
#ifdef _WIN32
    WORD primary = GetUserDefaultUILanguage() & 0x3FF;
    if (primary == 0x0A) return Lang::Es; // LANG_SPANISH
#else
    const char *env = std::getenv("LANG");
    if (env && env[0] == 'e' && env[1] == 's') return Lang::Es;
#endif
    return Lang::En;
}

void init(const std::string &lang_override) {
    if (lang_override == "es" || lang_override == "español" || lang_override == "spanish")
        g_lang = Lang::Es;
    else if (lang_override == "en" || lang_override == "english")
        g_lang = Lang::En;
    else
        g_lang = detect_os();
}

const char *t(const char *key) {
    bool es = (g_lang == Lang::Es);
#define T(k, en, sp) if (!strcmp(key, k)) return es ? (sp) : (en)

    // commands
    T("cmd_login",    "Authenticate with Qobuz (token-based); creates or updates the account slot for --country",
                      "Autenticar con Qobuz (por token); crea o actualiza la cuenta para --country");
    T("cmd_download", "Download a track, album, artist, or playlist by URL or ID",
                      "Descargar una pista, álbum, artista o lista de reproducción por URL o ID");
    T("cmd_search",   "Search the Qobuz catalog",
                      "Buscar en el catálogo de Qobuz");
    T("cmd_inspect",  "Inspect an album's track list and regional availability before downloading",
                      "Inspeccionar la lista de pistas y disponibilidad regional de un álbum");
    T("cmd_config",   "Manage configuration",
                      "Administrar configuración");
    T("cmd_history",  "View, export, or import download history",
                      "Ver, exportar o importar historial de descargas");
    T("cmd_set_dir",  "Set the download directory",
                      "Establecer el directorio de descargas");
    T("cmd_set_quality", "Set default quality: mp3 | flac | flac-hi | flac-ultra",
                         "Establecer calidad por defecto: mp3 | flac | flac-hi | flac-ultra");
    T("cmd_show",     "Print current config path and contents",
                      "Mostrar la ruta y contenido de la configuración actual");
    T("cmd_export",   "Export history as JSON",
                      "Exportar historial como JSON");
    T("cmd_import",   "Import history from a JSON file",
                      "Importar historial desde un archivo JSON");
    T("cmd_clear",    "Delete all history records",
                      "Eliminar todos los registros del historial");
    T("cmd_refresh_credentials",
        "Re-scrape app_id/app_secret from the Qobuz web player and save them",
        "Volver a extraer app_id/app_secret del reproductor web de Qobuz y guardarlos");

    // console
    T("login_success",       "Login successful. Config saved.",
                             "Inicio de sesión exitoso. Configuración guardada.");
    T("creds_refreshed",     "App credentials refreshed and saved.",
                             "Credenciales de aplicación actualizadas y guardadas.");
    T("creds_refresh_failed", "Could not refresh app credentials:",
                              "No se pudieron actualizar las credenciales:");
    T("creds_no_probe_track", "Could not find a track to validate the new secret against.",
                              "No se encontró una pista para validar el nuevo secreto.");
    T("no_history",          "No download history.",
                             "No hay historial de descargas.");

    // download
    T("downloading_track",    "Downloading track",    "Descargando pista");
    T("downloading_album",    "Downloading album",    "Descargando álbum");
    T("downloading_artist",   "Downloading artist",   "Descargando artista");
    T("downloading_playlist", "Downloading playlist", "Descargando lista de reproducción");
    T("full_discography",     "full discography",     "discografía completa");
    T("downloaded_tracks",    "Downloaded",           "Descargadas");
    T("tracks_suffix",        "track(s).",            "pista(s).");
    T("saved",                "Saved:",               "Guardado:");
    T("warning_could_not_save",     "Warning: could not save",     "Advertencia: no se pudo guardar");
    T("warning_could_not_read",     "Warning: could not read",     "Advertencia: no se pudo leer");
    T("warning_could_not_download", "Warning: could not download", "Advertencia: no se pudo descargar");
    T("warning_album_extras",  "Warning: could not fetch album extras:",
                               "Advertencia: no se pudieron obtener extras del álbum:");
    T("warning_artist_extras", "Warning: could not fetch artist extras:",
                               "Advertencia: no se pudieron obtener extras del artista:");

    // search
    T("search_tracks",    "Tracks",               "Pistas");
    T("search_albums",    "Albums",               "Álbumes");
    T("search_artists",   "Artists",              "Artistas");
    T("search_playlists", "Playlists",            "Listas de reproducción");
    T("results_suffix",   "results",              "resultados");

    // config show
    T("config_file",         "Config file:",        "Archivo de configuración:");
    T("download_dir_label",  "Download dir:",       "Directorio de descargas:");
    T("quality_label",       "Quality:",            "Calidad:");
    T("accounts_label",      "Accounts:",           "Cuentas:");
    T("authenticated",       "authenticated",       "autenticado");
    T("download_dir_set",    "Download directory set to:", "Directorio de descargas establecido en:");
    T("default_quality_set", "Default quality set to:",   "Calidad por defecto establecida en:");
    T("records_shown",       "record(s) shown.",    "registro(s) mostrados.");
    T("exported_to",         "Exported to",         "Exportado a");
    T("imported_records",    "Imported",            "Importados");
    T("new_records",         "new record(s).",      "registro(s) nuevos.");
    T("deleted_records",     "Deleted",             "Eliminados");
    T("records_suffix",      "record(s).",          "registro(s).");

    // quality labels
    T("quality_mp3",       "MP3 320kbps",       "MP3 320kbps");
    T("quality_flac",      "FLAC 16-bit",       "FLAC 16-bit");
    T("quality_flac_hi",   "FLAC 24-bit",       "FLAC 24-bit");
    T("quality_flac_ultra","FLAC 24-bit Hi-Res","FLAC 24-bit Hi-Res");

    // errors
    T("err_not_authenticated",
      "Not authenticated \xe2\x80\x94 run `streamer login` first",
      "No autenticado \xe2\x80\x94 ejecuta `streamer login` primero");
    T("err_app_id_not_set",
      "app_id is not set for this account. Edit it in Settings.",
      "app_id no est\xc3\xa1 configurado para esta cuenta. Ed\xc3\xadtalo en Configuraci\xc3\xb3n.");

    // inspect fields
    T("field_title",    "Title",    "T\xc3\xadtulo");
    T("field_artist",   "Artist",   "Artista");
    T("field_album",    "Album",    "\xc3\x81lbum");
    T("field_label",    "Label",    "Sello");
    T("field_date",     "Date",     "Fecha");
    T("field_genre",    "Genre",    "G\xc3\xa9nero");
    T("field_quality",  "Quality",  "Calidad");
    T("field_duration", "Duration", "Duraci\xc3\xb3n");
    T("field_hires",    "Hi-Res",   "Alta resoluci\xc3\xb3n");
    T("field_download", "Download", "Descarga");
    T("yes",            "yes",      "s\xc3\xad");
    T("no",             "no",       "no");
    T("no_tracks",      "No tracks.", "Sin pistas.");
    T("disc",           "Disc",     "Disco");

    // errors
    T("error_fetch",      "Error fetching:",   "Error al obtener:");
    T("error_parse_url",  "Error parsing URL", "Error al analizar URL");
    T("error_download",   "Download error:",   "Error de descarga:");

    // history
    T("history_ok",   "OK",   "OK");
    T("history_fail", "FAIL", "FALLO");

#undef T
    return "??";
}

} // namespace i18n
