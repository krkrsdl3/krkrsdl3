//---------------------------------------------------------------------------
/*
	TVP2 ( T Visual Presenter 2 )  A script authoring tool
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Utilities for Debugging
//---------------------------------------------------------------------------
#ifndef DebugIntfH
#define DebugIntfH

//---------------------------------------------------------------------------
// global definitions
//---------------------------------------------------------------------------
extern bool TVPAutoLogToFileOnError;
extern bool TVPAutoClearLogOnError;
extern bool TVPLoggingToFile;
extern ttstr TVPNativeLogLocation;
extern tjs_uint TVPLogMaxLines;
extern ttstr TVPLogLocation;

extern void TVPSetOnLog(void (*func)(const ttstr& line));
extern void TVPAddLog(const ttstr& line);
extern void TVPAddImportantLog(const ttstr& line);
extern ttstr TVPGetLastLog(tjs_uint n);
extern iTJSConsoleOutput* TVPGetTJS2ConsoleOutputGateway();
extern iTJSConsoleOutput* TVPGetTJS2DumpOutputGateway();
extern void TVPTJS2StartDump();
extern void TVPTJS2EndDump();
extern void TVPOnError();
extern ttstr TVPGetImportantLog();
extern void TVPSetLogLocation(const ttstr& loc);
extern void TVPStartLogToFile(bool clear);
extern void TVPAddLoggingHandler(tTJSVariantClosure clo);
extern void TVPRemoveLoggingHandler(tTJSVariantClosure clo);
//---------------------------------------------------------------------------

#endif
