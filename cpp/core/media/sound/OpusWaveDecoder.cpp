#include "tjsCommHead.h"
#include "OpusWaveDecoder.h"

#include "TVPStorage.h"

#include "opus/opusfile.h"

class OpusWaveDecoder : public tTVPWaveDecoder // decoder interface
{
    OggOpusFile *InputFile; // OggOpusFile instance
    tTJSBinaryStream *InputStream; // input stream
    tTVPWaveFormat Format; // output PCM format
    opus_int16 *Buffer, *pBuffer;
    int BufferRemain; // in samples

private:
    int static read_func(void *datasource, unsigned char *ptr, int size) {
        OpusWaveDecoder *decoder = (OpusWaveDecoder *)datasource;
        if(!decoder->InputStream)
            return -1;
        return decoder->InputStream->Read(ptr, size);
    }
    int static seek_func(void *datasource, opus_int64 offset, int whence) {
        OpusWaveDecoder *decoder = (OpusWaveDecoder *)datasource;
        if(!decoder->InputStream)
            return -1;

        tjs_uint64 result;
        int seek_type = TJS_BS_SEEK_SET;

        switch(whence) {
            case SEEK_SET:
                seek_type = TJS_BS_SEEK_SET;
                break;
            case SEEK_CUR:
                seek_type = TJS_BS_SEEK_CUR;
                break;
            case SEEK_END:
                seek_type = TJS_BS_SEEK_END;
                break;
        }

        result = decoder->InputStream->Seek(offset, seek_type);
        return 0;
    }
    int static close_func(void *datasource) {
        OpusWaveDecoder *decoder = (OpusWaveDecoder *)datasource;
        if(!decoder->InputStream)
            return EOF;
        delete decoder->InputStream;
        decoder->InputStream = NULL;
        return 0;
    }
    opus_int64 static tell_func(void *datasource) {
        OpusWaveDecoder *decoder = (OpusWaveDecoder *)datasource;
        if(!decoder->InputStream)
            return EOF;
        return decoder->InputStream->GetPosition();
    }

public:
    OpusWaveDecoder() :
        InputFile(nullptr), InputStream(nullptr), Buffer(nullptr) {}
    ~OpusWaveDecoder() {
        if(InputFile) {
            op_free(InputFile);
            InputFile = nullptr;
        }
        if(InputStream) {
            delete InputStream;
        }
        if(Buffer) {
            delete[] Buffer;
        }
    }

    int BufferRead(opus_int16 *buf, int samples_to_read) {
        int res = std::min(BufferRemain, samples_to_read);
        memcpy(buf, pBuffer, res * Format.Channels * sizeof(opus_int16));
        pBuffer += res * Format.Channels;
        BufferRemain -= res;
        if(BufferRemain <= 0)
            pBuffer = nullptr;
        return res;
    }

public:
    // ITSSWaveDecoder
    virtual void GetFormat(tTVPWaveFormat &format) { format = Format; }
    virtual bool Render(void *_buf, tjs_uint bufsamplelen, tjs_uint &rendered) {
        // render output PCM
        if(!InputFile)
            return false; // InputFile is yet not inited
        opus_int16 *buf = (opus_int16 *)_buf;
        int res;
        int pos = 0; // decoded PCM (in sample)
        int remain = bufsamplelen; // remaining PCM (in sample)

        while(remain) {
            if(pBuffer) {
                res = BufferRead(buf, remain);
            } else if(remain >= 5760) {
                res = op_read(InputFile, buf, remain * Format.Channels,
                              nullptr); // decode via ov_read
            } else {
                res =
                    op_read(InputFile, Buffer, 5760 * Format.Channels, nullptr);
                if(res > remain) {
                    memcpy(buf, Buffer,
                           remain * Format.Channels * sizeof(opus_int16));
                    BufferRemain = res - remain;
                    pBuffer = Buffer + remain * Format.Channels;
                    res = remain;
                } else {
                    memcpy(buf, Buffer,
                           res * Format.Channels * sizeof(opus_int16));
                }
            }
            // if the decoding is not ready
            if(res <= 0)
                break;
            pos += res;
            remain -= res;
            buf += res * Format.Channels;
        }

        rendered = pos; // return rendered PCM samples

        return !remain; // if the decoding is ended
    }
    virtual bool SetPosition(tjs_uint64 samplepos) {
        // set PCM position (seek)
        if(!InputFile)
            return false;

        if(0 != op_pcm_seek(InputFile, samplepos)) {
            return false;
        }

        return true;
    }

    // others
    bool SetStream(const ttstr &url) {
        // set input stream
        InputStream = TVPCreateStream(url, TJS_BS_READ);
        if(!InputStream)
            return false;

        OpusFileCallbacks callbacks = { read_func, seek_func, tell_func,
                                        close_func };
        // callback functions

        // open input stream via ov_open_callbacks
        int ret;
        InputFile = op_open_callbacks(this, &callbacks, NULL, 0, &ret);
        if(!InputFile) {
            // error!
            delete InputStream;
            InputStream = NULL;
            return false;
        }

        // set Format up
        Format.BitsPerSample = 16;
        Format.BytesPerSample = Format.BitsPerSample / 8;
        Format.Channels = op_channel_count(InputFile, -1);
        Format.IsFloat = false;
        Format.SamplesPerSec = 48000;
        Format.Seekable = op_seekable(InputFile) ? 0 : 2;
        Format.SpeakerConfig = 0;
        // 		Format.Signed = true;
        // 		Format.BigEndian = false;

        ogg_int64_t pcmtotal = op_pcm_total(InputFile, -1); // PCM total samples
        if(pcmtotal < 0)
            pcmtotal = 0;
        Format.TotalSamples = pcmtotal;
        Format.TotalTime = pcmtotal * (1000.0 / 48000);

        Buffer = new opus_int16[Format.Channels * 5760]; // 120 ms in 48kHz
        BufferRemain = 0;
        pBuffer = nullptr;

        return true;
    }
};

tTVPWaveDecoder *OpusWaveDecoderCreator::Create(const ttstr &storagename,
                                                const ttstr &extension) {
    OpusWaveDecoder *decoder = nullptr;
    decoder = new OpusWaveDecoder();
    if(!decoder->SetStream(storagename)) {
        delete decoder;
        decoder = nullptr;
    }
    return decoder;
}
