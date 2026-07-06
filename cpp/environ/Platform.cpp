#include "tjsCommHead.h"
#include "Platform.h"

#include "TVPEvent.h"
#include "RenderManager.h"

void TVPExitApplication(int code)
{
    // clear some static data for memory leak detect
    TVPDeliverCompactEvent(TVP_COMPACT_LEVEL_MAX);
    if (!TVPIsSoftwareRenderManager())
        iTVPTexture2D::RecycleProcess();
    exit(code);
}