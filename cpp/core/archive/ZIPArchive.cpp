#include "tjsCommHead.h"

#include "UtilStreams.h"

#include "minizip/unzip.h"

class ZipArchive : public tTVPArchive {
    unzFile uf;
    typedef std::pair<ttstr, unz64_file_pos> FileEntry;
    std::vector<FileEntry> filelist;

public:
    ~ZipArchive();
    ZipArchive(const ttstr& name, tTJSBinaryStream* st, bool normalizeFileName);

    bool isValid() { return uf != nullptr; }
    virtual tjs_uint GetCount() { return filelist.size(); }
    virtual ttstr GetName(tjs_uint idx) { return filelist[idx].first; }
    virtual tTJSBinaryStream* CreateStreamByIndex(tjs_uint idx);

    tTJSBinaryStream* _st = nullptr;
};

static voidpf zip_open64file(voidpf opaque, const void* filename, int mode) {
    if (mode == (ZLIB_FILEFUNC_MODE_READ | ZLIB_FILEFUNC_MODE_EXISTING))
        return (voidpf)filename;
    return nullptr;
}

static uLong zip_readfile(voidpf, voidpf s, void* buf, uLong size) {
    return ((ZipArchive*)s)->_st->Read(buf, size);
}

static uLong zip_writefile(voidpf, voidpf s, const void* buf, uLong size) {
    return ((ZipArchive*)s)->_st->Write(buf, size);
}

static ZPOS64_T zip_tell64file(voidpf, voidpf s) {
    return ((ZipArchive*)s)->_st->GetPosition();
}

static long zip_seek64file(voidpf, voidpf s, ZPOS64_T offset, int origin) {
    ((ZipArchive*)s)->_st->Seek(offset, origin);
    return 0;
}

static int zip_closefile(voidpf, voidpf s) {
    //	delete ((tTJSBinaryStream*)s);
    return 0;
}

static zlib_filefunc64_32_def zipfunc = {
    {
        zip_open64file,
        zip_readfile,
        zip_writefile,
        zip_tell64file,
        zip_seek64file,
        zip_closefile,
        nullptr,
        nullptr
    },
    nullptr,
    nullptr,
    nullptr
};

tTVPArchive* TVPOpenZIPArchive(const ttstr& name, tTJSBinaryStream* st, bool normalizeFileName) {
    tjs_uint64 pos = st->GetPosition();
    bool checkZIP = st->ReadI16LE() == 0x4B50; // 'PK'
    st->SetPosition(pos);
    if (!checkZIP) return nullptr;
    ZipArchive* arc = new ZipArchive(name, st, normalizeFileName);
    if (!arc->isValid()) {
        arc->_st = nullptr;
        delete arc;
        return nullptr;
    }
    return arc;
}

tTJSBinaryStream* ZipArchive::CreateStreamByIndex(tjs_uint idx) {
    if (unzGoToFilePos64(uf, &filelist[idx].second) != UNZ_OK) return nullptr;
    // decompress and hold in memory for random access
    unz_file_info file_info;
    if (unzGetCurrentFileInfo(uf, &file_info, NULL, 0, NULL, 0, NULL, 0) != UNZ_OK) return nullptr;
    if (file_info.compression_method == 0) { // uncompressed
        uInt iSizeVar;
        ZPOS64_T offset_local_extrafield;  /* offset of the local extra field */
        uInt  size_local_extrafield;    /* size of the local extra field */
        if (unzCheckCurrentFileCoherencyHeader(uf, &iSizeVar, &offset_local_extrafield, &size_local_extrafield) != UNZ_OK)
            return nullptr;
        return new TArchiveStream(this, unzGetStartPos(uf, iSizeVar), file_info.uncompressed_size);
    }
    else {
        if (unzOpenCurrentFile(uf) != UNZ_OK) return nullptr;
        tTVPMemoryStream* mem = new tTVPMemoryStream();
        mem->SetSize(file_info.uncompressed_size);
        unzReadCurrentFile(uf, mem->GetInternalBuffer(), file_info.uncompressed_size);
        unzCloseCurrentFile(uf);
        return mem;
    }
}

ZipArchive::~ZipArchive() {
    if (uf) {
        unzClose(uf);
        uf = NULL;
    }
    if (_st) {
        delete _st;
        _st = nullptr;
    }
}

void storeFilename(ttstr& name, const char* narrowName, const ttstr& filename);
ZipArchive::ZipArchive(const ttstr& name, tTJSBinaryStream* st, bool normalizeFileName) : tTVPArchive(name) {
    if (!st) st = TVPCreateStream(name);
    _st = st;
    if ((uf = unzOpen3(this, &zipfunc, 1)) != NULL) {
        unz_file_info file_info;
        do {
            unz64_file_pos entry;
            if (unzGetFilePos64(uf, &entry) == UNZ_OK) {
                ttstr filename;
                char filename_inzip[1024];
                if (unzGetCurrentFileInfo(uf, &file_info, filename_inzip, sizeof(filename_inzip), NULL, 0, NULL, 0) == UNZ_OK) {
                    storeFilename(filename, filename_inzip, name);
                    if (normalizeFileName)
                        NormalizeInArchiveStorageName(filename);
                    filelist.emplace_back(filename, entry);
                }
            }
        } while (unzGoToNextFile(uf) == UNZ_OK);
        if (normalizeFileName) {
            std::sort(filelist.begin(), filelist.end(), [](const FileEntry& a, const FileEntry& b) {
                return a.first < b.first;
                });
        }
    }
}

