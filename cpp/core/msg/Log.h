#pragma once

// 日志管理
void TVPConsoleLog(const tjs_char* l);

void TVPConsoleLog(const tjs_nchar* format, ...);

void TVPConsoleLog(const ttstr& l, bool important);