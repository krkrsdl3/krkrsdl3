//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// safe 64bit System Tick Count
//---------------------------------------------------------------------------
#ifndef TickCountH
#define TickCountH

//---------------------------------------------------------------------------
tjs_uint64 TVPGetTickCount();
void TVPStartTickCount();
// this must be called before TVPGetTickCount(), in main thread.
// this function can be called more than one time.
//---------------------------------------------------------------------------

#endif
