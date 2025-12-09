//---------------------------------------------------------------------------
/*
	TVP2 ( T Visual Presenter 2 )  A script authoring tool
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Utilities for Debugging
//---------------------------------------------------------------------------
#include "tjsCommHead.h"
#include "TVPDebug.h"

#include "TVPSystem.h"
#include "TVPMsg.h"
#include "TVPStorage.h"

#include "Log.h"

#include <deque>

//---------------------------------------------------------------------------
// global variables
//---------------------------------------------------------------------------
struct tTVPLogItem
{
	ttstr Log;
	ttstr Time;
	tTVPLogItem(const ttstr& log, const ttstr& time)
	{
		Log = log;
		Time = time;
	}
};
static std::deque<tTVPLogItem>* TVPLogDeque = NULL;
tjs_uint TVPLogMaxLines = 2048;

bool TVPAutoLogToFileOnError = true;
bool TVPAutoClearLogOnError = false;
bool TVPLoggingToFile = false;
static tjs_uint TVPLogToFileRollBack = 100;
static ttstr* TVPImportantLogs = NULL;
ttstr TVPLogLocation;
ttstr TVPNativeLogLocation;
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
static bool TVPLogObjectsInitialized = false;
static void TVPEnsureLogObjects()
{
	if (TVPLogObjectsInitialized) return;
	TVPLogObjectsInitialized = true;

	TVPLogDeque = new std::deque<tTVPLogItem>();
	TVPImportantLogs = new ttstr();
}
//---------------------------------------------------------------------------
static void TVPDestroyLogObjects()
{
	if (TVPLogDeque) delete TVPLogDeque, TVPLogDeque = NULL;
	if (TVPImportantLogs) delete TVPImportantLogs, TVPImportantLogs = NULL;
}
//---------------------------------------------------------------------------
tTVPAtExit TVPDestroyLogObjectsAtExit
(TVP_ATEXIT_PRI_CLEANUP, TVPDestroyLogObjects);
//---------------------------------------------------------------------------




//---------------------------------------------------------------------------
void (*TVPOnLog)(const ttstr& line) = NULL;
// this function is invoked when a line is logged
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// TVPSetOnLog
//---------------------------------------------------------------------------
void TVPSetOnLog(void (*func)(const ttstr& line))
{
	TVPOnLog = func;
}
//---------------------------------------------------------------------------
std::vector<tTJSVariantClosure> TVPLoggingHandlerVector;

static void TVPCleanupLoggingHandlerVector()
{
	// eliminate empty
	std::vector<tTJSVariantClosure>::iterator i;
	for (i = TVPLoggingHandlerVector.begin();
		i != TVPLoggingHandlerVector.end();
		i++)
	{
		if (!i->Object)
		{
			i->Release();
			i = TVPLoggingHandlerVector.erase(i);
		}
		else
		{
			i++;
		}
	}
}
static void TVPDestroyLoggingHandlerVector()
{
	TVPSetOnLog(NULL);
	std::vector<tTJSVariantClosure>::iterator i;
	for (i = TVPLoggingHandlerVector.begin();
		i != TVPLoggingHandlerVector.end();
		i++)
	{
		i->Release();
	}
	TVPLoggingHandlerVector.clear();
}

static tTVPAtExit TVPDestroyLoggingHandlerAtExit
(TVP_ATEXIT_PRI_PREPARE, TVPDestroyLoggingHandlerVector);
//---------------------------------------------------------------------------
static bool TVPInDeliverLoggingEvent = false;
static void _TVPDeliverLoggingEvent(const ttstr& line) // internal
{
	if (!TVPInDeliverLoggingEvent)
	{
		TVPInDeliverLoggingEvent = true;
		try
		{
			if (TVPLoggingHandlerVector.size())
			{
				bool emptyflag = false;
				tTJSVariant vline(line);
				tTJSVariant* pvline[] = { &vline };
				for (tjs_uint i = 0; i < TVPLoggingHandlerVector.size(); i++)
				{
					if (TVPLoggingHandlerVector[i].Object)
					{
						tjs_error er;
						try
						{
							er =
								TVPLoggingHandlerVector[i].FuncCall(0, NULL, NULL, NULL, 1, pvline, NULL);
						}
						catch (...)
						{
							// failed
							TVPLoggingHandlerVector[i].Release();
							TVPLoggingHandlerVector[i].Object =
								TVPLoggingHandlerVector[i].ObjThis = NULL;
							throw;
						}
						if (TJS_FAILED(er))
						{
							// failed
							TVPLoggingHandlerVector[i].Release();
							TVPLoggingHandlerVector[i].Object =
								TVPLoggingHandlerVector[i].ObjThis = NULL;
							emptyflag = true;
						}
					}
					else
					{
						emptyflag = true;
					}
				}

				if (emptyflag)
				{
					// the array has empty cell
					TVPCleanupLoggingHandlerVector();
				}
			}

			if (!TVPLoggingHandlerVector.size())
			{
				TVPSetOnLog(NULL);
			}
		}
		catch (...)
		{
			TVPInDeliverLoggingEvent = false;
			throw;
		}
		TVPInDeliverLoggingEvent = false;
	}
}
//---------------------------------------------------------------------------
void TVPAddLoggingHandler(tTJSVariantClosure clo)
{
	std::vector<tTJSVariantClosure>::iterator i;
	i = std::find(TVPLoggingHandlerVector.begin(),
		TVPLoggingHandlerVector.end(), clo);
	if (i == TVPLoggingHandlerVector.end())
	{
		clo.AddRef();
		TVPLoggingHandlerVector.push_back(clo);
		TVPSetOnLog(&_TVPDeliverLoggingEvent);
	}
}
//---------------------------------------------------------------------------
void TVPRemoveLoggingHandler(tTJSVariantClosure clo)
{
	std::vector<tTJSVariantClosure>::iterator i;
	i = std::find(TVPLoggingHandlerVector.begin(),
		TVPLoggingHandlerVector.end(), clo);
	if (i != TVPLoggingHandlerVector.end())
	{
		i->Release();
		i->Object = i->ObjThis = NULL;
	}

	if (!TVPInDeliverLoggingEvent)
	{
		TVPCleanupLoggingHandlerVector();
		if (!TVPLoggingHandlerVector.size())
		{
			TVPSetOnLog(NULL);
		}
	}
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// log stream holder
//---------------------------------------------------------------------------
class tTVPLogStreamHolder
{
	FILE* Stream;
	bool Alive;
	bool OpenFailed;

public:
	tTVPLogStreamHolder() { Stream = NULL; Alive = true; OpenFailed = false; }
	~tTVPLogStreamHolder() { if (Stream) fclose(Stream); Alive = false; }

private:
	void Open(const tjs_nchar* mode);

public:
	void Clear(); // clear log stream
	void Log(const ttstr& text); // log given text

	void Reopen() { if (Stream) fclose(Stream); Stream = NULL; Alive = false; OpenFailed = false; } // reopen log stream

} static TVPLogStreamHolder;
//---------------------------------------------------------------------------
void tTVPLogStreamHolder::Open(const tjs_nchar* mode)
{
	if (OpenFailed) return; // no more try

	try
	{
		ttstr filename;
		if (TVPLogLocation.GetLen() == 0)
		{
			Stream = NULL;
			OpenFailed = true;
		}
		else
		{
			// no log location specified
			filename = TVPNativeLogLocation + TJS_W("/krkr.console.log");
			TVPEnsureDataPathDirectory();
			std::string _filename = filename.AsNarrowStdString();
			Stream = fopen(_filename.c_str(), mode);
			if (!Stream) OpenFailed = true;
		}

		if (Stream)
		{
			fseek(Stream, 0, SEEK_END);
			if (ftell(Stream) == 0)
			{
				// write BOM
				// TODO: 32-bit unicode support
				fwrite(TJS_N("\xff\xfe"), 1, 2, Stream); // indicate unicode text
			}

#ifdef TJS_TEXT_OUT_CRLF
			ttstr separator(TVPSeparatorCRLF);
#else
			ttstr separator(TVPSeparatorCR);
#endif
			Log(separator);


			static tjs_char timebuf[80];

			tm* struct_tm;
			time_t timer;
			timer = time(&timer);

			struct_tm = localtime(&timer);
			TJS_strftime(timebuf, 79, TJS_W("%#c"), struct_tm);

			Log(ttstr(TJS_W("Logging to ")) + ttstr(filename) + TJS_W(" started on ") + timebuf);

		}
	}
	catch (...)
	{
		OpenFailed = true;
	}
}
//---------------------------------------------------------------------------
void tTVPLogStreamHolder::Clear()
{
	// clear log text
	if (Stream) fclose(Stream);

	Open(TJS_N("wb"));
}
//---------------------------------------------------------------------------
void tTVPLogStreamHolder::Log(const ttstr& text)
{
	if (!Stream) Open(TJS_N("ab"));

	try
	{
		if (Stream)
		{
			size_t len = text.GetLen() * sizeof(tjs_char);
			if (len != fwrite(text.c_str(), 1, len, Stream))
			{
				// cannot write
				fclose(Stream);
				OpenFailed = true;
				return;
			}
#ifdef TJS_TEXT_OUT_CRLF
			fwrite(TJS_W("\r\n"), 1, 2 * sizeof(tjs_char), Stream);
#else
			fwrite(TJS_W("\n"), 1, 1 * sizeof(tjs_char), Stream);
#endif

			// flush
			fflush(Stream);
		}
	}
	catch (...)
	{
		try
		{
			if (Stream) fclose(Stream);
		}
		catch (...)
		{
		}

		OpenFailed = true;
	}
}
//---------------------------------------------------------------------------







//---------------------------------------------------------------------------
// TVPAddLog
//---------------------------------------------------------------------------
void TVPAddLog(const ttstr& line, bool appendtoimportant)
{
	// add a line to the log.
	// exceeded lines over TVPLogMaxLines are eliminated.
	// this function is not thread-safe ...

	TVPEnsureLogObjects();
	if (!TVPLogDeque) return; // log system is shuttingdown
	if (!TVPImportantLogs) return; // log system is shuttingdown

	static time_t prevlogtime = 0;
	static ttstr prevtimebuf;
	static tjs_char timebuf[40];

	tm* struct_tm;
	time_t timer;
	timer = time(&timer);

	if (prevlogtime != timer)
	{
		struct_tm = localtime(&timer);
		TJS_strftime(timebuf, 39, TJS_W("%H:%M:%S"), struct_tm);
		prevlogtime = timer;
		prevtimebuf = timebuf;
	}

	TVPLogDeque->push_back(tTVPLogItem(line, prevtimebuf));

	if (appendtoimportant)
	{
#ifdef TJS_TEXT_OUT_CRLF
		* TVPImportantLogs += ttstr(timebuf) + TJS_W(" ! ") + line + TJS_W("\r\n");
#else
		* TVPImportantLogs += ttstr(timebuf) + TJS_W(" ! ") + line + TJS_W("\n");
#endif
	}
	while (TVPLogDeque->size() >= TVPLogMaxLines + 100)
	{
		std::deque<tTVPLogItem>::iterator i = TVPLogDeque->begin();
		TVPLogDeque->erase(i, i + 100);
	}

	tjs_int timebuflen = (tjs_int)TJS_strlen(timebuf);
	ttstr buf((tTJSStringBufferLength)(timebuflen + 1 + line.GetLen()));
	tjs_char* p = buf.Independ();
	TJS_strcpy(p, timebuf);
	p += timebuflen;
	*p = TJS_W(' ');
	p++;
	TJS_strcpy(p, line.c_str());
	if (TVPOnLog) TVPOnLog(buf);
#ifdef ENABLE_DEBUGGER
	if (TJSEnableDebugMode) TJSDebuggerLog(buf, appendtoimportant);
	//OutputDebugStringW( buf.c_str() );
	//OutputDebugStringW( L"\n" );
#endif	// ENABLE_DEBUGGER

	TVPConsoleLog(buf, appendtoimportant);

	if (TVPLoggingToFile) TVPLogStreamHolder.Log(buf);
}
//---------------------------------------------------------------------------
void TVPAddLog(const ttstr& line)
{
	TVPAddLog(line, false);
}
//---------------------------------------------------------------------------
void TVPAddImportantLog(const ttstr& line)
{
	TVPAddLog(line, true);
}
//---------------------------------------------------------------------------
ttstr TVPGetImportantLog()
{
	if (!TVPImportantLogs) return ttstr();
	return *TVPImportantLogs;
}
//---------------------------------------------------------------------------




//---------------------------------------------------------------------------
// TVPGetLastLog : get last n lines of the log ( each line is spearated with '\n'/'\r\n' )
//---------------------------------------------------------------------------
ttstr TVPGetLastLog(tjs_uint n)
{
	TVPEnsureLogObjects();
	if (!TVPLogDeque) return TJS_W(""); // log system is shuttingdown

	tjs_uint len = 0;
	tjs_uint size = (tjs_uint)TVPLogDeque->size();
	if (n > size) n = size;
	if (n == 0) return ttstr();
	std::deque<tTVPLogItem>::iterator i = TVPLogDeque->end();
	i -= n;
	tjs_uint c;
	for (c = 0; c < n; c++, i++)
	{
#ifdef TJS_TEXT_OUT_CRLF
		len += i->Time.GetLen() + 1 + i->Log.GetLen() + 2;
#else
		len += i->Time.GetLen() + 1 + i->Log.GetLen() + 1;
#endif
	}

	ttstr buf((tTJSStringBufferLength)len);
	tjs_char* p = buf.Independ();

	i = TVPLogDeque->end();
	i -= n;
	for (c = 0; c < n; c++)
	{
		TJS_strcpy(p, i->Time.c_str());
		p += i->Time.GetLen();
		*p = TJS_W(' ');
		p++;
		TJS_strcpy(p, i->Log.c_str());
		p += i->Log.GetLen();
#ifdef TJS_TEXT_OUT_CRLF
		* p = TJS_W('\r');
		p++;
		*p = TJS_W('\n');
		p++;
#else
		* p = TJS_W('\n');
		p++;
#endif
		i++;
	}
	return buf;
}
//---------------------------------------------------------------------------




//---------------------------------------------------------------------------
// TVPStartLogToFile
//---------------------------------------------------------------------------
void TVPStartLogToFile(bool clear)
{
	TVPEnsureLogObjects();
	if (!TVPImportantLogs) return; // log system is shuttingdown

	if (TVPLoggingToFile) return; // already logging
	if (clear) TVPLogStreamHolder.Clear();

	// log last lines

	TVPLogStreamHolder.Log(*TVPImportantLogs);

#ifdef TJS_TEXT_OUT_CRLF
	ttstr separator(TJS_W("\r\n")
		TJS_W("------------------------------------------------------------------------------\r\n"
		));
#else
	ttstr separator(TJS_W("\n")
		TJS_W("------------------------------------------------------------------------------\n"
		));
#endif

	TVPLogStreamHolder.Log(separator);

	ttstr content = TVPGetLastLog(TVPLogToFileRollBack);
	TVPLogStreamHolder.Log(content);

	//
	TVPLoggingToFile = true;
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// TVPOnError
//---------------------------------------------------------------------------
void TVPOnError()
{
	if (TVPAutoLogToFileOnError) TVPStartLogToFile(TVPAutoClearLogOnError);
	// TVPOnErrorHook();
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// TVPSetLogLocation
//---------------------------------------------------------------------------
void TVPSetLogLocation(const ttstr& loc)
{
	TVPLogLocation = TVPNormalizeStorageName(loc);

	ttstr native = TVPGetLocallyAccessibleName(TVPLogLocation);
	if (native.IsEmpty())
	{
		TVPNativeLogLocation.Clear();
		TVPLogLocation.Clear();
	}
	else
	{
		TVPNativeLogLocation = native;
		if (TVPNativeLogLocation[TVPNativeLogLocation.length() - 1] != TJS_W('/'))
			TVPNativeLogLocation += TJS_W("/");
	}

	TVPLogStreamHolder.Reopen();

	// check force logging option
	tTJSVariant val;
	if (TVPGetCommandLine(TJS_W("-forcelog"), &val))
	{
		ttstr str(val);
		if (str == TJS_W("yes"))
		{
			TVPLoggingToFile = false;
			TVPStartLogToFile(false);
		}
		else if (str == TJS_W("clear"))
		{
			TVPLoggingToFile = false;
			TVPStartLogToFile(true);
		}
	}
	if (TVPGetCommandLine(TJS_W("-logerror"), &val))
	{
		ttstr str(val);
		if (str == TJS_W("no"))
		{
			TVPAutoClearLogOnError = false;
			TVPAutoLogToFileOnError = false;
		}
		else if (str == TJS_W("clear"))
		{
			TVPAutoClearLogOnError = true;
			TVPAutoLogToFileOnError = true;
		}
	}
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TJS2 Console Output Gateway
//---------------------------------------------------------------------------
class tTVPTJS2ConsoleOutputGateway : public iTJSConsoleOutput
{
	void ExceptionPrint(const tjs_char* msg)
	{
		TVPAddLog(msg);
	}

	void Print(const tjs_char* msg)
	{
		TVPAddLog(msg);
	}
} static TVPTJS2ConsoleOutputGateway;
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// TJS2 Dump Output Gateway
//---------------------------------------------------------------------------
static ttstr TVPDumpOutFileName;
static FILE* TVPDumpOutFile = NULL; // use traditional output routine
//---------------------------------------------------------------------------
class tTVPTJS2DumpOutputGateway : public iTJSConsoleOutput
{
	void ExceptionPrint(const tjs_char* msg) { Print(msg); }

	void Print(const tjs_char* msg)
	{
		if (TVPDumpOutFile)
		{
			fwrite(msg, 1, TJS_strlen(msg) * sizeof(tjs_char), TVPDumpOutFile);
#ifdef TJS_TEXT_OUT_CRLF
			fwrite(TJS_W("\r\n"), 1, 2 * sizeof(tjs_char), TVPDumpOutFile);
#else
			fwrite(TJS_W("\n"), 1, 1 * sizeof(tjs_char), TVPDumpOutFile);
#endif
		}
	}
} static TVPTJS2DumpOutputGateway;
//---------------------------------------------------------------------------
void TVPTJS2StartDump()
{
}
//---------------------------------------------------------------------------
void TVPTJS2EndDump()
{
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// console interface retrieving functions
//---------------------------------------------------------------------------
iTJSConsoleOutput* TVPGetTJS2ConsoleOutputGateway()
{
	return &TVPTJS2ConsoleOutputGateway;
}
//---------------------------------------------------------------------------
iTJSConsoleOutput* TVPGetTJS2DumpOutputGateway()
{
	return &TVPTJS2DumpOutputGateway;
}
//---------------------------------------------------------------------------