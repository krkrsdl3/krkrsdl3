//---------------------------------------------------------------------------
/*
	TVP2 ( T Visual Presenter 2 )  A script authoring tool
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Wave Player interface
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include "WaveIntf.h"
#include "TVPMsg.h"
#include "UtilStreams.h"

#include "VorbisWaveDecoder.h"
#include "FFWaveDecoder.h"
#include "OpusWaveDecoder.h"


//---------------------------------------------------------------------------
// PCM related constants / definitions
//---------------------------------------------------------------------------

#ifndef WAVE_FORMAT_PCM
	#define WAVE_FORMAT_PCM 0x0001
#endif
#ifndef WAVE_FORMAT_IEEE_FLOAT
	#define WAVE_FORMAT_IEEE_FLOAT 0x0003
#endif
#ifndef WAVE_FORMAT_EXTENSIBLE
	#define WAVE_FORMAT_EXTENSIBLE 0xFFFE
#endif

#ifndef SPEAKER_FRONT_LEFT
// from Windows ksmedia.h
// speaker config

#define     SPEAKER_FRONT_LEFT              0x1
#define     SPEAKER_FRONT_RIGHT             0x2
#define     SPEAKER_FRONT_CENTER            0x4
#define     SPEAKER_LOW_FREQUENCY           0x8
#define     SPEAKER_BACK_LEFT               0x10
#define     SPEAKER_BACK_RIGHT              0x20
#define     SPEAKER_FRONT_LEFT_OF_CENTER    0x40
#define     SPEAKER_FRONT_RIGHT_OF_CENTER   0x80
#define     SPEAKER_BACK_CENTER             0x100
#define     SPEAKER_SIDE_LEFT               0x200
#define     SPEAKER_SIDE_RIGHT              0x400
#define     SPEAKER_TOP_CENTER              0x800
#define     SPEAKER_TOP_FRONT_LEFT          0x1000
#define     SPEAKER_TOP_FRONT_CENTER        0x2000
#define     SPEAKER_TOP_FRONT_RIGHT         0x4000
#define     SPEAKER_TOP_BACK_LEFT           0x8000
#define     SPEAKER_TOP_BACK_CENTER         0x10000
#define     SPEAKER_TOP_BACK_RIGHT          0x20000

#endif
//---------------------------------------------------------------------------






//---------------------------------------------------------------------------
// GUIDs used in WAVRFORMATEXTENSIBLE
//---------------------------------------------------------------------------
tjs_uint8 TVP_GUID_KSDATAFORMAT_SUBTYPE_PCM[16] =
{ 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
  0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 };
tjs_uint8 TVP_GUID_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT[16] =
{ 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
  0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 };
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// tTVPWD_RIFFWave
//---------------------------------------------------------------------------
class tTVPWD_RIFFWave : public tTVPWaveDecoder
{
	tTJSBinaryStream *Stream;
	tTVPWaveFormat Format;
	tjs_uint64 DataStart;
	tjs_uint64 CurrentPos;
	tjs_uint SampleSize;

public:
	tTVPWD_RIFFWave(tTJSBinaryStream *stream, tjs_uint64 datastart,
		const tTVPWaveFormat & format);
	~tTVPWD_RIFFWave();
	void GetFormat(tTVPWaveFormat & format);
	bool Render(void *buf, tjs_uint bufsamplelen, tjs_uint& rendered);
	bool SetPosition(tjs_uint64 samplepos);
};
//---------------------------------------------------------------------------
tTVPWD_RIFFWave::tTVPWD_RIFFWave(tTJSBinaryStream *stream, tjs_uint64 datastart,
	const tTVPWaveFormat & format)
{
	Stream = stream;
	DataStart = datastart;
	Format = format;
	SampleSize = format.BytesPerSample * format.Channels;
	CurrentPos = 0;
	Stream->SetPosition(DataStart);
}
//---------------------------------------------------------------------------
tTVPWD_RIFFWave::~tTVPWD_RIFFWave()
{
	delete Stream;
}
//---------------------------------------------------------------------------
void tTVPWD_RIFFWave::GetFormat(tTVPWaveFormat & format)
{
	format = Format;
}
//---------------------------------------------------------------------------
bool tTVPWD_RIFFWave::Render(void *buf, tjs_uint bufsamplelen, tjs_uint& rendered)
{
	tjs_uint64 remain = Format.TotalSamples - CurrentPos;
	tjs_uint writesamples = bufsamplelen < remain ? bufsamplelen : (tjs_uint)remain;
	if(writesamples == 0)
	{
		// already finished stream or bufsamplelen is zero
		rendered = 0;
		return false;
	}

	tjs_uint readsize = writesamples * SampleSize;
	tjs_uint read = Stream->Read(buf, readsize);

#if TJS_HOST_IS_BIG_ENDIAN
	// endian-ness conversion
	if(Format.BytesPerSample == 2)
	{
		tjs_uint16 *p = (tjs_uint16 *)buf;
		tjs_uint16 *plim = (tjs_uint16 *)( (tjs_uint8*)buf + read);
		while(p < plim)
		{
			*p = (*p>>8) + (*p<<8);
			p++;
		}
	}
	else if(Format.BytesPerSample == 3)
	{
		tjs_uint8 *p = (tjs_uint8 *)buf;
		tjs_uint8 *plim = (tjs_uint8 *)( (tjs_uint8*)buf + read);
		while(p < plim)
		{
			tjs_uint8 tmp = p[0];
			p[0] = p[2];
			p[2] = tmp;
			p += 3;
		}
	}
	else if(Format.BytesPerSample == 4)
	{
		tjs_uint32 *p = (tjs_uint32 *)buf;
		tjs_uint32 *plim = (tjs_uint32 *)( (tjs_uint8*)buf + read);
		while(p < plim)
		{
			*p =
				(*p &0xff000000) >> 24 +
				(*p &0x00ff0000) >> 8 +
				(*p &0x0000ff00) << 8 +
				(*p &0x000000ff) << 24;
			p ++;
		}
	}
#endif


	rendered = read / SampleSize;
	CurrentPos += rendered;

	if(read < readsize || writesamples < bufsamplelen)
		return false; // read error or end of stream

	return true;
}
//---------------------------------------------------------------------------
bool tTVPWD_RIFFWave::SetPosition(tjs_uint64 samplepos)
{
	if(Format.TotalSamples <= samplepos) return false;

	tjs_uint64 streampos = DataStart + samplepos * SampleSize;
	tjs_uint64 possave = Stream->GetPosition();

	if(streampos != Stream->Seek(streampos, TJS_BS_SEEK_SET))
	{
		// seek failed
		Stream->Seek(possave, TJS_BS_SEEK_SET);
		return false;
	}

	CurrentPos = samplepos;
	return true;
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// default RIFF Wave Decoder creator
//---------------------------------------------------------------------------
class tTVPWDC_RIFFWave : public tTVPWaveDecoderCreator
{
public:
	tTVPWaveDecoder * Create(const ttstr & storagename, const ttstr & extension);
};
//---------------------------------------------------------------------------
static bool TVPFindRIFFChunk(tTVPStreamHolder & stream, const tjs_uint8 *chunk)
{
	tjs_uint8 buf[4];
	while(true)
	{
		if(4 != stream->Read(buf, 4)) return false;
		if(memcmp(buf, chunk, 4))
		{
			// skip to next chunk
			tjs_uint32 chunksize = stream->ReadI32LE();
			tjs_int64 next = stream->GetPosition() + chunksize;
			if(next != stream->Seek(next, TJS_BS_SEEK_SET)) return false;
		}
		else
		{
			return true;
		}
	}
}
//---------------------------------------------------------------------------
tTVPWaveDecoder * tTVPWDC_RIFFWave::Create(const ttstr & storagename,
	const ttstr &extension)
{
	if(extension != TJS_W(".wav")) return NULL;

	tTVPStreamHolder stream(storagename);

	static const tjs_uint8 riff_mark[] =
		{ /*R*/0x52, /*I*/0x49, /*F*/0x46, /*F*/0x46 };
	static const tjs_uint8 wave_mark[] =
		{ /*W*/0x57, /*A*/0x41, /*V*/0x56, /*E*/0x45 };
	static const tjs_uint8 fmt_mark[] =
		{ /*f*/0x66, /*m*/0x6d, /*t*/0x74, /* */0x20 };
	static const tjs_uint8 data_mark[] =
		{ /*d*/0x64, /*a*/0x61, /*t*/0x74, /*a*/0x61 };

	try
	{
		tjs_uint32 size;
		tjs_int64 next;

		// check RIFF mark
		tjs_uint8 buf[4];
		if(4 != stream->Read(buf, 4)) return NULL;
		if(memcmp(buf, riff_mark, 4)) return NULL;

		if(4 != stream->Read(buf, 4)) return NULL; // RIFF chunk size; discard

		// check WAVE subid
		if(4 != stream->Read(buf, 4)) return NULL;
		if(memcmp(buf, wave_mark, 4)) return NULL;

		// find fmt chunk
		if(!TVPFindRIFFChunk(stream, fmt_mark)) return NULL;

		size = stream->ReadI32LE();
		next = stream->GetPosition() + size;

		// read format
		tTVPWaveFormat format;
//		bool do_reorder_5poiont1ch = false;
			// whether to reorder 5.1ch channel samples
			// (AC3 order to WAVEFORMATEXTENSIBLE order)

		tjs_uint16 format_tag = stream->ReadI16LE(); // wFormatTag
		if(format_tag != WAVE_FORMAT_PCM &&
			format_tag != WAVE_FORMAT_IEEE_FLOAT &&
			format_tag != WAVE_FORMAT_EXTENSIBLE) return NULL;


		format.Channels = stream->ReadI16LE(); // nChannels
		format.SamplesPerSec = stream->ReadI32LE(); // nSamplesPerSec

		if(4 != stream->Read(buf, 4)) return NULL; // nAvgBytesPerSec; discard

		tjs_uint16 block_align = stream->ReadI16LE(); // nBlockAlign

		format.BitsPerSample = stream->ReadI16LE(); // wBitsPerSample

		tjs_uint16 ext_size = stream->ReadI16LE(); // cbSize
		if(format_tag == WAVE_FORMAT_EXTENSIBLE)
		{
			if(ext_size != 22) return NULL; // invalid extension length
			if(format.BitsPerSample & 0x07) return NULL;  // not integer multiply by 8
			format.BytesPerSample = format.BitsPerSample / 8;
			format.BitsPerSample = stream->ReadI16LE(); // wValidBitsPerSample
			format.SpeakerConfig = stream->ReadI32LE(); // dwChannelMask

			tjs_uint8 guid[16];
			if(16 != stream->Read(guid, 16)) return NULL;
			if(!memcmp(guid, TVP_GUID_KSDATAFORMAT_SUBTYPE_PCM, 16))
				format.IsFloat = false;
			else if(!memcmp(guid, TVP_GUID_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, 16))
				format.IsFloat = true;
			else
				return NULL;
		}
		else
		{
			if(format.BitsPerSample & 0x07) return NULL; // not integer multiplyed by 8
			format.BytesPerSample = format.BitsPerSample / 8;

			if(format.Channels == 4)
			{
				format.SpeakerConfig = 0;
//				format.SpeakerConfig =
//					SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT |
//					SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT;

			}
			else if(format.Channels == 6)
			{
				format.SpeakerConfig = 0;
//				do_reorder_5poiont1ch = true;
//				format.SpeakerConfig =
//					SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT |
			}
			else
			{
				format.SpeakerConfig = 0;
			}

			format.IsFloat = format_tag == WAVE_FORMAT_IEEE_FLOAT;
		}


		if(format.BitsPerSample > 32) return NULL; // too large bits
		if(format.BitsPerSample < 8) return NULL; // too less bits
		if(format.BitsPerSample > format.BytesPerSample * 8)
			return NULL; // bits per sample is larger than bytes per sample
		if(format.IsFloat)
		{
			if(format.BitsPerSample != 32) return NULL; // not a 32-bit IEEE float
			if(format.BytesPerSample != 4) return NULL;
		}

		if((tjs_int) block_align != (tjs_int)(format.BytesPerSample * format.Channels))
			return NULL; // invalid align

		if(next != stream->Seek(next, TJS_BS_SEEK_SET)) return NULL;

		// find data chunk
		if(!TVPFindRIFFChunk(stream, data_mark)) return NULL;

		size = stream->ReadI32LE();

		tjs_int64 datastart;

		tjs_int64 remain_size = stream->GetSize() -
			(datastart = stream->GetPosition());
		if(size > remain_size) return NULL;
			// data ends before "size" described in the header

		// compute total sample count and total length in time
		format.TotalSamples = size / (format.Channels * format.BitsPerSample / 8);
		format.TotalTime = format.TotalSamples * 1000 / format.SamplesPerSec;

		// create tTVPWD_RIFFWave instance
		format.Seekable = true;
		tTVPWD_RIFFWave * decoder = new tTVPWD_RIFFWave(stream.Get(), datastart,
			format);
		stream.Disown();

		return decoder;
	}
	catch(...)
	{
		// this function must be a silent one unless file open error...
		return NULL;
	}
}
//---------------------------------------------------------------------------




//---------------------------------------------------------------------------
// tTVPWaveDecoder interface management
//---------------------------------------------------------------------------
bool TVPWaveDecoderManagerAvail = false;
struct tTVPWaveDecoderManager
{
	std::vector<tTVPWaveDecoderCreator *> Creators;
	tTVPWDC_RIFFWave RIFFWaveDecoderCreator;
    VorbisWaveDecoderCreator vorbisWaveDecoderCreator;
    FFWaveDecoderCreator ffWaveDecoderCreator;
    OpusWaveDecoderCreator opusWaveDecoderCreator;

	tTVPWaveDecoderManager()
	{
		TVPWaveDecoderManagerAvail = true;
        TVPRegisterWaveDecoderCreator(&ffWaveDecoderCreator);
        TVPRegisterWaveDecoderCreator(&opusWaveDecoderCreator);
		TVPRegisterWaveDecoderCreator(&RIFFWaveDecoderCreator);
        TVPRegisterWaveDecoderCreator(&vorbisWaveDecoderCreator);
	}

	~tTVPWaveDecoderManager()
	{
		TVPWaveDecoderManagerAvail = false;
	}

} static TVPWaveDecoderManager;
//---------------------------------------------------------------------------
void TVPRegisterWaveDecoderCreator(tTVPWaveDecoderCreator *d)
{
	if(TVPWaveDecoderManagerAvail)
		TVPWaveDecoderManager.Creators.push_back(d);
}
//---------------------------------------------------------------------------
void TVPUnregisterWaveDecoderCreator(tTVPWaveDecoderCreator *d)
{
	if(TVPWaveDecoderManagerAvail)
	{
		std::vector<tTVPWaveDecoderCreator *>::iterator i;
		i = std::find(TVPWaveDecoderManager.Creators.begin(),
			TVPWaveDecoderManager.Creators.end(), d);
		if(i != TVPWaveDecoderManager.Creators.end())
		{
			TVPWaveDecoderManager.Creators.erase(i);
		}
	}
}
//---------------------------------------------------------------------------
tTVPWaveDecoder *  TVPCreateWaveDecoder(const ttstr & storagename)
{
	// find a decoder and create its instance.
	// throws an exception when the decodable decoder is not found.
	if(!TVPWaveDecoderManagerAvail) return NULL;

	ttstr ext(TVPExtractStorageExt(storagename));
	ext.ToLowerCase();

	tjs_int i = (tjs_int)(TVPWaveDecoderManager.Creators.size()-1);
	for(; i>=0; i--)
	{
		tTVPWaveDecoder * decoder;
		decoder = TVPWaveDecoderManager.Creators[i]->Create(storagename, ext);
		if(decoder) return decoder;
	}

	TVPThrowExceptionMessage(TVPUnknownWaveFormat, storagename);
	return NULL;
}
//---------------------------------------------------------------------------