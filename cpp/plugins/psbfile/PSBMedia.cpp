#pragma once

#include "PSBMedia.h"
#include "UtilStreams.h"
#include "PSBFile.h"

PSB::PSBMedia* psbVar = nullptr;

namespace PSB
{

void PSBMedia::NormalizeDomainName(ttstr& name)
{
    tjs_int dotIndex = name.IndexOf(TJS_W('.'));
    if (dotIndex == -1)
        return;
    name = name.SubString(0, dotIndex) + name.SubString(dotIndex, name.GetLen()).AsLowerCase();
}

void PSBMedia::NormalizePathName(ttstr& name)
{
    // nothing to do
}

bool PSBMedia::CheckExistentStorage(const ttstr& name)
{
    tjs_int dotIndex = name.IndexOf(TJS_W('/'));
    if (dotIndex == -1)
        return false;
    std::map<ttstr, PSBMediaInfo>::iterator iterFile = _resources.find(name.SubString(0, dotIndex));
    if (iterFile == _resources.end())
    {
        tTJSNI_PsbFile tmp;
        tmp.load(name.SubString(0, dotIndex));
        iterFile = _resources.find(name.SubString(0, dotIndex));
        if (iterFile == _resources.end())
            return false;
    }
    std::map<ttstr, PSBMediaInfo::PSBMediaItemInfo>::iterator iterChunk =
        iterFile->second._resources.find(name.SubString(dotIndex + 1, name.GetLen()));
    if (iterChunk == iterFile->second._resources.end())
        return false;
    return true;
}

tTJSBinaryStream* PSBMedia::Open(const ttstr& name, tjs_uint32 flags)
{
    tjs_int dotIndex = name.IndexOf(TJS_W('/'));
    if (dotIndex == -1)
        return nullptr;
    std::map<ttstr, PSBMediaInfo>::iterator iterFile = _resources.find(name.SubString(0, dotIndex));
    if (iterFile == _resources.end())
        return nullptr;
    std::map<ttstr, PSBMediaInfo::PSBMediaItemInfo>::iterator iterChunk =
        iterFile->second._resources.find(name.SubString(dotIndex + 1, name.GetLen()));
    if (iterChunk == iterFile->second._resources.end())
        return nullptr;
    
    tTJSBinaryStream* filePtr = TVPCreateStream(iterFile->first);
    if (!filePtr)
        return nullptr;

    tTVPMemoryStream* memoryStream = new tTVPMemoryStream(nullptr, iterChunk->second.Lengths);
    filePtr->SetPosition(iterChunk->second.Offsets);
    filePtr->ReadBuffer(memoryStream->GetInternalBuffer(), iterChunk->second.Lengths);
    delete filePtr;
    return memoryStream;
}

void PSBMedia::GetListAt(const ttstr& name, iTVPStorageLister* lister)
{
    // nothing to do
    SDL_Log("PSB:GetListAt");
}

void PSBMedia::GetLocallyAccessibleName(ttstr& name)
{
    // nothing to do
    SDL_Log("PSB:GetLocallyAccessibleName");
}

void PSBMedia::AddPSBFile(const ttstr& name, PSBMediaInfo data)
{
    _resources.insert(std::pair<ttstr, PSBMediaInfo>(name, data));
}

void PSBMedia::RemovePSBFile(const ttstr& name)
{
    _resources.erase(name);
}

}
