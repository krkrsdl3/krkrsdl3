#include "ncbind/ncbind.hpp"

#include "TVPStorage.h"
#include "TVPPlugin.h"
#include "Platform.h"

#include <filesystem>

#define NCB_MODULE_NAME TJS_N("fstat.dll")

#ifndef _KRKRSDL3_WINDOWS
#define FILE_ATTRIBUTE_READONLY 0x0001
#define FILE_ATTRIBUTE_HIDDEN 0x0002
#define FILE_ATTRIBUTE_SYSTEM 0x0004
#define FILE_ATTRIBUTE_DIRECTORY 0x0010
#define FILE_ATTRIBUTE_ARCHIVE 0x0020
#define FILE_ATTRIBUTE_DEVICE 0x0040
#define FILE_ATTRIBUTE_NORMAL 0x0080
#define FILE_ATTRIBUTE_TEMPORARY 0x0100
#define FILE_ATTRIBUTE_SPARSE_FILE 0x0200
#define FILE_ATTRIBUTE_REPARSE_POINT 0x0400
#define FILE_ATTRIBUTE_COMPRESSED 0x0800
#define FILE_ATTRIBUTE_OFFLINE 0x1000
#define FILE_ATTRIBUTE_NOT_CONTENT_INDEXED 0x2000
#define FILE_ATTRIBUTE_ENCRYPTED 0x4000
#endif

// Date クラスメンバ
static iTJSDispatch2* dateClass = nullptr;   // Date のクラスオブジェクト
static iTJSDispatch2* dateSetTime = nullptr; // Date.setTime メソッド
static iTJSDispatch2* dateGetTime = nullptr; // Date.getTime メソッド

static const tjs_char* StoragesFstatPreScript = R"(
global.FILE_ATTRIBUTE_READONLY = 0x00000001,
global.FILE_ATTRIBUTE_HIDDEN = 0x00000002,
global.FILE_ATTRIBUTE_SYSTEM = 0x00000004,
global.FILE_ATTRIBUTE_DIRECTORY = 0x00000010,
global.FILE_ATTRIBUTE_ARCHIVE = 0x00000020,
global.FILE_ATTRIBUTE_NORMAL = 0x00000080,
global.FILE_ATTRIBUTE_TEMPORARY = 0x00000100;)";

static std::string _searchPath(const std::string& filename, const std::string& searchpath)
{
    std::vector<std::string> paths;

    if (!searchpath.empty())
    {
#ifdef _KRKRSDL3_WINDOWS
        char delimiter = ';';
#else
        char delimiter = ':';
#endif
        std::stringstream ss(searchpath);
        std::string p;
        while (std::getline(ss, p, delimiter))
        {
            if (!p.empty())
                paths.push_back(p);
        }
    }
    else
    {
        const char* env_path = std::getenv("PATH");
        if (env_path)
        {
#ifdef _KRKRSDL3_WINDOWS
            char delimiter = ';';
#else
            char delimiter = ':';
#endif
            std::stringstream ss(env_path);
            std::string p;
            while (std::getline(ss, p, delimiter))
            {
                if (!p.empty())
                    paths.push_back(p);
            }
        }
    }
    for (const auto& p : paths)
    {
        std::filesystem::path full = std::filesystem::path(p) / filename;
        if (std::filesystem::exists(full) && std::filesystem::is_regular_file(full))
        {
            return std::filesystem::absolute(full).string();
        }
    }
    return "";
}

/**
 * メソッド追加用
 */
class StoragesFstat
{

    /**
     * ファイル時刻を Date クラスにして保存
     * @param store 格納先
     * @param filetime ファイル時刻
     */
    static void storeDate(tTJSVariant& store, const tjs_int64 filetime, iTJSDispatch2* objthis)
    {
        // ファイル生成時
        if (filetime > 0)
        {
            iTJSDispatch2* obj;
            if (TJS_SUCCEEDED(dateClass->CreateNew(0, nullptr, nullptr, &obj, 0, nullptr, objthis)))
            {
                // UNIX TIME に変換
                tTJSVariant time(filetime);
                tTJSVariant* param[] = {&time};
                dateSetTime->FuncCall(0, nullptr, nullptr, nullptr, 1, param, obj);
                store = tTJSVariant(obj, obj);
                obj->Release();
            }
        }
    }
    /**
     * Date クラスの時刻をファイル時刻に変換
     * @param restore  参照先（Dateクラスインスタンス）
     * @param filetime ファイル時刻結果格納先
     * @return 取得できたかどうか
     */
    static bool restoreDate(tTJSVariant& restore, tjs_int64& filetime)
    {
        if (restore.Type() != tvtObject)
            return false;
        iTJSDispatch2* date = restore.AsObjectNoAddRef();
        if (!date)
            return false;
        tTJSVariant result;
        if (dateGetTime->FuncCall(0, nullptr, nullptr, &result, 0, nullptr, date) != TJS_S_OK)
            return false;
        filetime = result.AsInteger();
        return true;
    }

    /**
     * パスをローカル化する＆末尾の\を削除
     * @param path パス名
     */
    static void getLocalName(ttstr& path)
    {
        TVPGetLocalName(path);
        if (path.GetLastChar() == TJS_N('\\'))
        {
            tjs_int i, len = path.length();
            auto* tmp = new tjs_char[len];
            const tjs_char* dp = path.c_str();
            for (i = 0, len--; i < len; i++)
                tmp[i] = dp[i];
            tmp[i] = 0;
            path = tmp;
            delete[] tmp;
        }
    }
    /**
     * ローカルパスの有無判定
     * @param in  path  パス名
     * @param out local ローカルパス
     * @return ローカルパスがある場合はtrue
     */
    static bool getLocallyAccessibleName(const ttstr& path)
    {
        const ttstr& local(TVPGetLocallyAccessibleName(path));
        return !local.IsEmpty();
    }

    /**
     * ファイルのタイムスタンプを取得する
     * @param filename ファイル名（ローカル名であること）
     * @param ctime 作成時刻
     * @param atime アクセス時刻
     * @param mtime 変更時刻
     * @param size  ファイルサイズ
     * @return 0:失敗 1:ファイル 2:フォルダ
     */
    static int getFileTime(ttstr const& filename,
                           tTJSVariant& ctime,
                           tTJSVariant& atime,
                           tTJSVariant& mtime,
                           tTJSVariant* size = nullptr)
    {
        tTVP_stat st{};
        TVP_stat(filename.c_str(), st);
        const bool isdir = (st.tvp_mode & 0170000) == S_IFDIR;

        if (!isdir && size != nullptr)
        {
            *size = static_cast<tjs_int64>(st.tvp_size);
        }
        storeDate(ctime, st.tvp_ctime, nullptr);
        storeDate(atime, st.tvp_atime, nullptr);
        storeDate(mtime, st.tvp_mtime, nullptr);
        return isdir ? 2 : 1;
    }
    /**
     * ファイルのタイムスタンプを設定する
     * @param filename ファイル名（ローカル名であること）
     * @param ctime 作成時刻
     * @param mtime 変更時刻
     * @param atime アクセス時刻
     * @return 0:失敗 1:ファイル 2:フォルダ
     */
    static int setFileTime(ttstr const& filename,
                           tTJSVariant& ctime,
                           tTJSVariant& atime,
                           tTJSVariant& mtime)
    {
        tTVP_stat st{};
        TVP_stat(filename.c_str(), st);
        const bool isdir = (st.tvp_mode & 0170000) == S_IFDIR;

        tjs_int64 m;
        const bool hasC = restoreDate(mtime, m);
        const bool r = TVP_utime(filename.AsStdString().c_str(), hasC ? m : 0);

        return r ? 0 : isdir ? 2 : 1;
    }
    static tjs_error _getTime(tTJSVariant* result, tTJSVariant const* param, bool chksize)
    {
        // 実ファイルでチェック
        ttstr filename = TVPNormalizeStorageName(param->AsStringNoAddRef());
        getLocalName(filename);
        tTJSVariant size, ctime, atime, mtime;
        const int sel = getFileTime(filename, ctime, atime, mtime, chksize ? &size : nullptr);
        if (result)
        {
            if (iTJSDispatch2* dict = TJSCreateDictionaryObject(); dict != nullptr)
            {
                if (chksize && sel == 1)
                    dict->PropSet(TJS_MEMBERENSURE, TJS_N("size"), nullptr, &size, dict);
                dict->PropSet(TJS_MEMBERENSURE, TJS_N("mtime"), nullptr, &mtime, dict);
                dict->PropSet(TJS_MEMBERENSURE, TJS_N("ctime"), nullptr, &ctime, dict);
                dict->PropSet(TJS_MEMBERENSURE, TJS_N("atime"), nullptr, &atime, dict);
                *result = dict;
                dict->Release();
            }
        }
        return TJS_S_OK;
    }

public:
    StoragesFstat() = default;

    static void clearStorageCaches() { TVPClearStorageCaches(); }

    /**
     * 指定されたファイルの情報を取得する
     * @param filename ファイル名
     * @return サイズ・時刻辞書
     * ※アーカイブ内ファイルはサイズのみ返す
     */
    static tjs_error fstat(tTJSVariant* result,
                           tjs_int numparams,
                           tTJSVariant** param,
                           iTJSDispatch2* objthis)
    {
        if (numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        if (ttstr filename = TVPGetPlacedPath(*param[0]);
            filename.length() > 0 && !getLocallyAccessibleName(filename))
        {
            // アーカイブ内ファイル
            tTJSBinaryStream* in = TVPCreateBinaryStreamForRead(filename, "");
            if (in)
            {
                tTJSVariant size((tjs_int64)in->GetSize());
                if (result)
                {
                    iTJSDispatch2* dict;
                    if ((dict = TJSCreateDictionaryObject()) != nullptr)
                    {
                        dict->PropSet(TJS_MEMBERENSURE, TJS_N("size"), nullptr, &size, dict);
                        *result = dict;
                        dict->Release();
                    }
                }
                delete in;
                return TJS_S_OK;
            }
        }
        return _getTime(result, param[0], true);
    }
    /**
     * 指定されたファイルのタイムスタンプ情報を取得する（アーカイブ内不可）
     * @param filename ファイル名
     * @param dict     時刻辞書
     * @return 成功したかどうか
     */
    static tjs_error getTime(tTJSVariant* result,
                             tjs_int numparams,
                             tTJSVariant** param,
                             iTJSDispatch2*)
    {
        if (numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        return _getTime(result, param[0], false);
    }
    /**
     * 指定されたファイルのタイムスタンプ情報を設定する
     * @param filename ファイル名
     * @param dict     時刻辞書
     * @return 成功したかどうか
     */
    static tjs_error setTime(tTJSVariant* result,
                             tjs_int numparams,
                             tTJSVariant** param,
                             iTJSDispatch2*)
    {
        if (numparams < 2)
            return TJS_E_BADPARAMCOUNT;

        ttstr filename = TVPNormalizeStorageName(param[0]->AsStringNoAddRef());
        getLocalName(filename);
        tTJSVariant size, ctime, atime, mtime;
        iTJSDispatch2* dict = param[1]->AsObjectNoAddRef();
        if (dict != nullptr)
        {
            dict->PropGet(0, TJS_N("ctime"), nullptr, &ctime, dict);
            dict->PropGet(0, TJS_N("atime"), nullptr, &atime, dict);
            dict->PropGet(0, TJS_N("mtime"), nullptr, &mtime, dict);
        }
        setFileTime(filename, ctime, atime, mtime);
        if (result)
            *result = true;

        return TJS_S_OK;
    }

    /**
     * 更新日時取得・設定（Dateを経由しない高速版）
     * @param target 対象
     * @param time 時間（64bit FILETIME数）
     */
    static tjs_int64 getLastModifiedFileTime(ttstr target)
    {
        ttstr filename = TVPNormalizeStorageName(target);
        getLocalName(filename);
        tTVP_stat stat{};
        if (TVP_stat(filename.c_str(), stat))
        {
            return 0;
        }
        return static_cast<tjs_int64>(stat.tvp_mtime);
    }
    static bool setLastModifiedFileTime(ttstr target, tjs_int64 time)
    {
        ttstr filename = TVPNormalizeStorageName(target);
        getLocalName(filename);
        return TVP_utime(filename.AsStdString().c_str(), time);
    }

    /**
     * 吉里吉里のストレージ空間中のファイルを抽出する
     * @param src 保存元ファイル
     * @param dest 保存先ファイル
     */
    static void exportFile(ttstr filename, ttstr storename)
    {
        tTJSBinaryStream* in = TVPCreateBinaryStreamForRead(filename, "");
        if (in)
        {
            tTJSBinaryStream* out = TVPCreateBinaryStreamForWrite(storename, "");
            if (out)
            {
                tjs_uint8 buffer[1024 * 16];
                tjs_int32 size;
                while ((size = in->Read(buffer, sizeof buffer)) > 0)
                {
                    out->Write(buffer, size);
                }
                delete out;
                delete in;
            }
            else
            {
                delete in;
                TVPThrowExceptionMessage(
                    (ttstr(TJS_N("cannot open storefile: ")) + storename).c_str());
            }
        }
        else
        {
            TVPThrowExceptionMessage((ttstr(TJS_N("cannot open readfile: ")) + filename).c_str());
        }
    }

    /**
     * 吉里吉里のストレージ空間中の指定ファイルを削除する。
     * @param file 削除対象ファイル
     * @return 実際に削除されたら true
     * 実ファイルがある場合のみ削除されます
     */
    static bool deleteFile(const tjs_char* file)
    {
        bool r = false;
        ttstr filePlacedPath = TVPGetPlacedPath(file);
        if (!filePlacedPath.length())
            return r;
        ttstr filename(TVPGetLocallyAccessibleName(filePlacedPath));
        if (filename.length())
        {
            r = TVPDeleteFile(filename.AsStdString());
            if (!r)
            {
                TVPAddLog(ttstr(TJS_N("deleteFile : ")) + filename + TJS_N("Failed"));
            }
            else
            {
                // 削除に成功した場合はストレージキャッシュをクリア
                TVPClearStorageCaches();
            }
        }
        return r;
    }

    /**
     * 吉里吉里のストレージ空間中の指定ファイルのサイズを変更する(切り捨てる)
     * @param file ファイル
     * @param size 指定サイズ
     * @return サイズ変更できたら true
     * 実ファイルがある場合のみ処理されます
     */
    static bool truncateFile(const tjs_char* file, tjs_int size)
    {
        try
        {
            std::filesystem::resize_file(ttstr(file).AsStdString(), size);
        }
        catch (...)
        {
            return false;
        }
        return true;
    }

    /**
     * 指定ファイルを移動する。
     * @param fromFile 移動対象ファイル
     * @param toFile 移動先パス
     * @return 実際に移動されたら true
     * 移動対象ファイルが実在し、移動先パスにファイルが無い場合のみ移動されます
     */
    static bool moveFile(const tjs_char* from, const tjs_char* to)
    {
        bool r = false;
        const ttstr& fromFile(TVPGetLocallyAccessibleName(from));
        const ttstr& toFile(TVPGetLocallyAccessibleName(to));
        if (fromFile.length() && toFile.length())
        {
            const std::string& ff = fromFile.AsStdString();
            r = TVPCopyFile(ff, toFile.AsStdString());
            TVPDeleteFile(ff);
            if (!r)
            {
                ttstr mes;
                TVPAddLog(ttstr(TJS_N("moveFile : ")) + fromFile + ", " + toFile + TJS_N("Failed"));
            }
            else
            {
                TVPClearStorageCaches();
            }
        }
        return r;
    }

    static tjs_error dirtree(tTJSVariant* result,
                             tjs_int numparams,
                             tTJSVariant** param,
                             iTJSDispatch2* objthis)
    {
        if (numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        bool dironly = numparams > 1 ? param[1]->operator bool() : false;

        ttstr path(TVPNormalizeStorageName(ttstr(*param[0]) + TJS_N("/")));
        TVPGetLocalName(path);

        iTJSDispatch2* array = TJSCreateArrayObject();
        tTJSVariant ret(array, array);
        array->Release();

        tjs_int count = 0;
        _dirtree(path, "", array, count, dironly);

        if (result)
            *result = ret;
        return TJS_S_OK;
    }

    static void _dirtree(
        const ttstr& path, const ttstr& subdir, iTJSDispatch2* array, tjs_int& count, bool dironly)
    {
        std::filesystem::path fsPath(path.c_str());
        if (!std::filesystem::exists(fsPath) || !std::filesystem::is_directory(fsPath))
            return;

        for (auto& entry : std::filesystem::directory_iterator(fsPath))
        {
            auto name = entry.path().filename().string();
            if (name == "." || name == "..")
                continue;

            if (entry.is_directory())
            {
                ttstr fullName = subdir + name + TJS_N("/");
                setDirListFile(array, count++, fullName, entry);
                _dirtree(path + name + TJS_N("/"), fullName, array, count, dironly);
            }
            else if (!dironly)
            {
                ttstr fullName = subdir + name;
                setDirListFile(array, count++, fullName, entry);
            }
        }
    }

    /**
     * 指定ディレクトリのファイル一覧を取得する
     * @param dir ディレクトリ名
     * @return ファイル名一覧が格納された配列
     */
    static tTJSVariant dirlist(tjs_char const* dir) { return _dirlist(dir, &setDirListFile); }

    /**
     * 指定ディレクトリのファイル一覧と詳細情報を取得する
     * @param dir ディレクトリ名
     * @return ファイル情報一覧が格納された配列
     *         [ %[ name:ファイル名, size, attrib, mtime, atime, ctime ], ... ]
     * dirlistと違いnameにおいてフォルダの場合の末尾"/"追加がないので注意(attribで判定のこと)
     */
    static tTJSVariant dirlistEx(tjs_char const* dir) { return _dirlist(dir, &setDirListInfo); }

    typedef bool (*DirListCallback)(iTJSDispatch2* array,
                                    tjs_int count,
                                    const ttstr& file,
                                    const std::filesystem::directory_entry& entry);

private:
    static tTJSVariant _dirlist(ttstr dir, DirListCallback cb)
    {
        dir = TVPNormalizeStorageName(dir);

        if (dir.GetLastChar() != TJS_N('/'))
        {
            TVPThrowExceptionMessage(
                TJS_N("'/' must be specified at the end of given directory name."));
        }
        TVPGetLocalName(dir);

        iTJSDispatch2* array = TJSCreateArrayObject();
        tTJSVariant result;

        try
        {
            std::filesystem::path path(dir.c_str());
            if (!std::filesystem::exists(path) || !std::filesystem::is_directory(path))
            {
                TVPThrowExceptionMessage(TJS_N("Directory not found."));
            }

            tjs_int count = 0;
            for (auto& entry : std::filesystem::directory_iterator(path))
            {
                ttstr file = entry.path().filename().string();
                if ((*cb)(array, count, file, entry))
                    count++;
            }
        } catch (...) { }

        result = tTJSVariant(array, array);
        array->Release();

        return result;
    }

    static bool setDirListFile(iTJSDispatch2* array,
                               tjs_int count,
                               const ttstr& file,
                               const std::filesystem::directory_entry& entry)
    {
        // [dirlist] 配列に追加する
        tTJSVariant val(file);
        array->PropSetByNum(0, count, &val, array);
        return true;
    }

    static bool setDirListInfo(iTJSDispatch2* array,
                               tjs_int count,
                               const ttstr& file,
                               const std::filesystem::directory_entry& entry)
    {
        iTJSDispatch2* dict = TJSCreateDictionaryObject();
        if (!dict)
            return false;

        try
        {
            // 文件名
            tTJSVariant vname = file;
            dict->PropSet(TJS_MEMBERENSURE, TJS_N("name"), nullptr, &vname, dict);

            // 文件大小
            int64_t size = entry.is_regular_file() ? entry.file_size() : 0;
            tTJSVariant vsize = size;
            dict->PropSet(TJS_MEMBERENSURE, TJS_N("size"), nullptr, &vsize, dict);

            // 文件属性（权限 & 类型）
            int32_t attrib = static_cast<int32_t>(entry.status().permissions());
            tTJSVariant vattr = attrib;
            dict->PropSet(TJS_MEMBERENSURE, TJS_N("attrib"), nullptr, &vattr, dict);

            // 文件时间
            auto ftime = entry.last_write_time(); // mtime
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - decltype(ftime)::clock::now() + std::chrono::system_clock::now());
            int64_t mtime = std::chrono::system_clock::to_time_t(sctp);

            tTJSVariant vmtime = mtime;
            dict->PropSet(TJS_MEMBERENSURE, TJS_N("mtime"), nullptr, &vmtime, dict);

            // Unix 没有直接的创建时间，ctime 用 st_ctime
            // 或文件系统支持的创建时间
            tTJSVariant vctime = static_cast<tjs_int64>(mtime);
            dict->PropSet(TJS_MEMBERENSURE, TJS_N("ctime"), nullptr, &vctime, dict);

            // atime
            // 如果系统支持 nanoseconds: use std::filesystem::last_access_time
            // (C++23) 或 stat
            tTJSVariant vatime = mtime; // 可以用 mtime 代替
            dict->PropSet(TJS_MEMBERENSURE, TJS_N("atime"), nullptr, &vatime, dict);

            tTJSVariant val(dict, dict);
            array->PropSetByNum(0, count, &val, array);

            dict->Release();
        }
        catch (...)
        {
            dict->Release();
            throw;
        }
        return true;
    }

public:
    /**
     * 指定ディレクトリを削除する
     * @param dir ディレクトリ名
     * @return 実際に削除されたら true
     * 中にファイルが無い場合のみ削除されます
     */
    static bool removeDirectory(ttstr dir)
    {

        if (dir.GetLastChar() != TJS_N('/'))
        {
            TVPThrowExceptionMessage(
                TJS_N("'/' must be specified at the end of given directory name."));
        }

        // OSネイティブな表現に変換
        dir = TVPNormalizeStorageName(dir);
        TVPGetLocalName(dir);

        const bool r = TVPDeleteFile(dir.AsStdString());
        if (!r)
        {
            TVPAddLog(ttstr(TJS_N("removeDirectory : ")) + dir + TJS_N(" failed."));
        }
        return r;
    }

    /**
     * ディレクトリの作成
     * @param dir ディレクトリ名
     * @return 実際に作成できたら true
     */
    static bool createDirectory(ttstr dir)
    {
        if (dir.GetLastChar() != TJS_N('/'))
        {
            TVPThrowExceptionMessage(
                TJS_N("'/' must be specified at the end of given directory name."));
        }
        dir = TVPNormalizeStorageName(dir);
        TVPGetLocalName(dir);
        const bool r = std::filesystem::create_directories(dir.AsStdString());
        if (!r)
        {
            TVPAddLog(ttstr(TJS_N("createDirectory : ")) + dir + TJS_N(" failed."));
        }
        return r;
    }

    /**
     * ディレクトリの作成
     * @param dir ディレクトリ名
     * @return 実際に作成できたら true
     */
    static bool createDirectoryNoNormalize(ttstr dir)
    {
        if (dir.GetLastChar() != TJS_N('/'))
        {
            TVPThrowExceptionMessage(
                TJS_N("'/' must be specified at the end of given directory name."));
        }
        TVPGetLocalName(dir);
        const bool r = std::filesystem::create_directories(dir.AsStdString());
        if (!r)
        {
            TVPAddLog(ttstr(TJS_N("createDirectory : ")) + dir + TJS_N(" failed."));
        }
        return r;
    }

    /**
     * カレントディレクトリの変更
     * @param dir ディレクトリ名
     * @return 実際に作成できたら true
     */
    static bool changeDirectory(ttstr dir)
    {
        // unsupport operation
        return false;
    }

    /**
     * ファイルの属性を設定する
     * @param filename ファイル/ディレクトリ名
     * @param attr 設定する属性
     * @return 実際に変更できたら true
     */
    static bool setFileAttributes(ttstr filename, tjs_uint16 attr)
    {
        filename = TVPNormalizeStorageName(filename);
        TVPGetLocalName(filename);

        try
        {
            std::filesystem::path filepath(filename.c_str());
            if (!std::filesystem::exists(filepath))
            {
                return false;
            }
            std::filesystem::permissions(filepath, std::filesystem::perms::all);

            // 设置只读属性
            if (attr & 0x01)
            {
                std::filesystem::permissions(filepath,
                                             std::filesystem::perms::owner_read |
                                                 std::filesystem::perms::group_read |
                                                 std::filesystem::perms::others_read,
                                             std::filesystem::perm_options::remove);
            }
        }
        catch (const std::filesystem::filesystem_error&)
        {
            return false;
        }
        return true;
    }

    /**
     * ファイルの属性を解除する
     * @param filename ファイル/ディレクトリ名
     * @param attr 解除する属性
     * @return 実際に変更できたら true
     */
    static bool resetFileAttributes(ttstr filename, tjs_uint16 attr)
    {
        filename = TVPNormalizeStorageName(filename);
        TVPGetLocalName(filename);

        try
        {
            std::filesystem::path filepath(filename.c_str());
            if (!std::filesystem::exists(filepath))
            {
                return false;
            }

            std::filesystem::permissions(filepath,
                                         std::filesystem::perms::owner_all |
                                             std::filesystem::perms::group_all |
                                             std::filesystem::perms::others_all,
                                         std::filesystem::perm_options::replace);
        }
        catch (const std::filesystem::filesystem_error&)
        {
            return false;
        }
        return true;
    }

    /**
     * ファイルの属性を取得する
     * @param filename ファイル/ディレクトリ名
     * @return 取得した属性
     */
    static tjs_uint16 getFileAttributes(ttstr filename)
    {
        filename = TVPNormalizeStorageName(filename);
        TVPGetLocalName(filename);

        tjs_uint16 attr = 0;

        try
        {
            std::filesystem::path filepath(filename.c_str());

            if (!std::filesystem::exists(filepath))
            {
                return 0xFFFF;
            }

            if (std::filesystem::is_directory(filepath))
            {
                attr |= 0x10;
            }

            auto perms = std::filesystem::status(filepath).permissions();
            if ((perms & std::filesystem::perms::owner_write) == std::filesystem::perms::none &&
                (perms & std::filesystem::perms::group_write) == std::filesystem::perms::none &&
                (perms & std::filesystem::perms::others_write) == std::filesystem::perms::none)
            {
                attr |= 0x01;
            }
        }
        catch (const std::filesystem::filesystem_error&)
        {
            return 0xFFFF;
        }
        return attr;
    }

    /**
     * フォルダ選択ダイアログを開く
     * @param window ウィンドウ
     * @param caption キャプション
     * @param initialDir 初期ディレクトリ
     * @param rootDir ルートディレクトリ
     */
    static tjs_error selectDirectory(tTJSVariant* result,
                                     tjs_int numparams,
                                     tTJSVariant** param,
                                     iTJSDispatch2* objthis)
    {
        if (numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        tTJSVariant val;
        std::string title;
        std::string initialdir;
        std::string rootDir;

        // title
        iTJSDispatch2* dsp = param[0]->AsObjectThisNoAddRef();
        if (TJS_SUCCEEDED(dsp->PropGet(TJS_MEMBERMUSTEXIST, TJS_N("caption"), 0, &val, dsp)))
        {
            title = val.AsStringNoAddRef()->operator const tjs_char*();
        }
        // initial dir
        if (TJS_SUCCEEDED(dsp->PropGet(TJS_MEMBERMUSTEXIST, TJS_N("initialDir"), 0, &val, dsp)))
        {
            ttstr lname(val);
            if (!lname.IsEmpty())
            {
                TVPGetLocalName(lname);
                initialdir = lname.c_str();
            }
        }
        // rootDir
        if (TJS_SUCCEEDED(dsp->PropGet(TJS_MEMBERMUSTEXIST, TJS_N("rootDir"), 0, &val, dsp)))
        {
            ttstr lname(val);
            if (!lname.IsEmpty())
            {
                TVPGetLocalName(lname);
                initialdir = lname.c_str();
            }
        }

        // show dialog box
        std::string retDir = TVPShowDirectorySelector(title, initialdir, rootDir);

        if (!retDir.empty())
        {
            // returns some informations
            ttstr tresult = TVPNormalizeStorageName(ttstr(retDir.c_str()));
            val = tresult;
            dsp->PropSet(TJS_MEMBERENSURE, TJS_N("name"), 0, &val, dsp);
            return TJS_S_OK;
        }

        return TJS_S_OK;
    }

    /**
     * ディレクトリの存在チェック
     * @param directory ディレクトリ名
     * @return ディレクトリが存在すれば true/存在しなければ
     * -1/ディレクトリでなければ false
     */
    static int isExistentDirectory(ttstr dir)
    {
        if (dir.GetLastChar() != TJS_N('/'))
        {
            TVPThrowExceptionMessage(
                TJS_N("'/' must be specified at the end of given directory name."));
        }
        dir = TVPNormalizeStorageName(dir);
        TVPGetLocalName(dir);
        const std::string& p = dir.AsStdString();
        return std::filesystem::exists(p) && std::filesystem::is_directory(p);
    }

    /**
     * 吉里吉里のストレージ空間中の指定ファイルをコピーする
     * @param from コピー元ファイル
     * @param to コピー先ファイル
     * @return 実際に移動できたら true
     */
    static bool copyFile(const tjs_char* from, const tjs_char* to)
    {
        try
        {
            ttstr fromFile(TVPGetLocallyAccessibleName(TVPGetPlacedPath(from)));
            ttstr toFile(TVPGetLocallyAccessibleName(TVPNormalizeStorageName(to)));
            return _copyFile(fromFile, toFile);
        }
        catch (...)
        {
            return false;
        }
    }

private:
    static bool _copyFile(const ttstr& from, const ttstr& to)
    {
        if (from.length() && to.length())
        {
            if (TVPCopyFile(from.AsStdString(), to.AsStdString()))
            {
                TVPClearStorageCaches();
                return true;
            }
        }
        return false;
    }

public:
    /**
     * パスの正規化を行わず吉里吉里のストレージ空間中の指定ファイルをコピーする
     * @param from コピー元ファイル
     * @param to コピー先ファイル
     * @param failIfExist ファイルが存在するときに失敗するなら
     * ture、上書きするなら false
     * @return 実際に移動できたら true
     */
    static bool copyFileNoNormalize(const tjs_char* from, const tjs_char* to, bool failIfExist)
    {
        ttstr fromFile(TVPGetLocallyAccessibleName(TVPGetPlacedPath(from)));
        ttstr toFile(to);
        if (toFile.length())
        {
            // ※指定次第で例外を発生させるためTVPGetLocallyAccessibleNameは使わない
            TVPGetLocalName(toFile);
            return _copyFile(fromFile, toFile);
        }
        return false;
    }

    /**
     * パスの正規化を行なわず、autoPathからの検索も行なわずに
     * ファイルの存在確認を行う
     * @param fileame ファイルパス
     * @return ファイルが存在したらtrue
     */
    static bool isExistentStorageNoSearchNoNormalize(ttstr filename)
    {
        return TVPIsExistentStorageNoSearchNoNormalize(filename);
    }

    /**
     * 表示名取得
     * @param fileame ファイルパス
     */
    static ttstr getDisplayName(ttstr filename)
    {
        filename = TVPNormalizeStorageName(filename);
        TVPGetLocalName(filename);
        std::filesystem::path p = filename.AsStdString();
        if (!std::filesystem::exists(p))
            return "";                // 文件不存在返回空
        return p.filename().string(); // 返回文件名部分
    }

    /**
     * MD5ハッシュ値の取得
     * @param filename 対象ファイル名
     * @return ハッシュ値（32文字の16進数ハッシュ文字列（小文字））
     */
    static tjs_error getMD5HashString(tTJSVariant* result,
                                      tjs_int numparams,
                                      tTJSVariant** param,
                                      iTJSDispatch2*)
    {
        if (numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        const ttstr& filename = TVPGetPlacedPath(*param[0]);
        tTJSBinaryStream* in = TVPCreateBinaryStreamForRead(filename, "");
        if (!in)
            TVPThrowExceptionMessage(
                (ttstr(TJS_N("cannot open : ")) + param[0]->GetString()).c_str());

        TVP_md5_state_t st;
        TVP_md5_init(&st);

        tjs_uint8 buffer[1024]; // > 16 digestバッファ兼ねる
        uint16_t size = 0;
        while ((size = in->Read(buffer, sizeof buffer)) > 0)
        {
            TVP_md5_append(&st, buffer, (int)size);
        }
        delete in;

        TVP_md5_finish(&st, buffer);

        tjs_char ret[32 + 1]{};
        static constexpr tjs_char hex[17] = TJS_N("0123456789abcdef");
        for (tjs_int i = 0; i < 16; i++)
        {
            ret[i * 2] = hex[buffer[i] >> 4 & 0xF];
            ret[i * 2 + 1] = hex[buffer[i] & 0xF];
        }
        ret[32] = 0;
        if (result)
            *result = ttstr(ret);
        return TJS_S_OK;
    }

    /**
     * パスの検索
     * @param filename   検索対象ファイル名
     * @param searchpath
     * 検索対象パス（ローカル表記(c:\\～等)で";"区切り，省略時はシステムのデフォルト検索パス）
     * @return
     * 見つからなかった場合はvoid，見つかった場合はファイルのフルパス(file://./～)
     */
    static tjs_error searchPath(tTJSVariant* result,
                                tjs_int numparams,
                                tTJSVariant** param,
                                iTJSDispatch2*)
    {
        if (numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        const ttstr filename(*param[0]);
        ttstr searchpath;
        if (TJS_PARAM_EXIST(1))
            searchpath = *param[1];
        if (const std::string& tmp = _searchPath(
                filename.AsStdString(), searchpath.length() ? searchpath.AsStdString() : "");
            tmp.length() > 0)
        {
            if (result)
                *result = TVPNormalizeStorageName(tmp);
        }
        else
        {
            // not found
            if (result)
                result->Clear();
        }
        return TJS_S_OK;
    }

    /*----------------------------------------------------------------------
     * カレントディレクトリ
     ----------------------------------------------------------------------*/
    static ttstr getCurrentPath()
    {
        const std::string crDir = TVPGetAppStoragePath().front();
        const ttstr result(crDir);
        return TVPNormalizeStorageName(result + TJS_N("\\"));
    }

    static void setCurrentPath(ttstr path) { changeDirectory(path); }
};

NCB_ATTACH_CLASS(StoragesFstat, Storages)
{
    NCB_METHOD(clearStorageCaches);
    RawCallback("fstat", &Class::fstat, TJS_STATICMEMBER);
    RawCallback("getTime", &Class::getTime, TJS_STATICMEMBER);
    RawCallback("setTime", &Class::setTime, TJS_STATICMEMBER);
    NCB_METHOD(getLastModifiedFileTime);
    NCB_METHOD(setLastModifiedFileTime);
    NCB_METHOD(exportFile);
    NCB_METHOD(deleteFile);
    NCB_METHOD(truncateFile);
    NCB_METHOD(moveFile);
    NCB_METHOD(dirlist);
    NCB_METHOD(dirlistEx);
    RawCallback("dirtree", &Class::dirtree, TJS_STATICMEMBER);
    NCB_METHOD(removeDirectory);
    NCB_METHOD(createDirectory);
    NCB_METHOD(createDirectoryNoNormalize);
    NCB_METHOD(changeDirectory);
    NCB_METHOD(setFileAttributes);
    NCB_METHOD(resetFileAttributes);
    NCB_METHOD(getFileAttributes);
    RawCallback("selectDirectory", &Class::selectDirectory, TJS_STATICMEMBER);
    NCB_METHOD(isExistentDirectory);
    NCB_METHOD(copyFile);
    NCB_METHOD(copyFileNoNormalize);
    NCB_METHOD(isExistentStorageNoSearchNoNormalize);
    NCB_METHOD(getDisplayName);
    RawCallback("getMD5HashString", &Class::getMD5HashString, TJS_STATICMEMBER);
    RawCallback("searchPath", &Class::searchPath, TJS_STATICMEMBER);
    Property("currentPath", &Class::getCurrentPath, &Class::setCurrentPath);
    Method(TJS_N("getTemporaryName"), &TVPGetTemporaryName);
};

// テンポラリファイル処理用クラス
class TemporaryFiles
{
};

NCB_REGISTER_CLASS(TemporaryFiles)
{
}

/**
 * 登録処理後
 */
static void PostRegistCallback()
{
    tTJSVariant var;
    TVPExecuteExpression(TJS_N("Date"), &var);
    dateClass = var.AsObject();
    var.Clear();
    TVPExecuteExpression(TJS_N("Date.setTime"), &var);
    dateSetTime = var.AsObject();
    var.Clear();
    TVPExecuteExpression(TJS_N("Date.getTime"), &var);
    dateGetTime = var.AsObject();
    var.Clear();
    TVPExecuteExpression(StoragesFstatPreScript);
}

#define RELEASE(name) \
    name->Release(); \
    name = NULL

/**
 * 開放処理前
 */
static void PreUnregistCallback()
{
    RELEASE(dateClass);
    RELEASE(dateSetTime);
}

NCB_POST_REGIST_CALLBACK(PostRegistCallback);
NCB_PRE_UNREGIST_CALLBACK(PreUnregistCallback);
