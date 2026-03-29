/**
 * layerExDraw.cpp  -  GdiPlus-compatible software drawing plugin for Kirikiroid2
 *
 * Provides the GdiPlus namespace used by many KiriKiri2 games for environment
 * effects (rain, snow, fog, etc.) and general layer drawing.  All rendering
 * is done in software directly on the layer ARGB pixel buffer - no Win32/GDI+
 * dependency required, works on Android and HarmonyOS.
 *
 * Exposed TJS2 API (matching layerExDraw.dll v1.x conventions):
 *   GdiPlus.Appearance  - drawing style / text appearance descriptor
 *   GdiPlus.Pen         - stroke style  (color, width)
 *   GdiPlus.SolidBrush  - fill style    (color)
 *   GdiPlus.Graphics    - drawing surface wrapping a Layer object
 */

#include "ncbind/ncbind.hpp"
#include "LayerExBase.h"

#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

#define NCB_MODULE_NAME "layerExDraw.dll"

// ARGB pixel helpers
static inline int    _A(uint32_t c) { return (c >> 24) & 0xFF; }
static inline int    _R(uint32_t c) { return (c >> 16) & 0xFF; }
static inline int    _G(uint32_t c) { return (c >>  8) & 0xFF; }
static inline int    _B(uint32_t c) { return  c        & 0xFF; }
static inline uint32_t mkARGB(int a,int r,int g,int b){
    a=std::max(0,std::min(255,a));r=std::max(0,std::min(255,r));
    g=std::max(0,std::min(255,g));b=std::max(0,std::min(255,b));
    return ((uint32_t)a<<24)|((uint32_t)r<<16)|((uint32_t)g<<8)|(uint32_t)b;}
static inline uint32_t blendOver(uint32_t dst,uint32_t src){
    int sa=_A(src);if(sa==0)return dst;if(sa==255)return src;
    int da=_A(dst),inv=255-sa,oa=sa+(da*inv+127)/255;if(oa==0)return 0;
    int or_=(_R(src)*sa+_R(dst)*da*inv/255+127)/oa;
    int og =(_G(src)*sa+_G(dst)*da*inv/255+127)/oa;
    int ob =(_B(src)*sa+_B(dst)*da*inv/255+127)/oa;
    return mkARGB(oa,or_,og,ob);}

// GdiPlusAppearance
class GdiPlusAppearance{
public:
    tjs_int color;bool shadow;tjs_int shadowColor;
    tjs_int shadowOffsetX,shadowOffsetY;tTVReal shadowBlur;
    bool antiAlias;bool stroke;tjs_int strokeColor;tTVReal strokeWidth;
    GdiPlusAppearance():color(0xFFFFFFFF),shadow(false),shadowColor(0x80000000),
        shadowOffsetX(1),shadowOffsetY(1),shadowBlur(0.0),antiAlias(true),
        stroke(false),strokeColor(0xFF000000),strokeWidth(1.0){}
    tjs_int getColor()const{return color;} void setColor(tjs_int v){color=v;}
    bool getShadow()const{return shadow;} void setShadow(bool v){shadow=v;}
    tjs_int getShadowColor()const{return shadowColor;} void setShadowColor(tjs_int v){shadowColor=v;}
    tjs_int getShadowOffsetX()const{return shadowOffsetX;} void setShadowOffsetX(tjs_int v){shadowOffsetX=v;}
    tjs_int getShadowOffsetY()const{return shadowOffsetY;} void setShadowOffsetY(tjs_int v){shadowOffsetY=v;}
    tTVReal getShadowBlur()const{return shadowBlur;} void setShadowBlur(tTVReal v){shadowBlur=v;}
    bool getAntiAlias()const{return antiAlias;} void setAntiAlias(bool v){antiAlias=v;}
    bool getStroke()const{return stroke;} void setStroke(bool v){stroke=v;}
    tjs_int getStrokeColor()const{return strokeColor;} void setStrokeColor(tjs_int v){strokeColor=v;}
    tTVReal getStrokeWidth()const{return strokeWidth;} void setStrokeWidth(tTVReal v){strokeWidth=v;}
};
NCB_REGISTER_CLASS(GdiPlusAppearance){
    NCB_CONSTRUCTOR(());
    NCB_PROPERTY(color,getColor,setColor);NCB_PROPERTY(shadow,getShadow,setShadow);
    NCB_PROPERTY(shadowColor,getShadowColor,setShadowColor);
    NCB_PROPERTY(shadowOffsetX,getShadowOffsetX,setShadowOffsetX);
    NCB_PROPERTY(shadowOffsetY,getShadowOffsetY,setShadowOffsetY);
    NCB_PROPERTY(shadowBlur,getShadowBlur,setShadowBlur);
    NCB_PROPERTY(antiAlias,getAntiAlias,setAntiAlias);
    NCB_PROPERTY(stroke,getStroke,setStroke);
    NCB_PROPERTY(strokeColor,getStrokeColor,setStrokeColor);
    NCB_PROPERTY(strokeWidth,getStrokeWidth,setStrokeWidth);
};

// GdiPlusPen
class GdiPlusPen{
public:
    tjs_int color;tTVReal width;tjs_int dashStyle;
    GdiPlusPen(tjs_int c=0xFF000000,tTVReal w=1.0):color(c),width(w),dashStyle(0){}
    tjs_int getColor()const{return color;} void setColor(tjs_int v){color=v;}
    tTVReal getWidth()const{return width;} void setWidth(tTVReal v){width=v;}
    tjs_int getDashStyle()const{return dashStyle;} void setDashStyle(tjs_int v){dashStyle=v;}
};
NCB_REGISTER_CLASS(GdiPlusPen){
    NCB_CONSTRUCTOR((tjs_int));
    NCB_PROPERTY(color,getColor,setColor);NCB_PROPERTY(width,getWidth,setWidth);
    NCB_PROPERTY(dashStyle,getDashStyle,setDashStyle);
};

// GdiPlusSolidBrush
class GdiPlusSolidBrush{
public:
    tjs_int color;
    GdiPlusSolidBrush(tjs_int c=0xFF000000):color(c){}
    tjs_int getColor()const{return color;} void setColor(tjs_int v){color=v;}
};
NCB_REGISTER_CLASS(GdiPlusSolidBrush){
    NCB_CONSTRUCTOR((tjs_int));NCB_PROPERTY(color,getColor,setColor);
};

// Helper: get native instance by class name
template<class T>
static T* getNative(iTJSDispatch2 *obj,const tjs_char *cls){
    if(!obj)return nullptr;tjs_int id=TJSFindNativeClassID(cls);if(id<0)return nullptr;
    T*ptr=nullptr;obj->NativeInstanceSupport(TJS_NIS_GETINSTANCE,id,
        reinterpret_cast<iTJSNativeInstance**>(&ptr));return ptr;}

// GdiPlusGraphics
// NOTE: _buf = _buffer (visual row 0); _sp = signed pitch (row stride, may be negative)
class GdiPlusGraphics{
    NI_LayerExBase *_ln;iTJSDispatch2 *_lo;
    int _w,_h,_sp;unsigned char*_buf;  // _sp = signed pitch
    void sync(){
        if(!_ln||!_lo){_buf=nullptr;return;}_ln->reset(_lo);
        _w=_ln->_width;_h=_ln->_height;
        _sp=_ln->_pitch;   // keep signed: row y is at _buf + y*_sp
        _buf=_ln->_buffer; // already points to visual row 0
    }
    inline bool ok(int x,int y)const{return _buf&&x>=0&&y>=0&&x<_w&&y<_h;}
    inline uint32_t& pix(int x,int y){
        return*(reinterpret_cast<uint32_t*>(_buf+(ptrdiff_t)y*_sp)+x);}
    void put(int x,int y,uint32_t c){if(!ok(x,y))return;pix(x,y)=blendOver(pix(x,y),c);}
    void line(int x0,int y0,int x1,int y1,uint32_t c){
        int dx=std::abs(x1-x0),dy=std::abs(y1-y0),sx=x0<x1?1:-1,sy=y0<y1?1:-1,err=dx-dy;
        while(true){put(x0,y0,c);if(x0==x1&&y0==y1)break;
            int e2=2*err;if(e2>-dy){err-=dy;x0+=sx;}if(e2<dx){err+=dx;y0+=sy;}}}
    void fillRect(int x,int y,int w,int h,uint32_t c){
        sync();if(!_buf)return;
        int x2=std::min(x+w,_w),y2=std::min(y+h,_h);x=std::max(x,0);y=std::max(y,0);
        for(int r=y;r<y2;r++){uint32_t*p=reinterpret_cast<uint32_t*>(_buf+(ptrdiff_t)r*_sp)+x;
            for(int c2=x;c2<x2;c2++,p++)*p=blendOver(*p,c);}}
    void ellipse(int x,int y,int w,int h,uint32_t c,bool fill){
        sync();if(!_buf||w<=0||h<=0)return;
        float cx2=x+w*.5f,cy2=y+h*.5f,rx=w*.5f,ry=h*.5f;
        if(fill){for(int py=y;py<y+h;py++){float dy2=(py-cy2)/ry,dx2=sqrtf(std::max(0.f,1.f-dy2*dy2))*rx;
            for(int px=(int)ceilf(cx2-dx2);px<=(int)floorf(cx2+dx2);px++)put(px,py,c);}}
        else{long long rx2=(long long)rx*(long long)rx,ry2=(long long)ry*(long long)ry;
            int px0=0,py0=(int)ry;long long d=ry2-rx2*(long long)ry+(long long)(rx2/4);
            long long ddx=0,ddy=2*rx2*py0;
            auto pp=[&](int a,int b){int icx=(int)cx2,icy=(int)cy2;
                put(icx+a,icy+b,c);put(icx-a,icy+b,c);put(icx+a,icy-b,c);put(icx-a,icy-b,c);};
            while(ddx<ddy){pp(px0,py0);px0++;ddx+=2*ry2;if(d<0)d+=ddx+ry2;else{py0--;ddy-=2*rx2;d+=ddx-ddy+ry2;}}
            long long d2=ry2*(long long)((int)(px0+.5f))*(int)(px0+.5f)+rx2*(long long)(py0-1)*(py0-1)-rx2*ry2;
            while(py0>=0){pp(px0,py0);py0--;ddy-=2*rx2;if(d2>0)d2+=rx2-ddy;else{px0++;ddx+=2*ry2;d2+=ddx-ddy+rx2;}}}}
    static uint32_t colFrom(tTJSVariant*v,tTVReal*lw=nullptr){
        if(v->Type()==tvtObject){iTJSDispatch2*o=v->AsObjectNoAddRef();
            GdiPlusPen*pen=getNative<GdiPlusPen>(o,"GdiPlusPen");
            if(pen){if(lw)*lw=pen->width;return(uint32_t)pen->color;}
            GdiPlusSolidBrush*br=getNative<GdiPlusSolidBrush>(o,"GdiPlusSolidBrush");
            if(br)return(uint32_t)br->color;
            GdiPlusAppearance*ap=getNative<GdiPlusAppearance>(o,"GdiPlusAppearance");
            if(ap)return(uint32_t)ap->color;}
        return(uint32_t)(tjs_int)*v;}
    static GdiPlusGraphics*me(iTJSDispatch2*th){
        return getNative<GdiPlusGraphics>(th,"GdiPlusGraphics");}
public:
    GdiPlusGraphics():_ln(nullptr),_lo(nullptr),_w(0),_h(0),_sp(0),_buf(nullptr){}
    GdiPlusGraphics(tTJSVariant lv):_ln(nullptr),_lo(nullptr),_w(0),_h(0),_sp(0),_buf(nullptr){
        _lo=lv.AsObjectNoAddRef();if(_lo){_ln=NI_LayerExBase::getNative(_lo,true);sync();}}
    tjs_int getWidth()const{return _w;} tjs_int getHeight()const{return _h;}
    void clear(tjs_int c=0){sync();if(!_buf)return;
        for(int r=0;r<_h;r++){uint32_t*p=reinterpret_cast<uint32_t*>(_buf+(ptrdiff_t)r*_sp);
            for(int cc=0;cc<_w;cc++)p[cc]=(uint32_t)c;}}
    static tjs_error TJS_INTF_METHOD cb_drawLine(tTJSVariant*res,tjs_int n,tTJSVariant**p,iTJSDispatch2*th){
        if(n<5)return TJS_E_BADPARAMCOUNT;GdiPlusGraphics*s=me(th);if(!s)return TJS_E_NATIVECLASSCRASH;
        s->sync();tTVReal lw=1;uint32_t c=colFrom(p[4],&lw);
        s->line((tjs_int)*p[0],(tjs_int)*p[1],(tjs_int)*p[2],(tjs_int)*p[3],c);return TJS_S_OK;}
    static tjs_error TJS_INTF_METHOD cb_fillRect(tTJSVariant*res,tjs_int n,tTJSVariant**p,iTJSDispatch2*th){
        if(n<5)return TJS_E_BADPARAMCOUNT;GdiPlusGraphics*s=me(th);if(!s)return TJS_E_NATIVECLASSCRASH;
        s->fillRect((tjs_int)*p[0],(tjs_int)*p[1],(tjs_int)*p[2],(tjs_int)*p[3],colFrom(p[4]));return TJS_S_OK;}
    static tjs_error TJS_INTF_METHOD cb_drawRect(tTJSVariant*res,tjs_int n,tTJSVariant**p,iTJSDispatch2*th){
        if(n<5)return TJS_E_BADPARAMCOUNT;GdiPlusGraphics*s=me(th);if(!s)return TJS_E_NATIVECLASSCRASH;
        s->sync();uint32_t c=colFrom(p[4]);int x=(tjs_int)*p[0],y=(tjs_int)*p[1],w=(tjs_int)*p[2],h=(tjs_int)*p[3];
        s->line(x,y,x+w-1,y,c);s->line(x,y,x,y+h-1,c);s->line(x+w-1,y,x+w-1,y+h-1,c);s->line(x,y+h-1,x+w-1,y+h-1,c);return TJS_S_OK;}
    static tjs_error TJS_INTF_METHOD cb_fillEllipse(tTJSVariant*res,tjs_int n,tTJSVariant**p,iTJSDispatch2*th){
        if(n<5)return TJS_E_BADPARAMCOUNT;GdiPlusGraphics*s=me(th);if(!s)return TJS_E_NATIVECLASSCRASH;
        s->ellipse((tjs_int)*p[0],(tjs_int)*p[1],(tjs_int)*p[2],(tjs_int)*p[3],colFrom(p[4]),true);return TJS_S_OK;}
    static tjs_error TJS_INTF_METHOD cb_drawEllipse(tTJSVariant*res,tjs_int n,tTJSVariant**p,iTJSDispatch2*th){
        if(n<5)return TJS_E_BADPARAMCOUNT;GdiPlusGraphics*s=me(th);if(!s)return TJS_E_NATIVECLASSCRASH;
        s->ellipse((tjs_int)*p[0],(tjs_int)*p[1],(tjs_int)*p[2],(tjs_int)*p[3],colFrom(p[4]),false);return TJS_S_OK;}
    static tjs_error TJS_INTF_METHOD cb_drawImage(tTJSVariant*res,tjs_int n,tTJSVariant**p,iTJSDispatch2*th){
        if(n<3)return TJS_E_BADPARAMCOUNT;GdiPlusGraphics*s=me(th);if(!s)return TJS_E_NATIVECLASSCRASH;
        s->sync();if(!s->_buf)return TJS_S_OK;
        iTJSDispatch2*so=p[0]->AsObjectNoAddRef();if(!so)return TJS_S_OK;
        NI_LayerExBase*src=NI_LayerExBase::getNative(so,false);if(!src)return TJS_S_OK;src->reset(so);
        int dx=(tjs_int)*p[1],dy=(tjs_int)*p[2];
        int dw=(n>=5)?(tjs_int)*p[3]:src->_width,dh=(n>=5)?(tjs_int)*p[4]:src->_height;
        int sx2=(n>=7)?(tjs_int)*p[5]:0,sy2=(n>=7)?(tjs_int)*p[6]:0;
        int sw=(n>=9)?(tjs_int)*p[7]:src->_width,sh=(n>=9)?(tjs_int)*p[8]:src->_height;
        int opa=(n>=10)?(tjs_int)*p[9]:(n==4?(tjs_int)*p[3]:255);float opaf=opa/255.f;
        // NI_LayerExBase::_buffer already points to visual row 0; use signed pitch directly
        int ssp=src->_pitch; // signed pitch for source
        unsigned char*sb=src->_buffer;
        if(!sb)return TJS_S_OK;
        for(int r=0;r<dh;r++){
            int sRow=sy2+(sh*r/dh);if(sRow<0||sRow>=src->_height)continue;
            int dRow=dy+r;if(dRow<0||dRow>=s->_h)continue;
            uint32_t*dL=reinterpret_cast<uint32_t*>(s->_buf+(ptrdiff_t)dRow*s->_sp);
            uint32_t*sL=reinterpret_cast<uint32_t*>(sb+(ptrdiff_t)sRow*ssp);
            for(int c2=0;c2<dw;c2++){int sCol=sx2+(sw*c2/dw);if(sCol<0||sCol>=src->_width)continue;
                int dCol=dx+c2;if(dCol<0||dCol>=s->_w)continue;
                uint32_t sp=sL[sCol];if(opaf<1.f)sp=(sp&0x00FFFFFF)|((uint32_t)(int)(_A(sp)*opaf)<<24);
                dL[dCol]=blendOver(dL[dCol],sp);}}
        return TJS_S_OK;}
    static tjs_error TJS_INTF_METHOD cb_drawString(tTJSVariant*,tjs_int,tTJSVariant**,iTJSDispatch2*){return TJS_S_OK;}
    static tjs_error TJS_INTF_METHOD cb_update(tTJSVariant*,tjs_int,tTJSVariant**,iTJSDispatch2*th){
        GdiPlusGraphics*s=me(th);if(!s)return TJS_E_NATIVECLASSCRASH;
        if(s->_ln&&s->_lo)s->_ln->redraw(s->_lo);return TJS_S_OK;}
};
NCB_REGISTER_CLASS(GdiPlusGraphics){
    NCB_CONSTRUCTOR((tTJSVariant));
    NCB_PROPERTY_RO(width,getWidth);NCB_PROPERTY_RO(height,getHeight);
    NCB_METHOD(clear);
    NCB_METHOD_RAW_CALLBACK(drawLine,   GdiPlusGraphics::cb_drawLine,   0);
    NCB_METHOD_RAW_CALLBACK(fillRect,   GdiPlusGraphics::cb_fillRect,   0);
    NCB_METHOD_RAW_CALLBACK(drawRect,   GdiPlusGraphics::cb_drawRect,   0);
    NCB_METHOD_RAW_CALLBACK(fillEllipse,GdiPlusGraphics::cb_fillEllipse,0);
    NCB_METHOD_RAW_CALLBACK(drawEllipse,GdiPlusGraphics::cb_drawEllipse,0);
    NCB_METHOD_RAW_CALLBACK(drawImage,  GdiPlusGraphics::cb_drawImage,  0);
    NCB_METHOD_RAW_CALLBACK(drawString, GdiPlusGraphics::cb_drawString, 0);
    NCB_METHOD_RAW_CALLBACK(update,     GdiPlusGraphics::cb_update,     0);
};

// Register GdiPlus namespace — use pure C++ dispatch API, no TVPExecuteExpression
// (NCB_POST_REGIST_CALLBACK fires during AllRegist before TJS global "Object" exists)
static void RegisterGdiPlusNS(){
    iTJSDispatch2*g=TVPGetScriptDispatch();if(!g)return;
    // Create a plain TJS Dictionary as the namespace container
    iTJSDispatch2*ns=TJSCreateDictionaryObject();
    if(!ns){g->Release();return;}
    // Helper: look up a registered class by name from the global and store as member of ns
    auto setClass=[&](const tjs_char*alias,const tjs_char*className){
        tTJSVariant cls;
        if(TJS_SUCCEEDED(g->PropGet(0,className,nullptr,&cls,g))){
            ns->PropSet(TJS_MEMBERENSURE,alias,nullptr,&cls,ns);
        }
    };
    setClass("Appearance", "GdiPlusAppearance");
    setClass("Pen",        "GdiPlusPen");
    setClass("SolidBrush", "GdiPlusSolidBrush");
    setClass("Graphics",   "GdiPlusGraphics");
    setClass("Brush",      "GdiPlusSolidBrush");
    tTJSVariant nsVar(ns,ns);
    g->PropSet(TJS_MEMBERENSURE,"GdiPlus",nullptr,&nsVar,g);
    ns->Release();
    g->Release();
}
static void UnregisterGdiPlusNS(){
    iTJSDispatch2*g=TVPGetScriptDispatch();if(!g)return;
    g->DeleteMember(0,"GdiPlus",nullptr,g);g->Release();}
NCB_POST_REGIST_CALLBACK(RegisterGdiPlusNS);
NCB_PRE_UNREGIST_CALLBACK(UnregisterGdiPlusNS);
