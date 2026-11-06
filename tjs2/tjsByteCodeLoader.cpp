//---------------------------------------------------------------------------
/*
        TJS2 Script Engine( Byte Code )
        Copyright (c), Takenori Imoto

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include "tjs.h"
#include "tjsScriptBlock.h"
#include "tjsByteCodeLoader.h"
#include "tjsGlobalStringMap.h"

namespace TJS
{

bool tTJSByteCodeLoader::IsTJS2ByteCode(const tjs_uint8* buff)
{
    // TJS2
    int tag = read4byte(buff);
    if (tag != FILE_TAG_LE)
        return false;
    // 100'\0'
    int ver = read4byte(&(buff[4]));
    if (ver != VER_TAG_LE)
        return false;
    return true;
}
tTJSScriptBlock* tTJSByteCodeLoader::ReadByteCode(tTJS* owner,
                                                  const tjs_char* name,
                                                  const tjs_uint8* buf,
                                                  size_t size)
{
    if (!buf || size < 20 || size > 0x7fffffffu)
        return NULL;

    ReadBuffer = buf;
    ReadIndex = 0;
    ReadSize = (tjs_uint32)size;

    // TJS2 / 100
    if (read4byte(buf) != FILE_TAG_LE || read4byte(&buf[4]) != VER_TAG_LE)
        return NULL;

    int filesize = read4byte(&buf[8]);
    if (filesize < 0 || (size_t)filesize != size)
        return NULL;

    // DATA chunk: its size field includes the tag and the size word.
    if (read4byte(&buf[12]) != DATA_TAG_LE)
        return NULL;
    int datasize = read4byte(&buf[16]);
    if (datasize < 8 || (size_t)datasize > size - 12)
        return NULL;
    if (!ReadDataArea(buf, 20, (size_t)datasize - 8))
        return NULL;

    // OBJS chunk starts right after the DATA chunk.
    const size_t obj_start = 12 + (size_t)datasize;
    if (obj_start + 8 > size)
        return NULL;
    if (read4byte(&buf[obj_start]) != OBJ_TAG_LE)
        return NULL;
    int objsize = read4byte(&buf[obj_start + 4]);
    if (objsize < 8 || (size_t)objsize > size - obj_start)
        return NULL;

    tTJSScriptBlock* block = new tTJSScriptBlock(owner, name, 0);
    if (!ReadObjects(block, buf, (int)obj_start + 8, objsize - 8))
    {
        block->Release();
        return NULL;
    }
    return block;
}

bool tTJSByteCodeLoader::ReadDataArea(const tjs_uint8* buff, int offset, size_t size)
{
    if (offset < 0 || (size_t)offset > ReadSize || size > ReadSize - (size_t)offset)
        return false;
    const int end = offset + (int)size;

    auto remain = [&]() -> long long { return (long long)end - offset; };
    auto read32 = [&](int& value) -> bool {
        if (remain() < 4)
            return false;
        value = read4byte(&buff[offset]);
        offset += 4;
        return true;
    };
    auto skipAligned = [&](long long bytes) -> bool {
        long long aligned = (bytes + 3) & ~3LL;
        if (aligned < 0 || aligned > remain())
            return false;
        offset += (int)aligned;
        return true;
    };

    int count;
    if (!read32(count) || count < 0 || (long long)count > remain())
        return false;
    ByteArray.set((tjs_int8*)&buff[offset], count);
    if (!skipAligned(count))
        return false;

    if (!read32(count) || count < 0 || (long long)count * 2 > remain())
        return false;
    ShortArray.clear();
    ShortArray.reserve(count);
    for (int i = 0; i < count; i++)
    {
        ShortArray.push_back(read2byte(&buff[offset]));
        offset += 2;
    }
    if (count & 1)
    {
        if (remain() < 2)
            return false;
        offset += 2;
    }

    if (!read32(count) || count < 0 || (long long)count * 4 > remain())
        return false;
    LongArray.clear();
    LongArray.reserve(count);
    for (int i = 0; i < count; i++)
    {
        LongArray.push_back(read4byte(&buff[offset]));
        offset += 4;
    }

    if (!read32(count) || count < 0 || (long long)count * 8 > remain())
        return false;
    LongLongArray.clear();
    LongLongArray.reserve(count);
    for (int i = 0; i < count; i++)
    {
        LongLongArray.push_back(read8byte(&buff[offset]));
        offset += 8;
    }

    if (!read32(count) || count < 0 || (long long)count * 8 > remain())
        return false;
    DoubleArray.clear();
    DoubleArray.reserve(count);
    for (int i = 0; i < count; i++)
    {
        tjs_uint64 tmp = read8byte(&buff[offset]);
        DoubleArray.push_back(*(double*)&tmp);
        offset += 8;
    }

    if (!read32(count) || count < 0 || (long long)count * 4 > remain())
        return false;
    StringArray.clear();
    StringArray.reserve(count);
    for (int i = 0; i < count; i++)
    {
        int len;
        if (!read32(len) || len < 0 || (long long)len * 2 > remain())
            return false;
        std::vector<tjs_char> ch((size_t)len * 3 + 1);
        size_t currLen = 0;
        for (int j = 0; j < len; j++)
        {
            if (remain() < 2)
                return false;
            tjs_uint16 tmpCh = read2byte(&buff[offset]);
            offset += 2;
            int chLen = (int)TVPWideCharToUtf8(tmpCh, NULL);
            if (chLen <= 0 || currLen + (size_t)chLen + 1 > ch.size())
                return false;
            TVPWideCharToUtf8(tmpCh, ch.data() + currLen);
            currLen += (size_t)chLen;
        }
        ch[currLen] = 0;
        StringArray.push_back(TJSMapGlobalStringMap(&(ch[0])));
        if (len & 1)
        {
            if (remain() < 2)
                return false;
            offset += 2;
        }
    }

    if (!read32(count) || count < 0 || (long long)count * 4 > remain())
        return false;
    OctetArray.clear();
    OctetArray.reserve(count);
    for (int i = 0; i < count; i++)
    {
        int len;
        if (!read32(len) || len < 0 || (long long)len > remain())
            return false;
        OctetArray.push_back(new tTJSVariantOctet(&buff[offset], len));
        offset += len;
        if (!skipAligned(len))
            return false;
    }

    return true;
}

bool tTJSByteCodeLoader::ReadObjects(tTJSScriptBlock* block,
                                     const tjs_uint8* buff,
                                     int offset,
                                     int size)
{
    if (offset < 0 || size < 0 || (size_t)offset > ReadSize ||
        (size_t)size > ReadSize - (size_t)offset)
        return false;
    const int end = offset + size;

    auto remain = [&]() -> long long { return (long long)end - offset; };
    auto read32 = [&](int& value) -> bool {
        if (remain() < 4)
            return false;
        value = read4byte(&buff[offset]);
        offset += 4;
        return true;
    };

    int toplevel;
    int objcount;
    if (!read32(toplevel) || !read32(objcount))
        return false;
    if (toplevel < -1 || objcount < 0 || toplevel >= objcount)
        return false;
    if ((long long)objcount * 64 > remain())
        return false;

    std::vector<tTJSInterCodeContext*> objs(objcount);
    std::vector<VariantRepalace> work;
    std::vector<int> parent(objcount, -1);
    std::vector<int> propSetter(objcount, -1);
    std::vector<int> propGetter(objcount, -1);
    std::vector<int> superClassGetter(objcount, -1);
    std::vector<std::vector<int>> properties(objcount);

    for (int o = 0; o < objcount; o++)
    {
        tTJSInterCodeContext::tSourcePos* srcPos = NULL;
        tjs_int32* code = NULL;
        tTJSVariant* vdata = NULL;
        auto cleanup = [&]() {
            delete[] srcPos;
            if (code)
                TJS_free(code);
            delete[] vdata;
        };

        int tag;
        int objsize;
        if (!read32(tag) || !read32(objsize))
            return false;
        if (tag != FILE_TAG_LE || objsize < 0)
            return false;
        if ((long long)objsize > remain())
            return false;

        int name32, contextType, maxVariableCount, variableReserveCount, maxFrameCount;
        int funcDeclArgCount, funcDeclUnnamedArgArrayBase, funcDeclCollapseBase;
        if (!read32(parent[o]) || !read32(name32) || !read32(contextType) ||
            !read32(maxVariableCount) || !read32(variableReserveCount) || !read32(maxFrameCount) ||
            !read32(funcDeclArgCount) || !read32(funcDeclUnnamedArgArrayBase) ||
            !read32(funcDeclCollapseBase) || !read32(propSetter[o]) || !read32(propGetter[o]) ||
            !read32(superClassGetter[o]))
            return false;
        if (name32 < 0 || (size_t)name32 >= StringArray.size())
            return false;

        int count;
        if (!read32(count) || count < 0 || (long long)count * 8 > remain())
            return false;
        tjs_int srcPosArraySize = 0;
        if (count > 0)
        {
            srcPos = new tTJSInterCodeContext::tSourcePos[count];
            srcPosArraySize = count;
            for (int i = 0; i < count; i++)
            {
                int value;
                if (!read32(value))
                {
                    cleanup();
                    return false;
                }
                srcPos[i].CodePos = value;
            }
            for (int i = 0; i < count; i++)
            {
                int value;
                if (!read32(value))
                {
                    cleanup();
                    return false;
                }
                srcPos[i].SourcePos = value;
            }
        }

        if (!read32(count) || count < 0 || (long long)count * 2 > remain())
        {
            cleanup();
            return false;
        }
        const tjs_int codeSize = count;
        code = (tjs_int32*)TJS_malloc(((size_t)count + 16) * sizeof(tjs_int32));
        if (!code)
        {
            cleanup();
            return false;
        }
        for (int i = 0; i < count; i++)
        {
            if (remain() < 2)
            {
                cleanup();
                return false;
            }
            code[i] = (tjs_int16)read2byte(&buff[offset]);
            offset += 2;
        }
        for (int i = count; i < count + 16; i++)
            code[i] = 0;
        if (count & 1)
        {
            if (remain() < 2)
            {
                cleanup();
                return false;
            }
            offset += 2;
        }
        TranslateCodeAddress(block, code, codeSize);

        if (!read32(count) || count < 0 || (long long)count * 4 > remain())
        {
            cleanup();
            return false;
        }
        int vcount = count << 1;
        std::vector<short> data((size_t)vcount);
        for (int i = 0; i < vcount; i++)
        {
            if (remain() < 2)
            {
                cleanup();
                return false;
            }
            data[(size_t)i] = (short)read2byte(&buff[offset]);
            offset += 2;
        }

        vdata = new tTJSVariant[count];
        const tjs_int datacount = count;
        for (int i = 0; i < datacount; i++)
        {
            int pos = i << 1;
            int type = data[(size_t)pos];
            int index = data[(size_t)pos + 1];
            switch (type)
            {
                case TYPE_VOID:
                    vdata[i].Clear();
                    break;
                case TYPE_OBJECT:
                    vdata[i] = (iTJSDispatch2*)NULL;
                    break;
                case TYPE_INTER_OBJECT:
                case TYPE_INTER_GENERATOR:
                    vdata[i].Clear();
                    work.push_back(VariantRepalace(&(vdata[i]), index));
                    break;
                case TYPE_STRING:
                    if (index >= 0 && (size_t)index < StringArray.size())
                        vdata[i] = StringArray[(size_t)index].c_str();
                    else
                        vdata[i].Clear();
                    break;
                case TYPE_OCTET:
                    if (index >= 0 && (size_t)index < OctetArray.size())
                        vdata[i] = OctetArray[(size_t)index];
                    else
                        vdata[i].Clear();
                    break;
                case TYPE_REAL:
                    if (index >= 0 && (size_t)index < DoubleArray.size())
                        vdata[i] = (tjs_real)DoubleArray[(size_t)index];
                    else
                        vdata[i].Clear();
                    break;
                case TYPE_BYTE:
                    if (index >= 0 && (size_t)index < ByteArray.size())
                        vdata[i] = (tjs_int)ByteArray[index];
                    else
                        vdata[i].Clear();
                    break;
                case TYPE_SHORT:
                    if (index >= 0 && (size_t)index < ShortArray.size())
                        vdata[i] = (tjs_int)ShortArray[(size_t)index];
                    else
                        vdata[i].Clear();
                    break;
                case TYPE_INTEGER:
                    if (index >= 0 && (size_t)index < LongArray.size())
                        vdata[i] = (tjs_int)LongArray[(size_t)index];
                    else
                        vdata[i].Clear();
                    break;
                case TYPE_LONG:
                    if (index >= 0 && (size_t)index < LongLongArray.size())
                        vdata[i] = (tjs_int64)LongLongArray[(size_t)index];
                    else
                        vdata[i].Clear();
                    break;
                case TYPE_UNKNOWN:
                default:
                    vdata[i].Clear();
                    break;
            }
        }

        if (!read32(count) || count < 0 || (long long)count * 4 > remain())
        {
            cleanup();
            return false;
        }
        std::vector<tjs_int> scgetterps((size_t)count);
        for (int i = 0; i < count; i++)
        {
            int value;
            if (!read32(value))
            {
                cleanup();
                return false;
            }
            if (value < -1 || value >= objcount)
            {
                cleanup();
                return false;
            }
            scgetterps[(size_t)i] = value;
        }

        if (!read32(count) || count < 0 || (long long)count * 8 > remain())
        {
            cleanup();
            return false;
        }
        if (count > 0)
        {
            int pcount = count << 1;
            std::vector<int>& props = properties[o];
            props.resize((size_t)pcount);
            for (int i = 0; i < pcount; i++)
            {
                int value;
                if (!read32(value))
                {
                    cleanup();
                    return false;
                }
                props[(size_t)i] = value;
            }
        }

        tTJSInterCodeContext* obj = new tTJSInterCodeContext(
            block, StringArray[(size_t)name32].c_str(), (tTJSContextType)contextType, code,
            codeSize, vdata, datacount, maxVariableCount, variableReserveCount, maxFrameCount,
            funcDeclArgCount, funcDeclUnnamedArgArrayBase, funcDeclCollapseBase, true, srcPos,
            srcPosArraySize, scgetterps);
        objs[o] = obj;
        srcPos = NULL;
        code = NULL;
        vdata = NULL;
    }

    for (int o = 0; o < objcount; o++)
    {
        if (parent[o] < -1 || parent[o] >= objcount || propSetter[o] < -1 ||
            propSetter[o] >= objcount || propGetter[o] < -1 || propGetter[o] >= objcount ||
            superClassGetter[o] < -1 || superClassGetter[o] >= objcount)
            return false;
    }

    tTJSVariant val;
    for (int o = 0; o < objcount; o++)
    {
        tTJSInterCodeContext* parentObj = parent[o] >= 0 ? objs[parent[o]] : NULL;
        tTJSInterCodeContext* propSetterObj = propSetter[o] >= 0 ? objs[propSetter[o]] : NULL;
        tTJSInterCodeContext* propGetterObj = propGetter[o] >= 0 ? objs[propGetter[o]] : NULL;
        tTJSInterCodeContext* superClassGetterObj =
            superClassGetter[o] >= 0 ? objs[superClassGetter[o]] : NULL;
        objs[o]->SetCodeObject(parentObj, propSetterObj, propGetterObj, superClassGetterObj);

        if (!properties[o].empty())
        {
            tTJSInterCodeContext* obj = parentObj;
            std::vector<int>& prop = properties[o];
            int length = (int)(prop.size() >> 1);
            for (int i = 0; i < length; i++)
            {
                int pos = i << 1;
                int pname = prop[(size_t)pos];
                int pobj = prop[(size_t)pos + 1];
                if (!obj || pname < 0 || (size_t)pname >= StringArray.size() || pobj < 0 ||
                    pobj >= objcount)
                    continue;
                val = objs[pobj];
                obj->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP, StringArray[(size_t)pname].c_str(),
                             NULL, &val, obj);
            }
        }
    }
    int count = (int)work.size();
    for (int i = 0; i < count; i++)
    {
        VariantRepalace& w = work[(size_t)i];
        if (w.Index >= 0 && w.Index < objcount)
            (*w.Work) = objs[w.Index];
    }
    work.clear();
    tTJSInterCodeContext* top = toplevel >= 0 ? objs[toplevel] : NULL;
    block->SetObjects(top, objs, objcount);
    return true;
}

#define TJS_OFFSET_VM_REG_ADDR(x) ((x) = TJS_TO_VM_REG_ADDR(x))
#define TJS_OFFSET_VM_CODE_ADDR(x) ((x) = TJS_TO_VM_CODE_ADDR(x))
/**
 * バイトコード中のアドレスは配列のインデックスを指しているので、それをアドレスに変換する
 */
void tTJSByteCodeLoader::TranslateCodeAddress(tTJSScriptBlock* block,
                                              tjs_int32* code,
                                              const tjs_int32 codeSize)
{
    tjs_int i = 0;
    for (; i < codeSize;)
    {
        tjs_int size;
        switch (code[i])
        {
            case VM_NOP:
                size = 1;
                break;
            case VM_NF:
                size = 1;
                break;
            case VM_CONST:
                TJS_OFFSET_VM_REG_ADDR(code[i + 1]);
                TJS_OFFSET_VM_REG_ADDR(code[i + 2]);
                size = 3;
                break;

#define OP2_DISASM(c) \
    case c: \
        TJS_OFFSET_VM_REG_ADDR(code[i + 1]); \
        TJS_OFFSET_VM_REG_ADDR(code[i + 2]); \
        size = 3; \
        break

                OP2_DISASM(VM_CP);
                OP2_DISASM(VM_CEQ);
                OP2_DISASM(VM_CDEQ);
                OP2_DISASM(VM_CLT);
                OP2_DISASM(VM_CGT);
                OP2_DISASM(VM_CHKINS);
#undef OP2_DISASM

#define OP2_DISASM(c) \
    case c: \
        TJS_OFFSET_VM_REG_ADDR(code[i + 1]); \
        TJS_OFFSET_VM_REG_ADDR(code[i + 2]); \
        size = 3; \
        break; \
    case c + 1: \
        TJS_OFFSET_VM_REG_ADDR(code[i + 1]); \
        TJS_OFFSET_VM_REG_ADDR(code[i + 2]); \
        TJS_OFFSET_VM_REG_ADDR(code[i + 3]); \
        TJS_OFFSET_VM_REG_ADDR(code[i + 4]); \
        size = 5; \
        break; \
    case c + 2: \
        TJS_OFFSET_VM_REG_ADDR(code[i + 1]); \
        TJS_OFFSET_VM_REG_ADDR(code[i + 2]); \
        TJS_OFFSET_VM_REG_ADDR(code[i + 3]); \
        TJS_OFFSET_VM_REG_ADDR(code[i + 4]); \
        size = 5; \
        break; \
    case c + 3: \
        TJS_OFFSET_VM_REG_ADDR(code[i + 1]); \
        TJS_OFFSET_VM_REG_ADDR(code[i + 2]); \
        TJS_OFFSET_VM_REG_ADDR(code[i + 3]); \
        size = 4; \
        break

                OP2_DISASM(VM_LOR);
                OP2_DISASM(VM_LAND);
                OP2_DISASM(VM_BOR);
                OP2_DISASM(VM_BXOR);
                OP2_DISASM(VM_BAND);
                OP2_DISASM(VM_SAR);
                OP2_DISASM(VM_SAL);
                OP2_DISASM(VM_SR);
                OP2_DISASM(VM_ADD);
                OP2_DISASM(VM_SUB);
                OP2_DISASM(VM_MOD);
                OP2_DISASM(VM_DIV);
                OP2_DISASM(VM_IDIV);
                OP2_DISASM(VM_MUL);
#undef OP2_DISASM

#define OP1_DISASM \
    TJS_OFFSET_VM_REG_ADDR(code[i + 1]); \
    size = 2;
            case VM_TT:
                OP1_DISASM;
                break;
            case VM_TF:
                OP1_DISASM;
                break;
            case VM_SETF:
                OP1_DISASM;
                break;
            case VM_SETNF:
                OP1_DISASM;
                break;
            case VM_LNOT:
                OP1_DISASM;
                break;
            case VM_BNOT:
                OP1_DISASM;
                break;
            case VM_ASC:
                OP1_DISASM;
                break;
            case VM_CHR:
                OP1_DISASM;
                break;
            case VM_NUM:
                OP1_DISASM;
                break;
            case VM_CHS:
                OP1_DISASM;
                break;
            case VM_CL:
                OP1_DISASM;
                break;
            case VM_INV:
                OP1_DISASM;
                break;
            case VM_CHKINV:
                OP1_DISASM;
                break;
            case VM_TYPEOF:
                OP1_DISASM;
                break;
            case VM_EVAL:
                OP1_DISASM;
                break;
            case VM_EEXP:
                OP1_DISASM;
                break;
            case VM_INT:
                OP1_DISASM;
                break;
            case VM_REAL:
                OP1_DISASM;
                break;
            case VM_STR:
                OP1_DISASM;
                break;
            case VM_OCTET:
                OP1_DISASM;
                break;
#undef OP1_DISASM

            case VM_CCL:
                TJS_OFFSET_VM_REG_ADDR(code[i + 1]);
                size = 3;
                break;

#define OP1_DISASM(c) \
    case c: \
        TJS_OFFSET_VM_REG_ADDR(code[i + 1]); \
        size = 2; \
        break; \
    case c + 1: \
        TJS_OFFSET_VM_REG_ADDR(code[i + 1]); \
        TJS_OFFSET_VM_REG_ADDR(code[i + 2]); \
        TJS_OFFSET_VM_REG_ADDR(code[i + 3]); \
        size = 4; \
        break; \
    case c + 2: \
        TJS_OFFSET_VM_REG_ADDR(code[i + 1]); \
        TJS_OFFSET_VM_REG_ADDR(code[i + 2]); \
        TJS_OFFSET_VM_REG_ADDR(code[i + 3]); \
        size = 4; \
        break; \
    case c + 3: \
        TJS_OFFSET_VM_REG_ADDR(code[i + 1]); \
        TJS_OFFSET_VM_REG_ADDR(code[i + 2]); \
        size = 3; \
        break

                OP1_DISASM(VM_INC);
                OP1_DISASM(VM_DEC);
#undef OP1_DISASM

#define OP1A_DISASM \
    TJS_OFFSET_VM_CODE_ADDR(code[i + 1]); \
    size = 2;
            case VM_JF:
                OP1A_DISASM;
                break;
            case VM_JNF:
                OP1A_DISASM;
                break;
            case VM_JMP:
                OP1A_DISASM;
                break;
#undef OP1A_DISASM

            case VM_CALL:
            case VM_CALLD:
            case VM_CALLI:
            case VM_NEW:
            {
                TJS_OFFSET_VM_REG_ADDR(code[i + 1]);
                TJS_OFFSET_VM_REG_ADDR(code[i + 2]);

                tjs_int st; // start of arguments
                if (code[i] == VM_CALLD || code[i] == VM_CALLI)
                {
                    TJS_OFFSET_VM_REG_ADDR(code[i + 3]);
                    st = 5;
                }
                else
                {
                    st = 4;
                }
                if (i + st > codeSize)
                {
                    TJS_eTJSScriptError(TJSByteCodeBroken, block, 0);
                    return;
                }
                tjs_int num = code[i + st - 1]; // st-1 = argument count
                tjs_int c = 0;
                if (num == -1)
                {
                    size = st;
                }
                else if (num == -2)
                {
                    st++;
                    if (i + st > codeSize)
                    {
                        TJS_eTJSScriptError(TJSByteCodeBroken, block, 0);
                        return;
                    }
                    num = code[i + st - 1];
                    if (num < 0 || (long long)num * 2 > codeSize - (i + st))
                    {
                        TJS_eTJSScriptError(TJSByteCodeBroken, block, 0);
                        return;
                    }
                    size = st + num * 2;
                    for (tjs_int j = 0; j < num; j++)
                    {
                        switch (code[i + st + j * 2])
                        {
                            case fatNormal:
                                TJS_OFFSET_VM_REG_ADDR(code[i + st + j * 2 + 1]);
                                break;
                            case fatExpand:
                                TJS_OFFSET_VM_REG_ADDR(code[i + st + j * 2 + 1]);
                                break;
                            case fatUnnamedExpand:
                                break;
                        }
                    }
                }
                else if (num >= 0)
                {
                    // normal operation
                    if (num > codeSize - (i + st))
                    {
                        TJS_eTJSScriptError(TJSByteCodeBroken, block, 0);
                        return;
                    }
                    size = st + num;
                    while (num--)
                    {
                        TJS_OFFSET_VM_REG_ADDR(code[i + c + st]);
                        c++;
                    }
                }
                else
                {
                    TJS_eTJSScriptError(TJSByteCodeBroken, block, 0);
                    return;
                }
                break;
            }

            case VM_GPD:
            case VM_GPDS:
                TJS_OFFSET_VM_REG_ADDR(code[i + 1]);
                TJS_OFFSET_VM_REG_ADDR(code[i + 2]);
                TJS_OFFSET_VM_REG_ADDR(code[i + 3]);
                size = 4;
                break;

            case VM_SPD:
            case VM_SPDE:
            case VM_SPDEH:
            case VM_SPDS:
                TJS_OFFSET_VM_REG_ADDR(code[i + 1]);
                TJS_OFFSET_VM_REG_ADDR(code[i + 2]);
                TJS_OFFSET_VM_REG_ADDR(code[i + 3]);
                size = 4;
                break;

            case VM_GPI:
            case VM_GPIS:
                TJS_OFFSET_VM_REG_ADDR(code[i + 1]);
                TJS_OFFSET_VM_REG_ADDR(code[i + 2]);
                TJS_OFFSET_VM_REG_ADDR(code[i + 3]);
                size = 4;
                break;

            case VM_SPI:
            case VM_SPIE:
            case VM_SPIS:
                TJS_OFFSET_VM_REG_ADDR(code[i + 1]);
                TJS_OFFSET_VM_REG_ADDR(code[i + 2]);
                TJS_OFFSET_VM_REG_ADDR(code[i + 3]);
                size = 4;
                break;

            case VM_SETP:
                TJS_OFFSET_VM_REG_ADDR(code[i + 1]);
                TJS_OFFSET_VM_REG_ADDR(code[i + 2]);
                size = 3;
                break;

            case VM_GETP:
                TJS_OFFSET_VM_REG_ADDR(code[i + 1]);
                TJS_OFFSET_VM_REG_ADDR(code[i + 2]);
                size = 3;
                break;

            case VM_DELD:
            case VM_TYPEOFD:
                TJS_OFFSET_VM_REG_ADDR(code[i + 1]);
                TJS_OFFSET_VM_REG_ADDR(code[i + 2]);
                TJS_OFFSET_VM_REG_ADDR(code[i + 3]);
                size = 4;
                break;

            case VM_DELI:
            case VM_TYPEOFI:
                TJS_OFFSET_VM_REG_ADDR(code[i + 1]);
                TJS_OFFSET_VM_REG_ADDR(code[i + 2]);
                TJS_OFFSET_VM_REG_ADDR(code[i + 3]);
                size = 4;
                break;

            case VM_SRV:
                TJS_OFFSET_VM_REG_ADDR(code[i + 1]);
                size = 2;
                break;

            case VM_RET:
                size = 1;
                break;

            case VM_ENTRY:
                TJS_OFFSET_VM_CODE_ADDR(code[i + 1]);
                TJS_OFFSET_VM_REG_ADDR(code[i + 2]);
                size = 3;
                break;

            case VM_EXTRY:
                size = 1;
                break;

            case VM_THROW:
                TJS_OFFSET_VM_REG_ADDR(code[i + 1]);
                size = 2;
                break;

            case VM_CHGTHIS:
                TJS_OFFSET_VM_REG_ADDR(code[i + 1]);
                TJS_OFFSET_VM_REG_ADDR(code[i + 2]);
                size = 3;
                break;

            case VM_GLOBAL:
                TJS_OFFSET_VM_REG_ADDR(code[i + 1]);
                size = 2;
                break;

            case VM_ADDCI:
                TJS_OFFSET_VM_REG_ADDR(code[i + 1]);
                TJS_OFFSET_VM_REG_ADDR(code[i + 2]);
                size = 3;
                break;

            case VM_REGMEMBER:
                size = 1;
                break;
            case VM_DEBUGGER:
                size = 1;
                break;
            default:
                size = 1;
                break;
        } /* switch */
        if (size <= 0 || i + size > codeSize)
        {
            TJS_eTJSScriptError(TJSByteCodeBroken, block, 0);
            return;
        }
        i += size;
    }
    if (codeSize != i)
    {
        TJS_eTJSScriptError(TJSByteCodeBroken, block, 0);
    }
}

} // namespace TJS
