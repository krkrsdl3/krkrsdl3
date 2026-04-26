#include "tjsCommHead.h"
#include "PSBFile.h"

#include "Log.h"
#include "TVPStorage.h"
#include "UtilStreams.h"
#include "tjsArray.h"

#include <zlib.h>

//---------------------------------------------------------------------------
// tTJSNI_PsbFile : PsbFile TJS native instance
//---------------------------------------------------------------------------
tTJSNI_PsbFile::tTJSNI_PsbFile()
{
    Owner = NULL;
    _header.version = 0;
    _header.encrypt = 0;
    _header.offsetEncrypt = 0;
    _header.offsetNames = 0;
    _header.offsetStrings = 0;
    _header.offsetStringsData = 0;
    _header.offsetChunkOffsets = 0;
    _header.offsetChunkLengths = 0;
    _header.offsetChunkData = 0;
    _header.offsetEntries = 0;
    _header.checksum = 0;
    _header.offsetExtraChunkOffsets = 0;
    _header.offsetExtraChunkLengths = 0;
    _header.offsetExtraChunkData = 0;
}
tjs_error tTJSNI_PsbFile::Construct(tjs_int numparams, tTJSVariant** param, iTJSDispatch2* tjs_obj)
{
    Owner = tjs_obj;

    return TJS_S_OK;
}
void tTJSNI_PsbFile::Invalidate()
{
    if (filePtr != nullptr)
        delete filePtr;
    Owner = NULL;
    inherited::Invalidate();
}

bool tTJSNI_PsbFile::load(const ttstr& filePath)
{
    tTJSBinaryStream* _filePtr = TVPCreateStream(filePath);
    return loadFromStream(_filePtr, filePath);
}

bool tTJSNI_PsbFile::loadFromStream(tTJSBinaryStream* _filePtr, const ttstr& filePath)
{
    if (!_filePtr)
        return false;
    // _filePtr指针由PsbFile管理
    if (filePtr != NULL)
        delete filePtr;
    filePtr = _filePtr;
    const size_t readSize = filePtr->GetSize();
    if (readSize < 9)
        return false;

    // Decompress MDF
    char sign[4];
    filePtr->Read(sign, 4);
    sign[3] = '\0';
    if (SDL_strcasecmp(sign, "MDF") == 0)
    {
        // uncompress data
        uLongf uncompressedSize = filePtr->ReadI32LE();
        tjs_uint8* _uncompress_buffer = new tjs_uint8[uncompressedSize];
        // compress data
        tjs_uint8* _compress_buffer = new tjs_uint8[readSize - 8];
        filePtr->Read(_compress_buffer, readSize - 8);
        // uncompress buffer
        if (uncompress(_uncompress_buffer, &uncompressedSize, _compress_buffer, readSize - 8) !=
            Z_OK)
        {
            delete[] _uncompress_buffer;
            delete[] _compress_buffer;
            return false;
        }
        // compress data
        tTVPMemoryStream* _stream = new tTVPMemoryStream(nullptr, uncompressedSize);
        memcpy(_stream->GetInternalBuffer(), _uncompress_buffer, uncompressedSize);
        // clear data
        delete[] _uncompress_buffer;
        delete[] _compress_buffer;
        delete filePtr;
        filePtr = _stream;
    }
    else // 使用memory防止xp3filter解密所带来的性能下降，不过内存可能得遭罪了
    {
        tTVPMemoryStream* _stream = new tTVPMemoryStream(nullptr, filePtr->GetSize());
        filePtr->SetPosition(0);
        filePtr->ReadBuffer(_stream->GetInternalBuffer(), filePtr->GetSize());
        delete filePtr;
        filePtr = _stream;
    }

    // ReadHeader
    filePtr->SetPosition(0);
    _header.parsePSBHeader(filePtr);

    // Encrypted
    if (_header.isEncrypted() && _header.getHeaderLength() > filePtr->GetSize())
    {
        TVPConsoleLog("psb file is encrypted");
        return false;
    }
    if (_header.version > 3)
    {
        TVPConsoleLog("not support psb file format version > 3");
        return false;
    }

    // Pre Load Strings
    filePtr->SetPosition(_header.offsetStrings);
    PSB::parsePSBArray(&stringsOffset,
                       filePtr->ReadI8LE() - static_cast<tjs_int8>(PSB::PSBObjType::ArrayN1) + 1,
                       filePtr);

    // Pre Load Names
    if (_header.version == 1)
    {
        // don't believe HeaderLength
        if (_header.offsetEncrypt >= filePtr->GetSize())
        {
            _header.offsetEncrypt = _header.getHeaderLength();
        }
        filePtr->SetPosition(_header.offsetEncrypt);
        PSB::parsePSBArray(
            &nameIndexes, filePtr->ReadI8LE() - static_cast<tjs_int8>(PSB::PSBObjType::ArrayN1) + 1,
            filePtr);
        // Load Names
        namesCache.reserve(nameIndexes.size());
        for (int i = 0; i < namesCache.size(); i++)
        {
            filePtr->SetPosition(_header.offsetNames + nameIndexes[i]);
            std::string str;
            tjs_uint8 val8 = 0;
            while (true)
            {
                if (filePtr->Read(&val8, 1) == 0)
                    break;

                if (val8 == '\0')
                {
                    str.append(1, val8);
                    break;
                }
                str.append(1, val8);
            }
            namesCache.push_back(str);
        }
    }
    else
    {
        filePtr->SetPosition(_header.offsetNames);
        PSB::parsePSBArray(
            &charset, filePtr->ReadI8LE() - static_cast<tjs_int8>(PSB::PSBObjType::ArrayN1) + 1,
            filePtr);
        PSB::parsePSBArray(
            &namesData, filePtr->ReadI8LE() - static_cast<tjs_int8>(PSB::PSBObjType::ArrayN1) + 1,
            filePtr);
        PSB::parsePSBArray(
            &nameIndexes, filePtr->ReadI8LE() - static_cast<tjs_int8>(PSB::PSBObjType::ArrayN1) + 1,
            filePtr);
        // Load Names
        namesCache.reserve(nameIndexes.size());
        for (int i = 0; i < nameIndexes.size(); i++)
        {
            std::string list;
            const auto index = nameIndexes[i];
            auto chr = namesData[index];
            while (chr != 0)
            {
                const auto code = namesData[chr];
                const auto d = charset[code];
                const auto realChr = chr - d;
                chr = code;
                list.append(1, realChr);
            }
            std::reverse(list.begin(), list.end());
            namesCache.push_back(list);
        }
    }

    // Pre Load Resources (Chunks)
    filePtr->SetPosition(_header.offsetChunkOffsets);
    PSB::parsePSBArray(&chunkOffsets,
                       filePtr->ReadI8LE() - static_cast<tjs_int8>(PSB::PSBObjType::ArrayN1) + 1,
                       filePtr);
    filePtr->SetPosition(_header.offsetChunkLengths);
    PSB::parsePSBArray(&chunkLengths,
                       filePtr->ReadI8LE() - static_cast<tjs_int8>(PSB::PSBObjType::ArrayN1) + 1,
                       filePtr);

    if (_header.version >= 4)
    {
        // Pre Load Extra Resources (Chunks)
        filePtr->SetPosition(_header.offsetExtraChunkOffsets);
        PSB::parsePSBArray(
            &extraChunkOffsets,
            filePtr->ReadI8LE() - static_cast<tjs_int8>(PSB::PSBObjType::ArrayN1) + 1, filePtr);
        filePtr->SetPosition(_header.offsetExtraChunkLengths);
        PSB::parsePSBArray(
            &extraChunkLengths,
            filePtr->ReadI8LE() - static_cast<tjs_int8>(PSB::PSBObjType::ArrayN1) + 1, filePtr);
    }

    // load All Data
    _root = readAllObjs("root", _header.offsetEntries);
    // add To media
    if (psbVar != nullptr && !filePath.IsEmpty())
    {
        psbVar->AddPSBFile(filePath.AsLowerCase(), _resources);
    }
    return true;
}

tTJSVariant tTJSNI_PsbFile::root()
{
    return _root;
}

tjs_uint32 tTJSNI_PsbFile::readListInfo(std::vector<tjs_uint32>* target)
{
    PSB::parsePSBArray(
        target, filePtr->ReadI8LE() - static_cast<tjs_int8>(PSB::PSBObjType::ArrayN1) + 1, filePtr);
    return filePtr->GetPosition();
}

void tTJSNI_PsbFile::refreshListInfo(std::vector<tjs_uint32>* target1,
                                     std::vector<tjs_uint32>* target2)
{
    target2->reserve(target1->size());
    tjs_uint32 basePose = filePtr->GetPosition();
    for (tjs_uint32 i = 0; i < target1->size(); i++)
    {
        filePtr->SetPosition(basePose + target1->at(i));
        target2->at(i) = filePtr->ReadI8LE();
        target1->at(i) += 4;
    }
}

tTJSVariant tTJSNI_PsbFile::readAllObjs(const ttstr& key, tjs_uint32 _objOffset)
{
    filePtr->SetPosition(_objOffset);
    auto typeByte = filePtr->ReadI8LE();
    switch (auto type = static_cast<PSB::PSBObjType>(typeByte))
    {
        case PSB::PSBObjType::None:
            return tTJSVariant();
            ;
        case PSB::PSBObjType::Null:
            return tTJSVariant();
        case PSB::PSBObjType::False:
        case PSB::PSBObjType::True:
            return tTJSVariant(type == PSB::PSBObjType::True);
        case PSB::PSBObjType::NumberN0:
            return tTJSVariant(0);
        case PSB::PSBObjType::NumberN1:
        {
            tjs_int8 val8 = 0;
            filePtr->ReadBuffer(&val8, 1);
            return tTJSVariant(val8);
        }
        case PSB::PSBObjType::NumberN2:
        {
            tjs_int16 val16 = 0;
            filePtr->ReadBuffer(&val16, 2);
            return tTJSVariant(val16);
        }
        case PSB::PSBObjType::NumberN3:
        {
            tjs_int32 val32 = 0;
            filePtr->ReadBuffer(&val32, 3);
            val32 |= ((val32 & 0x800000) == 0 ? 0 : 0xFFFF0000);
            return tTJSVariant(val32);
        }
        case PSB::PSBObjType::NumberN4:
        {
            tjs_int32 val32 = 0;
            filePtr->ReadBuffer(&val32, 4);
            return tTJSVariant(val32);
        }
        case PSB::PSBObjType::NumberN5:
        {
            tjs_int64 val64 = 0;
            filePtr->ReadBuffer(&val64, 5);
            val64 |= ((val64 & 0x8000000000) == 0 ? 0 : 0xFFFFFFFF00000000);
            return tTJSVariant(val64);
        }
        case PSB::PSBObjType::NumberN6:
        {
            tjs_int64 val64 = 0;
            filePtr->ReadBuffer(&val64, 6);
            val64 |= ((val64 & 0x800000000000) == 0 ? 0 : 0xFFFFFF0000000000);
            return tTJSVariant(val64);
        }
        case PSB::PSBObjType::NumberN7:
        {
            tjs_int64 val64 = 0;
            filePtr->ReadBuffer(&val64, 7);
            val64 |= ((val64 & 0x80000000000000) == 0 ? 0 : 0xFFFF000000000000);
            return tTJSVariant(val64);
        }
        case PSB::PSBObjType::NumberN8:
        {
            tjs_int64 val64 = 0;
            filePtr->ReadBuffer(&val64, 8);
            return tTJSVariant(val64);
        }
        case PSB::PSBObjType::Float0:
            return tTJSVariant(0.0f);
        case PSB::PSBObjType::Float:
        {
            tjs_uint8 buffer[4];
            filePtr->ReadBuffer(buffer, 4);
            float tmp;
            std::memcpy(&tmp, buffer, 4);
            return tTJSVariant((tjs_real)tmp);
        }
        case PSB::PSBObjType::Double:
        {
            tjs_uint8 buffer[8];
            filePtr->ReadBuffer(buffer, 8);
            double tmp;
            std::memcpy(&tmp, buffer, 8);
            return tTJSVariant((tjs_real)tmp);
        }
        case PSB::PSBObjType::ArrayN1:
        case PSB::PSBObjType::ArrayN2:
        case PSB::PSBObjType::ArrayN3:
        case PSB::PSBObjType::ArrayN4:
        case PSB::PSBObjType::ArrayN5:
        case PSB::PSBObjType::ArrayN6:
        case PSB::PSBObjType::ArrayN7:
        case PSB::PSBObjType::ArrayN8:
        {
            std::vector<tjs_uint32> tmp;
            PSB::parsePSBArray(&tmp, typeByte - static_cast<tjs_int8>(PSB::PSBObjType::ArrayN1) + 1,
                               filePtr);
            iTJSDispatch2* array = TJSCreateArrayObject();
            for (auto i : tmp)
            {
                tTJSVariant tmp(static_cast<tjs_int32>(i));
                tTJSVariant* args[] = {&tmp};
                static tjs_uint addHint = 0;
                array->FuncCall(0, TJS_N("add"), &addHint, nullptr, 1, args, array);
            }
            tTJSVariant result(array, array);
            array->Release();
            return result;
        }
        case PSB::PSBObjType::StringN1:
        case PSB::PSBObjType::StringN2:
        case PSB::PSBObjType::StringN3:
        case PSB::PSBObjType::StringN4:
        {
            tjs_int32 idx = 0;
            filePtr->ReadBuffer(&idx,
                                typeByte - static_cast<tjs_int8>(PSB::PSBObjType::StringN1) + 1);
            filePtr->SetPosition(_header.offsetStringsData + stringsOffset[idx]);
            std::string str;
            tjs_uint8 val8 = 0;
            while (true)
            {
                if (filePtr->Read(&val8, 1) == 0)
                    break;

                if (val8 == '\0')
                {
                    str.append(1, val8);
                    return tTJSVariant(str);
                }
                str.append(1, val8);
            }
            return tTJSVariant("");
        }
        case PSB::PSBObjType::ResourceN1:
        case PSB::PSBObjType::ResourceN2:
        case PSB::PSBObjType::ResourceN3:
        case PSB::PSBObjType::ResourceN4:
        {
            if (key.IsEmpty())
                return tTJSVariant();

            tjs_int32 idx = 0;
            filePtr->ReadBuffer(&idx,
                                typeByte - static_cast<tjs_int8>(PSB::PSBObjType::ResourceN1) + 1);
            try
            {
                _resources._resources.insert(std::pair<ttstr, PSB::PSBMediaInfo::PSBMediaItemInfo>(
                    key, {_header.offsetChunkData + chunkOffsets.at(idx), chunkLengths.at(idx)}));
                // 有些游戏需要全量数据，也只能给了呗
                tjs_uint8* buffer = new tjs_uint8[chunkLengths.at(idx)];
                filePtr->SetPosition(_header.offsetChunkData + chunkOffsets.at(idx));
                filePtr->ReadBuffer(buffer, chunkLengths.at(idx));
                tTJSVariant ret(buffer, chunkLengths.at(idx));
                delete[] buffer;
                return ret;
            }
            catch (...)
            {
            }
            return tTJSVariant();
        }
        case PSB::PSBObjType::ExtraChunkN1:
        case PSB::PSBObjType::ExtraChunkN2:
        case PSB::PSBObjType::ExtraChunkN3:
        case PSB::PSBObjType::ExtraChunkN4:
        {
            if (key.IsEmpty())
                return tTJSVariant();

            tjs_int32 idx = 0;
            filePtr->ReadBuffer(&idx, typeByte -
                                          static_cast<tjs_int8>(PSB::PSBObjType::ExtraChunkN1) + 1);

            try
            {
                _resources._resources.insert(std::pair<ttstr, PSB::PSBMediaInfo::PSBMediaItemInfo>(
                    key, {_header.offsetChunkData + extraChunkOffsets.at(idx),
                          extraChunkLengths.at(idx)}));
            }
            catch (...)
            {
            }
            return tTJSVariant();
        }
        case PSB::PSBObjType::List:
        {
            std::vector<tjs_uint32> _objsOffset;
            tjs_uint32 _tmpOffset = this->readListInfo(&_objsOffset);

            iTJSDispatch2* array = TJSCreateArrayObject();
            for (auto _offset : _objsOffset)
            {
                tTJSVariant obj = readAllObjs(ttstr(), _tmpOffset + _offset);
                tTJSVariant* args[] = {&obj};
                static tjs_uint addHint = 0;
                array->FuncCall(0, TJS_N("add"), &addHint, nullptr, 1, args, array);
            }
            tTJSVariant result(array, array);
            array->Release();
            return result;
        }
        case PSB::PSBObjType::Objects:
        {
            std::vector<tjs_uint32> _objsOffset, _objsNamesIdx;
            tjs_uint32 _tmpOffset = 0;
            if (_header.version == 1)
            {
                _tmpOffset = this->readListInfo(&_objsOffset);
                this->refreshListInfo(&_objsOffset, &_objsNamesIdx);
            }
            else
            {
                this->readListInfo(&_objsNamesIdx);
                _tmpOffset = this->readListInfo(&_objsOffset);
            }

            iTJSDispatch2* dsp = TJSCreateDictionaryObject();
            for (tjs_int i = 0; i < _objsNamesIdx.size(); i++)
            {
                ttstr keyName = namesCache.at(_objsNamesIdx.at(i));
                tTJSVariant obj = readAllObjs(keyName, _tmpOffset + _objsOffset.at(i));
                dsp->PropSet(TJS_MEMBERENSURE, keyName.c_str(), nullptr, &obj, dsp);
            }
            tTJSVariant result(dsp, dsp);
            dsp->Release();
            return result;
        }
        default:
            TVPConsoleLog("unknown psbObjType");
            return tTJSVariant();
    }
}

//---------------------------------------------------------------------------
// tTJSNC_PsbFile : PsbFile TJS native class
//---------------------------------------------------------------------------
tjs_uint32 tTJSNC_PsbFile::ClassID = (tjs_uint32)-1;
tTJSNC_PsbFile::tTJSNC_PsbFile()
  : tTJSNativeClass(TJS_N("PSBFile")){
    TJS_BEGIN_NATIVE_MEMBERS(PSBFile)
    //----------------------------------------------------------------------
    // finalize/methods
    //----------------------------------------------------------------------
    TJS_DECL_EMPTY_FINALIZE_METHOD
    //----------------------------------------------------------------------
    // constructor/methods
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(_this, tTJSNI_PsbFile, PSBFile){
    if (numparams == 0) return TJS_S_OK;
    else if (numparams == 1 && param[0]->Type() == tvtString)
    {
        ttstr path = *param[0];
        if (_this->load(path))
        {
            if (result)
                *result = true;
        }
        else
        {
            if (result)
                *result = false;
        }
    }
    else if (numparams == 1 && param[0]->Type() == tvtOctet)
    {
        tTJSVariantOctet* data = param[0]->AsOctetNoAddRef();
        // 创建stream存储数据
        tTVPMemoryStream* _memPtr = new tTVPMemoryStream(nullptr, data->GetLength());
        std::memcpy(_memPtr->GetInternalBuffer(), data->GetData(), data->GetLength());
        // 加载
        if (_this->loadFromStream(_memPtr, ""))
        {
            if (result)
                *result = true;
        }
        else
        {
            if (result)
                *result = false;
        }
    }
    else
    {
        return TJS_E_BADPARAMCOUNT;
    }
    return TJS_S_OK;
    }
    TJS_END_NATIVE_CONSTRUCTOR_DECL(PSBFile)
    //----------------------------------------------------------------------
    //-- methods
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(load)
    {
        TJS_GET_NATIVE_INSTANCE(_this, tTJSNI_PsbFile);
        if (numparams != 1)
        {
            return TJS_E_BADPARAMCOUNT;
        }
        if (param[0]->Type() == tvtString)
        {
            ttstr path = *param[0];
            if (_this->load(path))
            {
                if (result)
                    *result = tTJSVariant(true);
            }
            else
            {
                if (result)
                    *result = tTJSVariant(false);
            }
        }
        else if (param[0]->Type() == tvtOctet)
        {
            TVPConsoleLog("PSBFile::load stream no implement!");
        }
        else
        {
            return TJS_E_INVALIDPARAM;
        }
        return TJS_S_OK;
    }
    TJS_END_NATIVE_METHOD_DECL(load)
    //----------------------------------------------------------------------
    // properties
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_PROP_DECL(root){
        TJS_BEGIN_NATIVE_PROP_GETTER{TJS_GET_NATIVE_INSTANCE(_this, tTJSNI_PsbFile);
    if (result)
        *result = _this->root();
    return TJS_S_OK;
    }
    TJS_END_NATIVE_PROP_GETTER
    TJS_DENY_NATIVE_PROP_SETTER
    }
    TJS_END_NATIVE_PROP_DECL(root)
    //----------------------------------------------------------------------
    TJS_END_NATIVE_MEMBERS
}

tTJSNativeInstance* tTJSNC_PsbFile::CreateNativeInstance()
{
    return new tTJSNI_PsbFile();
}

tTJSNativeClass* TVPCreateNativeClass_PsbFile()
{
    return new tTJSNC_PsbFile();
}
