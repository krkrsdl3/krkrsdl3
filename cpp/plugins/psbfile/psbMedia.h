#pragma once
#include "TVPStorage.h"

#include "SDL3/SDL.h"
#include <map>

namespace PSB {
    struct PSBMediaInfo
    {
        struct PSBMediaItemInfo
        {
            tjs_uint32 Offsets;
            tjs_uint32 Lengths;
        };
        std::map<ttstr, PSBMediaItemInfo> _resources;
    };

    class PSBMedia : public iTVPStorageMedia {
    public:
        PSBMedia() : refCount(1) {}

        virtual void AddRef() { refCount++; };

        virtual void Release()
        {
            if (refCount == 1)
            {
                delete this;
            }
            else
            {
                refCount--;
            }
        };

        // returns media name like "file", "http" etc.
        virtual void GetName(ttstr& name)
        {
            name = TJS_W("psb");
        }

        //	virtual ttstr IsCaseSensitive() = 0;
        // returns whether this media is case sensitive or not

        // normalize domain name according with the media's rule
        virtual void NormalizeDomainName(ttstr& name) override;

        // normalize path name according with the media's rule
        virtual void NormalizePathName(ttstr& name) override;

        // check file existence
        virtual bool CheckExistentStorage(const ttstr& name) override;

        // open a storage and return a tTJSBinaryStream instance.
        // name does not contain in-archive storage name but
        // is normalized.
        virtual tTJSBinaryStream* Open(const ttstr& name, tjs_uint32 flags) override;

        // list files at given place
        virtual void GetListAt(const ttstr& name,
                                               iTVPStorageLister* lister) override;

        // basically the same as above,
        // check wether given name is easily accessible from local OS filesystem.
        // if true, returns local OS native name. otherwise returns an empty string.
        virtual void GetLocallyAccessibleName(ttstr& name) override;

        void AddPSBFile(const ttstr& name, PSBMediaInfo data);
        void RemovePSBFile(const ttstr& name);

    protected:
        /**
         * デストラクタ
         */
        virtual ~PSBMedia() {}

    private:
        tjs_uint refCount; //< リファレンスカウント
        std::map<ttstr, PSBMediaInfo> _resources;
    };
}

extern PSB::PSBMedia* psbVar;