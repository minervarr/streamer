#include "Strings.h"
#include <unordered_map>
#include <string>

static Lang g_lang = Lang::En;

struct Entry { const wchar_t* en; const wchar_t* es; };

static const std::unordered_map<std::wstring, Entry>& GetTable() {
    static const std::unordered_map<std::wstring, Entry> table = {
        // ── MainWindow ──
        {L"Go",                     {L"Go",                          L"Ir"}},
        {L"url_placeholder",        {L"Paste a Qobuz URL and press Go to download directly",
                                     L"Pega una URL de Qobuz y presiona Ir para descargar directamente"}},
        {L"Search",                 {L"Search",                      L"Buscar"}},
        {L"Downloads",              {L"Downloads",                   L"Descargas"}},
        {L"Settings",               {L"Settings",                    L"Configuración"}},

        // ── SettingsPanel ──
        {L"Accounts",               {L"Accounts",                    L"Cuentas"}},
        {L"Country",                {L"Country",                     L"País"}},
        {L"Email",                  {L"Email",                       L"Correo"}},
        {L"Status",                 {L"Status",                      L"Estado"}},
        {L"+ Add",                  {L"+ Add",                       L"+ Añadir"}},
        {L"Remove",                 {L"Remove",                      L"Eliminar"}},
        {L"Password",               {L"Password",                    L"Contraseña"}},
        {L"Login with Qobuz",       {L"Login with Qobuz",            L"Iniciar sesión con Qobuz"}},
        {L"Download Dir",           {L"Download Dir",                L"Carpeta de descargas"}},
        {L"Browse...",              {L"Browse...",                    L"Examinar..."}},
        {L"Quality",                {L"Quality",                     L"Calidad"}},
        {L"Concurrency",            {L"Concurrency",                 L"Concurrencia"}},
        {L"Requests / min",         {L"Requests / min",              L"Peticiones / min"}},
        {L"Save Settings",          {L"Save Settings",               L"Guardar configuración"}},
        {L"Export Accounts",         {L"Export Accounts",              L"Exportar cuentas"}},
        {L"Import Accounts",         {L"Import Accounts",              L"Importar cuentas"}},
        {L"Language",               {L"Language",                    L"Idioma"}},
        {L"English",                {L"English",                     L"Inglés"}},
        {L"Español",                {L"Español",                     L"Español"}},

        // ── SettingsPanel messages ──
        {L"Select an account slot first.",
            {L"Select an account slot first.",
             L"Selecciona una cuenta primero."}},
        {L"Could not fetch Qobuz app credentials.\nCheck your internet connection.",
            {L"Could not fetch Qobuz app credentials.\nCheck your internet connection.",
             L"No se pudieron obtener las credenciales de Qobuz.\nVerifica tu conexión a internet."}},
        {L"Could not obtain authorization code from Qobuz.\nTry logging in again.",
            {L"Could not obtain authorization code from Qobuz.\nTry logging in again.",
             L"No se pudo obtener el código de autorización de Qobuz.\nIntenta iniciar sesión de nuevo."}},
        {L"Login successful.",
            {L"Login successful.",
             L"Inicio de sesión exitoso."}},
        {L"Login failed. Check Downloads tab for details.",
            {L"Login failed. Check Downloads tab for details.",
             L"Inicio de sesión fallido. Revisa la pestaña Descargas para más detalles."}},
        {L"Settings saved.",
            {L"Settings saved.",
             L"Configuración guardada."}},
        {L"Accounts exported.",
            {L"Accounts exported.",
             L"Cuentas exportadas."}},
        {L"account(s) imported.",
            {L"account(s) imported.",
             L"cuenta(s) importadas."}},
        {L"Not logged in",
            {L"Not logged in",
             L"Sin sesión"}},
        {L"Authenticated",
            {L"Authenticated",
             L"Autenticado"}},
        {L"Select download directory",
            {L"Select download directory",
             L"Seleccionar carpeta de descargas"}},
        {L"Login",
            {L"Login",
             L"Inicio de sesión"}},
        {L"Saved",
            {L"Saved",
             L"Guardado"}},
        {L"Export",
            {L"Export",
             L"Exportar"}},
        {L"Import",
            {L"Import",
             L"Importar"}},

        // ── SearchPanel ──
        {L"search_placeholder",
            {L"Search — e.g.  artist:\"Daft Punk\" AND year:2001-2010  |  hires:true  |  type:album",
             L"Buscar — ej.  artista:\"Daft Punk\" Y año:2001-2010  |  alta_res:verdadero  |  tipo:álbum"}},
        {L"Smart (auto)",           {L"Smart (auto)",                L"Automático"}},
        {L"Albums",                 {L"Albums",                      L"Álbumes"}},
        {L"Tracks",                 {L"Tracks",                      L"Pistas"}},
        {L"Artists",                {L"Artists",                     L"Artistas"}},
        {L"Playlists",              {L"Playlists",                   L"Listas de reproducción"}},
        {L"All types",              {L"All types",                   L"Todos los tipos"}},
        {L"Title",                  {L"Title",                       L"Título"}},
        {L"Artist",                 {L"Artist",                      L"Artista"}},
        {L"Label",                  {L"Label",                       L"Sello"}},
        {L"Date",                   {L"Date",                        L"Fecha"}},
        {L"Duration",               {L"Duration",                    L"Duración"}},
        {L"Genre",                  {L"Genre",                       L"Género"}},
        {L"Hi-Res",                 {L"Hi-Res",                      L"Hi-Res"}},
        {L"Type",                   {L"Type",                        L"Tipo"}},
        {L"Download Selected",      {L"Download Selected",            L"Descargar seleccionados"}},
        {L"Searching...",           {L"Searching...",                L"Buscando..."}},
        {L"results",                {L"results",                     L"resultados"}},
        {L"Selected",               {L"Selected",                    L"Seleccionados"}},
        {L"Clear All",              {L"Clear All",                   L"Limpiar todo"}},
        {L"Download",               {L"Download",                    L"Descargar"}},
        {L"Yes",                    {L"Yes",                         L"Sí"}},

        // ── DownloadsPanel ──
        {L"Progress",               {L"Progress",                    L"Progreso"}},
        {L"Cancel Selected",        {L"Cancel Selected",              L"Cancelar seleccionado"}},
        {L"Download History",       {L"Download History",             L"Historial de descargas"}},
        {L"Queued",                 {L"Queued",                      L"En cola"}},
        {L"Downloading",            {L"Downloading",                 L"Descargando"}},
        {L"Done",                   {L"Done",                        L"Completado"}},
        {L"Error",                  {L"Error",                       L"Error"}},
        {L"Cancelled",              {L"Cancelled",                   L"Cancelado"}},
        {L"Failed to start process",
            {L"Failed to start process",
             L"No se pudo iniciar el proceso"}},

        // ── LoginDialog ──
        {L"Cancel",                 {L"Cancel",                      L"Cancelar"}},
        {L"WebView2 could not initialize.\nMake sure Microsoft Edge is installed.",
            {L"WebView2 could not initialize.\nMake sure Microsoft Edge is installed.",
             L"No se pudo inicializar WebView2.\nAsegúrate de que Microsoft Edge esté instalado."}},
        {L"Login Error",
            {L"Login Error",
             L"Error de inicio de sesión"}},
    };
    return table;
}

void SetLang(Lang lang)     { g_lang = lang; }
Lang GetLang()              { return g_lang; }

void SetLangFromString(const wchar_t* s) {
    if (!s) return;
    std::wstring ws(s);
    if (ws == L"es" || ws == L"español" || ws == L"Español") g_lang = Lang::Es;
    else g_lang = Lang::En;
}

void DetectLang() {
    LANGID lid = GetUserDefaultUILanguage();
    WORD primary = lid & 0x3FF;
    if (primary == 0x0A) g_lang = Lang::Es; // LANG_SPANISH
}

const wchar_t* T(const wchar_t* key) {
    auto& table = GetTable();
    auto it = table.find(key);
    if (it == table.end()) return key;
    return (g_lang == Lang::Es) ? it->second.es : it->second.en;
}

const char* LangToStr(Lang lang) {
    return (lang == Lang::Es) ? "es" : "en";
}

Lang StrToLang(const char* s) {
    if (!s) return Lang::En;
    if (s[0] == 'e' && s[1] == 's') return Lang::Es;
    return Lang::En;
}
