#include "tjsCommHead.h"
#include "tjsNativeBasicDrawDevice.h"

//---------------------------------------------------------------------------
// tTJSNI_BasicDrawDevice : BasicDrawDevice TJS native class
//---------------------------------------------------------------------------
tjs_uint32 tTJSNC_BasicDrawDevice::ClassID = (tjs_uint32)-1;
tTJSNC_BasicDrawDevice::tTJSNC_BasicDrawDevice() :
    tTJSNativeClass(TJS_W("BasicDrawDevice")){
        // register native methods/properties

        TJS_BEGIN_NATIVE_MEMBERS(BasicDrawDevice) TJS_DECL_EMPTY_FINALIZE_METHOD
            //----------------------------------------------------------------------
            // constructor/methods
            //----------------------------------------------------------------------
            TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(
                /*var.name*/ _this, /*var.type*/ tTJSNI_BasicDrawDevice,
                /*TJS class name*/ BasicDrawDevice){ return TJS_S_OK;
}
TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/ BasicDrawDevice)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ recreate) {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_BasicDrawDevice);
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ recreate)
//----------------------------------------------------------------------


//---------------------------------------------------------------------------
//----------------------------------------------------------------------
// properties
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(interface){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_BasicDrawDevice);
*result = reinterpret_cast<tjs_int64>(_this->GetDevice());
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(interface)
//----------------------------------------------------------------------
#define TVP_REGISTER_PTDD_ENUM(name)                                           \
    TJS_BEGIN_NATIVE_PROP_DECL(name){                                          \
        TJS_BEGIN_NATIVE_PROP_GETTER{ *result = (tjs_int64)                    \
                                       tTJSNI_BasicDrawDevice::name;           \
    return TJS_S_OK;                                                           \
    }                                                                          \
    TJS_END_NATIVE_PROP_GETTER                                                 \
    TJS_DENY_NATIVE_PROP_SETTER                                                \
    }                                                                          \
    TJS_END_NATIVE_PROP_DECL(name)
// compatible for old kirikiri2
TVP_REGISTER_PTDD_ENUM(dtNone)
TVP_REGISTER_PTDD_ENUM(dtDrawDib)
TVP_REGISTER_PTDD_ENUM(dtDBGDI)
TVP_REGISTER_PTDD_ENUM(dtDBDD)
TVP_REGISTER_PTDD_ENUM(dtDBD3D)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(preferredDrawer){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_BasicDrawDevice);
*result = (tjs_int64)(_this->GetPreferredDrawerType());
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER {
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_BasicDrawDevice);
    _this->SetPreferredDrawerType((tTJSNI_BasicDrawDevice::tDrawerType)(tjs_int)*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(preferredDrawer)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(drawer){ TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_BasicDrawDevice);
*result = (tjs_int64)(_this->GetDrawerType());
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(drawer)
//----------------------------------------------------------------------
TJS_END_NATIVE_MEMBERS
}
//---------------------------------------------------------------------------
iTJSNativeInstance *tTJSNC_BasicDrawDevice::CreateNativeInstance() {
    return new tTJSNI_BasicDrawDevice();
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
tTJSNI_BasicDrawDevice::tTJSNI_BasicDrawDevice() {
    Device = new tTVPBasicDrawDevice();
}
//---------------------------------------------------------------------------
tTJSNI_BasicDrawDevice::~tTJSNI_BasicDrawDevice() {
    if(Device)
        Device->Destruct(), Device = NULL;
}
//---------------------------------------------------------------------------
tjs_error TJS_INTF_METHOD tTJSNI_BasicDrawDevice::Construct(
    tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *tjs_obj) {
    return TJS_S_OK;
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTJSNI_BasicDrawDevice::Invalidate() {
    if(Device)
        Device->Destruct(), Device = NULL;
}
//---------------------------------------------------------------------------