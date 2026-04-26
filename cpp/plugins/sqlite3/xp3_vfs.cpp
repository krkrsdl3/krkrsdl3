/****************************************************************************/
/*! @file
@brief 吉里吉里のXP3 用 VFS

読み取り専用で、ロックなどもサポートしていない
いくつかのメソッドは SQLite の os_win.c から流用している

-----------------------------------------------------------------------------
	Copyright (C) 2008 T.Imoto <http://www.kaede-software.com>
-----------------------------------------------------------------------------
@author		T.Imoto
@date		2008/04/15
@note
*****************************************************************************/

#include <string>
#include <vector>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include "sqlite3/sqlite3.h"

#define MAX_PATH 4096

#include "TVPStorage.h"

#ifndef SQLITE_DEFAULT_SECTOR_SIZE
#	define SQLITE_DEFAULT_SECTOR_SIZE 512
#endif

//--------------------------------------------------------------------------------------------------
// ファイルアクセス
//--------------------------------------------------------------------------------------------------
struct xp3File
{
	const sqlite3_io_methods *pMethod;	// Must be first
	tTJSBinaryStream*		stream_;	// stream pointer
};

static int xp3Close( sqlite3_file *id )
{
	xp3File*	file = (xp3File*)id;
	if( file->stream_ ) {
		delete file->stream_;
		file->stream_ = NULL;
	}
	return SQLITE_OK;
}
static int xp3Read( sqlite3_file *id, void *pBuf, int iAmt, sqlite3_int64 iOfst )
{
	xp3File*	file = (xp3File*)id;
	if( file->stream_ ) {
		tjs_uint64 hr;
		hr = file->stream_->Seek( iOfst, TJS_BS_SEEK_SET);
		if( hr != iOfst ) {
			return SQLITE_FULL;
		}

		tjs_uint numOfRead = file->stream_->Read( pBuf, iAmt);
		if( numOfRead == iAmt ) {
			return SQLITE_OK;
		} else {
			memset( &((char*)pBuf)[numOfRead], 0, iAmt-numOfRead);
			return SQLITE_IOERR_SHORT_READ;
		}
	}
	return SQLITE_IOERR_READ;
}

// たぶん、アーカイブの時は失敗するだろうけど、一応書いておく
static int xp3Write( sqlite3_file *id, const void *pBuf, int iAmt, sqlite3_int64 iOfst )
{
	xp3File*	file = (xp3File*)id;
	if( file->stream_ ) {
		tjs_uint64 hr;
		hr = file->stream_->Seek( iOfst, TJS_BS_SEEK_SET);
		if( hr != iOfst ) {
			return SQLITE_FULL;
		}

		tjs_uint numOfWrote;
		do {
			numOfWrote = 0;
			numOfWrote = file->stream_->Write( pBuf, iAmt);
			iAmt -= numOfWrote;
			pBuf = &((char*)pBuf)[numOfWrote];
		} while( iAmt > 0 && numOfWrote > 0 );

		if( iAmt <= 0 ) {
			return SQLITE_OK;
		} else {
			return SQLITE_FULL;
		}
	}
	return SQLITE_IOERR_WRITE;
}

// ファイルサイズを変更する
// たぶん、機能しない
// SetSize は E_NOTIMPL が返ってくるはず
static int xp3Truncate( sqlite3_file *id, sqlite3_int64 size )
{
	xp3File*	file = (xp3File*)id;
	if( file->stream_ ) {
		// no need
		return SQLITE_OK;
	}
	return SQLITE_IOERR_TRUNCATE;
}

// 未書き込みのデータを書き込む
static int xp3Sync(sqlite3_file *id, int flags)
{
	// 該当するのが見当たらないので、何もしない
	// ハンドル版は FlushFileBuffers を使うみたい
	return SQLITE_OK;
}

// Determine the current size of a file in bytes
static int xp3FileSize(sqlite3_file *id, sqlite3_int64 *pSize)
{
	xp3File*	file = (xp3File*)id;
	if( file->stream_ ) {
        *pSize = file->stream_->GetSize();
		return SQLITE_OK;
	}
	return SQLITE_IOERR_FSTAT;
}

// IStream::LockRegion/UnlockRegionを使えば実現できると思うが、
// 吉里吉里では実装されていないので、何もしない
static int xp3Lock(sqlite3_file *id, int locktype)
{
	return SQLITE_OK;
}

static int xp3CheckReservedLock(sqlite3_file *id, int *pResOut)
{
	// いつもロックしていない
	return 0;
}

static int xp3Unlock(sqlite3_file *id, int locktype)
{
	// いつもロックしていない
	return SQLITE_OK;
}

static int xp3FileControl(sqlite3_file *id, int op, void *pArg)
{
	switch( op ) {
	case SQLITE_FCNTL_LOCKSTATE:
		*(int*)pArg = 0;
		return SQLITE_OK;
	}
	return SQLITE_ERROR;
}

static int xp3SectorSize(sqlite3_file *id)
{
	return SQLITE_DEFAULT_SECTOR_SIZE;
}
static int xp3DeviceCharacteristics(sqlite3_file *id)
{
	return 0;
}
static const sqlite3_io_methods xp3IoMethod = {
	1,
	xp3Close,
	xp3Read,
	xp3Write,
	xp3Truncate,
	xp3Sync,
	xp3FileSize,
	xp3Lock,
	xp3Unlock,
	xp3CheckReservedLock,
	xp3FileControl,
	xp3SectorSize,
	xp3DeviceCharacteristics
};

//--------------------------------------------------------------------------------------------------
// VFS 関数
//--------------------------------------------------------------------------------------------------
static int xp3Open( sqlite3_vfs *vfs, const char *zName, sqlite3_file *id, int flags, int *pOutFlags )
{
	if( flags & (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_DELETEONCLOSE) ) {
		// 書き込みアクセスや生成、閉じた時に削除するなどは出来ない
		return SQLITE_READONLY;
	}

	tTJSBinaryStream* stream;
	if( (stream = TVPCreateBinaryStreamForRead(zName, TJS_BS_READ)) == NULL) {
		return SQLITE_CANTOPEN;
	}

	if( pOutFlags ) {
		*pOutFlags = SQLITE_OPEN_READONLY;
	}

	xp3File *pFile = (xp3File*)id;
	memset(pFile, 0, sizeof(*pFile));
	pFile->pMethod = &xp3IoMethod;
	pFile->stream_ = stream;
	return SQLITE_OK;
}

static int xp3Delete( sqlite3_vfs *vfs, const char *zFilename, int syncDir )
{
	return SQLITE_READONLY;
}

static int xp3Access( sqlite3_vfs *vfs, const char *zFilename, int flags, int *pResOut )
{
	int rc = 0;
	switch( flags ) {
	case SQLITE_ACCESS_READ:
	case SQLITE_ACCESS_EXISTS:
		if( TVPIsExistentStorage(zFilename) ) {
			rc = 1;
		} else {
			rc = 0;
		}
		break;
	case SQLITE_ACCESS_READWRITE:
		rc = 0;
		break;
	default:
		assert(!"Invalid flags argument");
	}
	*pResOut = rc;
	return SQLITE_OK;
}

// XP3内ではフルパスを返さず、そのまま返す
// パッチなどを当てて、そちら側を読む必要が出る可能性もあるので、フルパスで読むのは妥当ではないと思われる
static int xp3FullPathname( sqlite3_vfs *vfs, const char *zRelative, int nFull, char *zFull )
{
	sqlite3_snprintf(nFull, zFull, "%s", zRelative);
	return SQLITE_OK;
}

static int xp3Sleep(sqlite3_vfs *pVfs, int microsec)
{
#ifdef _WIN32
    Sleep((microsec + 999) / 1000);
#else
    usleep(microsec);
#endif
    return ((microsec + 999) / 1000) * 1000;
}

static int xp3CurrentTime( sqlite3_vfs *pVfs, double *prNow )
{
	
#ifdef _WIN32
    FILETIME ft;
    /* FILETIME structure is a 64-bit value representing the number of
            100-nanosecond intervals since January 1, 1601 (= JD 2305813.5).
    */

    double now;
    GetSystemTimeAsFileTime(&ft);
    now = ((double)ft.dwHighDateTime) * 4294967296.0;
    *prNow = (now + ft.dwLowDateTime) / 864000000000.0 + 2305813.5;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    *prNow = 2440587.5 + tv.tv_sec / 86400.0 + tv.tv_usec / 86400000000.0;
#endif
    return SQLITE_OK;
}

sqlite3_vfs *getXp3Vfs()
{
	static sqlite3_vfs xp3Vfs = {
		1,					// iVersion
		sizeof(xp3File),	// szOsFile
		MAX_PATH,			// mxPathname
		0,					// pNext
		"xp3",				// zName
		0,					// pAppData

		xp3Open,			// xOpen
		xp3Delete,			// xDelete
		xp3Access,			// xAccess
		xp3FullPathname,	// xFullPathname
		NULL,			    // xDlOpen
        NULL,               // xDlError
        NULL,               // xDlSym
        NULL,               // xDlClose
        NULL,               // xRandomness
		xp3Sleep,			// xSleep
		xp3CurrentTime,		// xCurrentTime
        NULL                // xGetLastError
		};
	return &xp3Vfs;
}


