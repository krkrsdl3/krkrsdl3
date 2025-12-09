//---------------------------------------------------------------------------
/*
	TVP2 ( T Visual Presenter 2 )  A script authoring tool
	Copyright (C) 2000-2007 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// CPU idetification / features detection routine
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include "cpu_types.h"
#include "TVPDebug.h"
#include "TVPSystem.h"

#include "TVPThread.h"
#include "Exception.h"

/*
	Note: CPU clock measuring routine is in EmergencyExit.cpp, reusing
	hot-key watching thread.
*/

//---------------------------------------------------------------------------
extern "C"
{
	tjs_uint32 TVPCPUType = 0; // CPU type
    tjs_uint32 TVPCPUFeatures	=	0;
}

static bool TVPCPUChecked = false;
//---------------------------------------------------------------------------





//---------------------------------------------------------------------------
// TVPGetCPUTypeForOne
//---------------------------------------------------------------------------
static void TVPGetCPUTypeForOne()
{
	try
	{
        TVPCPUFeatures = 0;

		//TVPCheckCPU(); // in detect_cpu.nas
	}
	catch(.../*EXCEPTION_EXECUTE_HANDLER*/)
	{
		// exception had been ocured
		throw Exception("CPU check failure.");
	}
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
static ttstr TVPDumpCPUInfo(tjs_int cpu_num)
{
	// dump detected cpu type
	ttstr features(TJS_W("(info) CPU #") + ttstr(cpu_num) +
		TJS_W(" : "));

	tjs_uint32 vendor = TVPCPUFeatures & TVP_CPU_VENDOR_MASK;

	TVPAddImportantLog(features);

	return features;
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
void TVPDetectCPU()
{
    if(TVPCPUChecked) return;
    TVPCPUChecked = true;
#if defined(__ANDROID__) || defined(WIN32)
    TVPCPUFeatures |= TVP_CPU_FAMILY_ARM; // must be arm
#if defined(__arm64__) || defined(__aarch64__) || defined(__LP64__) || defined(WIN32)
	TVPCPUFeatures |= TVP_CPU_HAS_NEON; // aka. asimd
#else
    if((android_getCpuFeatures() & ANDROID_CPU_ARM_FEATURE_NEON) != 0) {
        TVPCPUFeatures |= TVP_CPU_HAS_NEON;
    }
#endif
#endif
#ifdef __APPLE__
    // must be iOS
    TVPCPUFeatures |= TVP_CPU_FAMILY_ARM | TVP_CPU_HAS_NEON;
#endif

    tjs_uint32 features = 0;
    features =  (TVPCPUFeatures & TVP_CPU_FEATURE_MASK);
    TVPCPUType &= ~ TVP_CPU_FEATURE_MASK;
    TVPCPUType |= features;
}
//---------------------------------------------------------------------------





//---------------------------------------------------------------------------
// jpeg and png loader support functions
//---------------------------------------------------------------------------
unsigned long MMXReady = 0;
extern "C"
{
	void CheckMMX(void)
	{
		TVPDetectCPU();
		MMXReady = TVPCPUType & TVP_CPU_HAS_MMX;
	}
}
//---------------------------------------------------------------------------





//---------------------------------------------------------------------------
// TVPGetCPUType
//---------------------------------------------------------------------------
tjs_uint32 TVPGetCPUType()
{
	TVPDetectCPU();
	return TVPCPUType;
}
//---------------------------------------------------------------------------


