#include "ncbind/ncbind.hpp"

#define NCB_MODULE_NAME TJS_W("expat.dll")

//---------------------------------------------------------------------------
void expat_init()
{
    
}

void expat_done()
{
}

NCB_PRE_REGIST_CALLBACK(expat_init);
NCB_POST_UNREGIST_CALLBACK(expat_done);