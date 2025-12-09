#include "tjsCommHead.h"

#include "TVPMsg.h"
#include "UtilStreams.h"
#include "Platform.h"
#include "TVPArchive.h"

//---------------------------------------------------------------------------
// tar archive
//---------------------------------------------------------------------------
tjs_uint64 parseOctNum(const char* oct, int length)
{
	tjs_uint64 num = 0;
	for (int i = 0; i < length; i++) {
		char c = oct[i];
		if ('0' <= c && c <= '9') {
			num = num * 8 + (c - '0');
		}
	}
	return num;
}

/*
 * tar header block definition
 *
 * From IEEE Std 1003.1-1988, pp.156
 */


// POSIX magic
#define TMAGIC "ustar"
#define TMAGLEN 6

// GNU magic+version:
#define TOMAGIC "ustar  "
#define TOMAGLEN 8

// POSIX version
#define TVERSION "00"
#define TVERSLEN 2

#define REGTYPE '0' /* Regular file */
#define AREGTYPE '\0' /* Regular file; old V7 format */
#define LNKTYPE '1' /* Link */
#define SYMTYPE '2' /* Symbolic link */
#define CHRTYPE '3' /* Character special */
#define BLKTYPE '4' /* Block special */
#define DIRTYPE '5' /* Directory */
#define FIFOTYPE '6' /* FIFO special */
#define CONTTYPE '7' /* Continguous file */
#define LONGLINK 'L' /* Long name Link by tantan*/
#define PAX_ENTRTY                                                             \
    'x' /* PAX header block for file entry : added by claybird 2011.11.29 */
#define PAX_GLOBAL                                                             \
    'g' /* PAX global extended header : added by claybird 2011.11.29 */

#define MULTYPE 'M' /* Added by GNUtar, not POSIX */
#define VOLTYPE 'V' /* Added by GNUtar, not POSIX */


#define TSUID 04000 /* Set UID on execution */
#define TSGID 02000 /* Set GID on execution */
#define TSVTX 01000 /* Reserved */
/* File permissions */
#define TUREAD 00400 /* read by owner */
#define TUWRITE 00200 /* write by owner */
#define TUEXEC 00100 /* execute/search by owner */
#define TGREAD 00040 /* read by group */
#define TGWRITE 00020 /* write by group */
#define TGEXEC 00010 /* execute/search by group */
#define TOREAD 00004 /* read by other */
#define TOWRITE 00002 /* write by other */
#define TOEXEC 00001 /* execute/search by other */


#define TBLOCK 512
#define NAMSIZ 100

// added by claybird(2009.12.05)
#define TAR_FORMAT_GNU 0
#define TAR_FORMAT_POSIX 1

typedef union hblock {
    char dummy[TBLOCK];
    struct {
        char name[NAMSIZ];
        char mode[8];
        char uid[8];
        char gid[8];
        char size[12];
        char mtime[12];
        char chksum[8];
        char typeflag;
        char linkname[NAMSIZ];
        char magic[6];
        char version[2];
        char uname[32];
        char gname[32];
        char devmajor[8];
        char devminor[8];
        union _exthead { // header extension
            struct _POSIX { // POSIX ustar format
                char prefix[155];
                char pad[12];
            } posix;
            struct _GNU { // GNUtar format
                /* Following fields were added by GNUtar */
                char atime[12];
                char ctime[12];
                char offset[12];
            } gnu;
        } exthead;
    } dbuf;
    unsigned int compsum() {
        unsigned int sum = 0;
        int i;
        for(i = 0; i < TBLOCK; i++) {
            sum += (unsigned char)dummy[i];
        }
        /* calc without checksum field */
        for(i = 0; i < sizeof(dbuf.chksum); i++) {
            sum -= dbuf.chksum[i];
            sum += ' ';
        }
        return sum;
    }
    // added by claybird(2011.01.13)
    int compsum_oldtar() {
        unsigned int sum = 0;
        int i;
        for(i = 0; i < TBLOCK; i++) {
            sum +=
                (signed char)dummy[i]; // different way to compute like old unix
        }
        /* calc without checksum field */
        for(i = 0; i < sizeof(dbuf.chksum); i++) {
            sum -= dbuf.chksum[i];
            sum += ' ';
        }
        return sum;
    }
    int getFormat() const {
        if(dbuf.magic[5] == TOMAGIC[5]) {
            return TAR_FORMAT_GNU;
        } else {
            return TAR_FORMAT_POSIX;
        }
    }
} TAR_HEADER;

class TARArchive : public tTVPArchive {
	struct EntryInfo {
		ttstr filename;
		tjs_uint64 offset;
		tjs_uint64 size;
	};
	std::vector<EntryInfo> filelist;
	friend class TARArchiveStream;

public:
	TARArchive(const ttstr& arcname) : tTVPArchive(arcname) {
	}
	~TARArchive() {
		TVPFreeArchiveHandlePoolByPointer(this);
	}
	bool init(tTJSBinaryStream* _instr, bool normalizeFileName) {
		if (_instr) {
			tjs_uint64 archiveSize = _instr->GetSize();
			TAR_HEADER tar_header;
			// check first header
			if (_instr->Read(&tar_header, sizeof(tar_header)) != sizeof(tar_header)) {
				//delete _instr;
				return false;
			}
			unsigned int checksum = parseOctNum(tar_header.dbuf.chksum, sizeof(tar_header.dbuf.chksum));
			if (checksum != tar_header.compsum() && (int)checksum != tar_header.compsum_oldtar()) {
				//delete _instr;
				return false;
			}
			_instr->SetPosition(0);
			while (_instr->GetPosition() <= archiveSize - sizeof(tar_header)) {
				if (_instr->Read(&tar_header, sizeof(tar_header)) != sizeof(tar_header)) break;
				tjs_uint64 original_size = parseOctNum(tar_header.dbuf.size, sizeof(tar_header.dbuf.size));
				std::vector<char> filename;
				if (tar_header.dbuf.typeflag == LONGLINK) {	// tar_header.dbuf.name == "././@LongLink"
					unsigned int readsize = (original_size + (TBLOCK - 1)) & ~(TBLOCK - 1);
					filename.resize(readsize + 1);	//TODO:size lost
					if (_instr->Read(&filename[0], readsize) != readsize) break;
					filename[readsize] = 0;
					if (_instr->Read(&tar_header, sizeof(tar_header)) != sizeof(tar_header)) break;
					original_size = parseOctNum(tar_header.dbuf.size, sizeof(tar_header.dbuf.size));
				}
				if (tar_header.dbuf.typeflag != REGTYPE) continue; // only accept regular file
				if (filename.empty()) {
					filename.resize(101);
					memcpy(&filename[0], tar_header.dbuf.name, sizeof(tar_header.dbuf.name));
					filename[100] = 0;
				}
				EntryInfo item;
				storeFilename(item.filename, &filename[0], ArchiveName);
				if (normalizeFileName)
					NormalizeInArchiveStorageName(item.filename);
				item.size = original_size;
				item.offset = _instr->GetPosition();
				filelist.emplace_back(item);
				tjs_uint64 readsize = (original_size + (TBLOCK - 1)) & ~(TBLOCK - 1);
				_instr->SetPosition(item.offset + readsize);
			}
			if (normalizeFileName) {
				std::sort(filelist.begin(), filelist.end(), [](const EntryInfo& a, const EntryInfo& b) {
					return a.filename < b.filename;
					});
			}
			TVPReleaseCachedArchiveHandle(this, _instr);
			return true;
		}
		return false;
	}
	virtual tjs_uint GetCount() { return filelist.size(); }
	virtual ttstr GetName(tjs_uint idx) { return filelist[idx].filename; }
	virtual tTJSBinaryStream* CreateStreamByIndex(tjs_uint idx);
};

tTJSBinaryStream* TARArchive::CreateStreamByIndex(tjs_uint idx) {
	const EntryInfo& info = filelist[idx];
	return new TArchiveStream(this, info.offset, info.size);
}

tTVPArchive* TVPOpenTARArchive(const ttstr& name, tTJSBinaryStream* st, bool normalizeFileName) {
	TARArchive* arc = new TARArchive(name);
	if (!arc->init(st, normalizeFileName)) {
		delete arc;
		return nullptr;
	}
	return arc;
}
