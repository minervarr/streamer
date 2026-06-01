#pragma once
#include <windows.h>

enum class Lang { En, Es };

void        SetLang(Lang lang);
void        SetLangFromString(const wchar_t* s);
void        DetectLang();
Lang        GetLang();
const wchar_t* T(const wchar_t* key);
const char*    LangToStr(Lang lang);
Lang        StrToLang(const char* s);
