#ifndef LOAD_MEMORY

#include "psdclass.h"

void PSD::clearStream() {
    if(pStream) {
        delete pStream;
        pStream = NULL;
    }
    mStreamSize = 0;
    mBufferPos = 0;
    mBufferSize = 0;
}

unsigned char &PSD::getStreamValue(const tTVInteger &pos) {
    static unsigned char eof = 0;
    if(pos >= 0 && pos < mStreamSize) {
        if(pos >= mBufferPos && pos < mBufferPos + mBufferSize) {
            return mBuffer[pos - mBufferPos];
        }
        mBufferPos = pos;
        pStream->Seek(pos, TJS_BS_SEEK_SET);
        mBufferSize = pStream->Read(mBuffer, READ_BUFFER_SIZE);
        return mBuffer[0];
    }
    return eof;
}

void PSD::copyToBuffer(uint8_t *buf, tTVInteger pos, int size) {
    if(pos >= 0 && pos < mStreamSize) {
        if(size <= 16) {
            if(pos >= mBufferPos && pos + size < mBufferPos + mBufferSize) {
                memcpy(buf, &mBuffer[pos - mBufferPos], size);
                return;
            }
            mBufferPos = pos;
            pStream->Seek(pos, TJS_BS_SEEK_SET);
            mBufferSize = pStream->Read(mBuffer, READ_BUFFER_SIZE);
            memcpy(buf, mBuffer, size);
            return;
        }
        // 直接読み込む
        pStream->Seek(pos, TJS_BS_SEEK_SET);
        pStream->Read(buf, size);
    }
}

#endif
