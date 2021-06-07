/*
 * Copyright (C) 2017-2021 Norbert Schlia (nschlia@oblivion-software.de)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * On Debian systems, the complete text of the GNU General Public License
 * Version 3 can be found in `/usr/share/common-licenses/GPL-3'.
 */

/**
 * @file
 * @brief FFmpeg_Transcoder class implementation
 *
 * @ingroup ffmpegfs
 *
 * @author Norbert Schlia (nschlia@oblivion-software.de)
 * @copyright Copyright (C) 2017-2021 Norbert Schlia (nschlia@oblivion-software.de)
 */

#include "ffmpeg_transcoder.h"
#include "transcode.h"
#include "buffer.h"
#include "wave.h"
#include "logging.h"

#include <assert.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif
// Disable annoying warnings outside our code
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include <libswscale/swscale.h>
#if LAVR_DEPRECATE
#include <libswresample/swresample.h>
#else
#include <libavresample/avresample.h>
#endif
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/audio_fifo.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavcodec/avcodec.h>
#pragma GCC diagnostic pop
#ifdef __cplusplus
}
#endif

// #define USE_INTERLEAVED_WRITE

#define FRAME_SEEK_THRESHOLD    25  /**< @brief Ignore seek if target is within the next n frames */

const FFmpeg_Transcoder::PRORES_BITRATE FFmpeg_Transcoder::m_prores_bitrate[] =
{
    // SD
    {	720,	486,	{ {	24,	0 }                   },	{	10,     23,     34,     50,     75,     113     }	},
    {	720,	486,	{ {	60,	1 },	{   30,	0 }   },	{	12,     29,     42,     63,     94,     141     }	},

    {	720,	576,	{ {	50,	1 },	{   25,	0 }   },	{	12,     28,     41,     61,     92,     138     }	},

    {	960,	720,	{ {	24,	0 }                   },	{	15,     35,     50,     75,     113,	170     }	},
    {	960,	720,	{ {	25,	0 }                   },	{	16,     36,     52,     79,     118,	177     }	},
    {	960,	720,	{ {	30,	0 }                   },	{	19,     44,     63,     94,     141,	212     }	},
    {	960,	720,	{ {	50,	0 }                   },	{	32,     73,     105,	157,	236,	354     }	},
    {	960,	720,	{ {	60,	0 }                   },	{	38,     87,     126,	189,	283,	424     }	},
    // HD
    {	1280,	720,	{ {	24,	0 }                   },	{	18,     41,     59,     88,     132,	198     }	},
    {	1280,	720,	{ {	25,	0 }                   },	{	19,     42,     61,     92,     138,	206     }	},
    {	1280,	720,	{ {	30,	0 }                   },	{	23,     51,     73,     110,	165,	247     }	},
    {	1280,	720,	{ {	50,	0 }                   },	{	38,     84,     122,	184,	275,	413     }	},
    {	1280,	720,	{ {	60,	0 }                   },	{	45,     101,	147,	220,	330,	495     }	},

    {	1280,	1080,	{ {	24,	0 }                   },	{	31,     70,     101,	151,	226,	339     }	},
    {	1280,	1080,	{ {	60,	1 },	{   30,	0 }   },	{	38,     87,     126,	189,	283,	424     }	},

    {	1440,	1080,	{ {	24,	0 }                   },	{	31,     70,     101,	151,	226,	339     }	},
    {	1440,	1080,	{ {	50,	1 },	{   25,	0 }   },	{	32,     73,     105,	157,	236,	354     }	},
    {	1440,	1080,	{ {	60,	1 },	{   30,	0 }   },	{	38,     87,     126,	189,	283,	424     }	},
    // Full HD
    {	1920,	1080,	{ {	24,	0 }                   },	{	36,     82,     117,	176,	264,	396     }	},
    {	1920,	1080,	{ {	50,	1 },	{   25,	0 }   },	{	38,     85,     122,	184,	275,	413     }	},
    {	1920,	1080,	{ {	60,	1 },	{   30,	0 }   },	{	45,     102,	147,	220,	330,	495     }	},
    {	1920,	1080,	{ {	50,	0 }                   },	{	76,     170,	245,	367,	551,	826     }	},
    {	1920,	1080,	{ {	60,	0 }                   },	{	91,     204,	293,	440,	660,	990     }	},
    // 2K
    {	2048,	1080,	{ {	24,	0 }                   },	{	41,     93,     134,	201,	302,	453     }	},
    {	2048,	1080,	{ {	25,	0 }                   },	{	43,     97,     140,	210,	315,	472     }	},
    {	2048,	1080,	{ {	30,	0 }                   },	{	52,     116,	168,	251,	377,	566     }	},
    {	2048,	1080,	{ {	50,	0 }                   },	{	86,     194,	280,	419,	629,	944     }	},
    {	2048,	1080,	{ {	60,	0 }                   },	{	103,	232,	335,	503,	754,	1131	}	},
    // 2K
    {	2048,	1556,	{ {	24,	0 }                   },	{	56,     126,	181,	272,	407,	611     }	},
    {	2048,	1556,	{ {	25,	0 }                   },	{	58,     131,	189,	283,	425,	637     }	},
    {	2048,	1556,	{ {	30,	0 }                   },	{	70,     157,	226,	340,	509,	764     }	},
    {	2048,	1556,	{ {	50,	0 }                   },	{	117,	262,	377,	567,	850,	1275	}	},
    {	2048,	1556,	{ {	60,	0 }                   },	{	140,	314,	452,	679,	1019,	1528	}	},
    // QFHD
    {	3840,	2160,	{ {	24,	0 }                   },	{	145,	328,	471,	707,	1061,	1591	}	},
    {	3840,	2160,	{ {	25,	0 }                   },	{	151,	342,	492,	737,	1106,	1659	}	},
    {	3840,	2160,	{ {	30,	0 }                   },	{	182,	410,	589,	884,	1326,	1989	}	},
    {	3840,	2160,	{ {	50,	0 }                   },	{	303,	684,	983,	1475,	2212,	3318	}	},
    {	3840,	2160,	{ {	60,	0 }                   },	{	363,	821,	1178,	1768,	2652,	3977	}	},
    // 4K
    {	4096,	2160,	{ {	24,	0 }                   },	{	155,	350,	503,	754,	1131,	1697	}	},
    {	4096,	2160,	{ {	25,	0 }                   },	{	162,	365,	524,	786,	1180,	1769	}	},
    {	4096,	2160,	{ {	30,	0 }                   },	{	194,	437,	629,	943,	1414,	2121	}	},
    {	4096,	2160,	{ {	50,	0 }                   },	{	323,	730,	1049,	1573,	2359,	3539	}	},
    {	4096,	2160,	{ {	60,	0 }                   },	{	388,	875,	1257,	1886,	2828,	4242	}	},
    // 5K
    {	5120,	2700,	{ {	24,	0 }                   },	{	243,	547,	786,	1178,	1768,	2652	}	},
    {	5120,	2700,	{ {	25,	0 }                   },	{	253,	570,	819,	1229,	1843,	2765	}	},
    {	5120,	2700,	{ {	30,	0 }                   },	{	304,	684,	982,	1473,	2210,	3314	}	},
    {	5120,	2700,	{ {	50,	0 }                   },	{	507,	1140,	1638,	2458,	3686,	5530	}	},
    {	5120,	2700,	{ {	60,	0 }                   },	{	608,	1367,	1964,	2946,	4419,	6629	}	},
    // 6K
    {	6144,	3240,	{ {	24,	0 }                   },	{	350,	788,	1131,	1697,	2545,	3818	}	},
    {	6144,	3240,	{ {	25,	0 }                   },	{	365,	821,	1180,	1769,	2654,	3981	}	},
    {	6144,	3240,	{ {	30,	0 }                   },	{	437,	985,	1414,	2121,	3182,	4772	}	},
    {	6144,	3240,	{ {	50,	0 }                   },	{	730,	1643,	2359,	3539,	5308,	7962	}	},
    {	6144,	3240,	{ {	60,	0 }                   },	{	875,	1969,	2828,	4242,	6364,	9545	}	},
    // 8K
    {	8192,	4320,	{ {	24,	0 }                   },	{	622,	1400,	2011,	3017,	4525,	6788	}	},
    {	8192,	4320,	{ {	25,	0 }                   },	{	649,	1460,	2097,	3146,	4719,	7078	}	},
    {	8192,	4320,	{ {	30,	0 }                   },	{	778,	1750,	2514,	3771,	5657,	8485	}	},
    {	8192,	4320,	{ {	50,	0 }                   },	{	1298,	2920,	4194,	6291,	9437,	14156	}	},
    {	8192,	4320,	{ {	60,	0 }                   },	{	1556,	3500,	5028,	7542,	11313,	16970	}	},
    // That's it
    {   0,     0,     {                               },	{	0 }	},
};

const FFmpeg_Transcoder::DEVICETYPE_MAP FFmpeg_Transcoder::m_devicetype_map =
{
    { AV_HWDEVICE_TYPE_VAAPI,           AV_PIX_FMT_NV12 },          ///< VAAPI uses the NV12 pix format
    #if 0
    { AV_HWDEVICE_TYPE_CUDA,            AV_PIX_FMT_CUDA },          ///< @todo HWACCEL - Cuda pix_fmt: to be added.
    { AV_HWDEVICE_TYPE_VDPAU,           AV_PIX_FMT_YUV420P },       ///< @todo HWACCEL - VDPAU pix_fmt: to be added.
    { AV_HWDEVICE_TYPE_QSV,             AV_PIX_FMT_QSV },           ///< @todo HWACCEL - QSV pix_fmt untested: Seems to be AV_PIX_FMT_P010 or AV_PIX_FMT_QSV. To be added.
    { AV_HWDEVICE_TYPE_OPENCL,          AV_PIX_FMT_OPENCL },        ///< @todo HWACCEL - OpenCL pix_fmt: Seems to be AV_PIX_FMT_OPENCL or AV_PIX_FMT_NV12. To be added.
    #if HAVE_VULKAN_HWACCEL
    { AV_HWDEVICE_TYPE_VULKAN,          AV_PIX_FMT_VULKAN },        ///< @todo HWACCEL - Vulkan pix_fmt: to be added.
    #endif // HAVE_VULKAN_HWACCEL
    #if __APPLE__
    { AV_HWDEVICE_TYPE_VIDEOTOOLBOX,    AV_PIX_FMT_VIDEOTOOLBOX },  ///< Videotoolbox pix_fmt: MacOS acceleration APIs not supported
    #endif // __APPLE__
    #if __ANDROID__
    { AV_HWDEVICE_TYPE_MEDIACODEC,      AV_PIX_FMT_MEDIACODEC },    ///< Mediacodec pix_fmt: Android acceleration APIs not supported
    #endif // __ANDROID__
    #if _WIN32
    { AV_HWDEVICE_TYPE_DRM,             AV_PIX_FMT_DRM_PRIME },     ///< DRM prime pix_fmt: Windows acceleration APIs not supported
    { AV_HWDEVICE_TYPE_DXVA2,           AV_PIX_FMT_DXVA2_VLD },     ///< DXVA2 pix_fmt: Windows acceleration APIs not supported
    { AV_HWDEVICE_TYPE_D3D11VA,         AV_PIX_FMT_D3D11VA_VLD },   ///< D3D11VA pix_fmt: Windows acceleration APIs not supported
    #endif // _WIN32
    #endif
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
FFmpeg_Transcoder::FFmpeg_Transcoder()
    : m_fileio(nullptr)
    , m_close_fileio(true)
    , m_last_seek_frame_no(0)
    , m_have_seeked(false)
    , m_skip_next_frame(false)
    , m_is_video(false)
    , m_cur_sample_fmt(AV_SAMPLE_FMT_NONE)
    , m_cur_sample_rate(-1)
    , m_cur_channel_layout(0)
    , m_audio_resample_ctx(nullptr)
    , m_audio_fifo(nullptr)
    , m_sws_ctx(nullptr)
    , m_buffer_sink_context(nullptr)
    , m_buffer_source_context(nullptr)
    , m_filter_graph(nullptr)
    , m_pts(AV_NOPTS_VALUE)
    , m_pos(AV_NOPTS_VALUE)
    , m_current_segment(1)
    , m_copy_audio(false)
    , m_copy_video(false)
    , m_current_format(nullptr)
    , m_buffer(nullptr)
    , m_reset_pts(0)
    , m_fake_frame_no(0)
    , m_hwaccel_enc_mode(HWACCELMODE_NONE)
    , m_hwaccel_dec_mode(HWACCELMODE_NONE)
    , m_hwaccel_enable_enc_buffering(false)
    , m_hwaccel_enable_dec_buffering(false)
    , m_hwaccel_enc_device_ctx(nullptr)
    , m_hwaccel_dec_device_ctx(nullptr)
    , m_enc_hw_pix_fmt(AV_PIX_FMT_NONE)
    , m_dec_hw_pix_fmt(AV_PIX_FMT_NONE)
    , m_active_stream_msk(0)
    , m_inhibit_stream_msk(0)
{
#pragma GCC diagnostic pop
    Logging::trace(nullptr, "FFmpeg trancoder ready to initialise.");

    // Initialise ID3v1.1 tag structure
    init_id3v1(&m_out.m_id3v1);
}

FFmpeg_Transcoder::~FFmpeg_Transcoder()
{
    // Close fifo and resample context
    close();

    Logging::trace(nullptr, "FFmpeg trancoder object destroyed.");
}

bool FFmpeg_Transcoder::is_video() const
{
    bool is_video = false;

    if (m_in.m_video.m_codec_ctx != nullptr && m_in.m_video.m_stream != nullptr)
    {
        is_video = !is_album_art(m_in.m_video.m_codec_ctx->codec_id, &m_in.m_video.m_stream->r_frame_rate);
    }

    return is_video;
}

bool FFmpeg_Transcoder::is_open() const
{
    return (m_in.m_format_ctx != nullptr);
}

int FFmpeg_Transcoder::open_input_file(LPVIRTUALFILE virtualfile, FileIO *fio)
{
    AVDictionary * opt = nullptr;
    int ret;

    if (virtualfile == nullptr)
    {
        Logging::error(filename(), "INTERNAL ERROR in open_input_file(): virtualfile is NULL.");
        return AVERROR(EINVAL);
    }

    m_virtualfile       = virtualfile;

    m_in.m_filename     = m_virtualfile->m_origfile;
    m_out.m_filename    = m_virtualfile->m_destfile;
    m_mtime             = m_virtualfile->m_st.st_mtime;
    m_current_format    = params.current_format(m_virtualfile);

    if (is_open())
    {
        Logging::warning(filename(), "File is already open.");
        return 0;
    }

    // This allows selecting if the demuxer should consider all streams to be
    // found after the first PMT and add further streams during decoding or if it rather
    // should scan all that are within the analyze-duration and other limits
    ret = dict_set_with_check(&opt, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
    if (ret < 0)
    {
        return ret;
    }

    // avioflags direct: Reduce buffering.
    //ret = av_dict_set_with_check(&opt, "avioflags", "direct", AV_DICT_DONT_OVERWRITE);
    //if (ret < 0)
    //{
    //    return ret;
    //}

    // analyzeduration: Defaults to 5,000,000 microseconds = 5 seconds.
    //ret = av_dict_set_with_check(&opt, "analyzeduration", "5000000", 0);    // <<== honored
    //if (ret < 0)
    //{
    //    return ret;
    //}

    // probesize: 5000000 by default.
    ret = dict_set_with_check(&opt, "probesize", "15000000", 0);          // <<== honoured;
    if (ret < 0)
    {
        return ret;
    }

    // using own I/O
    if (fio == nullptr)
    {
        // Open new file io
        m_fileio = FileIO::alloc(m_virtualfile->m_type);
        m_close_fileio = true;  // do not close and delete
    }
    else
    {
        // Use already open file io
        m_fileio = fio;
        m_close_fileio = false; // must not close or delete
    }

    if (m_fileio == nullptr)
    {
        int _errno = errno;
        Logging::error(filename(), "Error opening file: (%1) %2", errno, strerror(errno));
        return AVERROR(_errno);
    }

    ret = m_fileio->open(m_virtualfile);
    if (ret)
    {
        return AVERROR(ret);
    }

    m_in.m_format_ctx = avformat_alloc_context();
    if (m_in.m_format_ctx == nullptr)
    {
        Logging::error(filename(), "Out of memory opening file: Unable to allocate format context.");
        return AVERROR(ENOMEM);
    }

    unsigned char *iobuffer = static_cast<unsigned char *>(av_malloc(m_fileio->bufsize() + FF_INPUT_BUFFER_PADDING_SIZE));
    if (iobuffer == nullptr)
    {
        Logging::error(filename(), "Out of memory opening file: Unable to allocate I/O buffer.");
        avformat_free_context(m_in.m_format_ctx);
        m_in.m_format_ctx = nullptr;
        return AVERROR(ENOMEM);
    }

    AVIOContext * pb = avio_alloc_context(
                iobuffer,
                static_cast<int>(m_fileio->bufsize()),
                0,
                static_cast<void *>(m_fileio),
                input_read,
                nullptr,    // input_write
                seek);      // input_seek
    m_in.m_format_ctx->pb = pb;

#if IF_DECLARED_CONST
    const AVInputFormat * infmt = nullptr;
#else // !IF_DECLARED_CONST
    AVInputFormat * infmt = nullptr;
#endif // !IF_DECLARED_CONST

#ifdef USE_LIBVCD
    if (m_virtualfile->m_type == VIRTUALTYPE_VCD)
    {
        Logging::debug(filename(), "Forcing mpeg format for VCD source to avoid misdetections.");
        infmt = av_find_input_format("mpeg");
    }
#endif // USE_LIBVCD
#ifdef USE_LIBDVD
    if (m_virtualfile->m_type == VIRTUALTYPE_DVD)
    {
        Logging::debug(filename(), "Forcing mpeg format for DVD source to avoid misdetections.");
        infmt = av_find_input_format("mpeg");
    }
#endif // USE_LIBDVD
#ifdef USE_LIBBLURAY
    if (m_virtualfile->m_type == VIRTUALTYPE_BLURAY)
    {
        Logging::debug(filename(), "Forcing mpegts format for Bluray source to avoid misdetections.");
        infmt = av_find_input_format("mpegts");
    }
#endif // USE_LIBBLURAY

    /** @bug Fix memory leak: Probably in FFmpeg API av_probe_input_buffer2(), the av_reallocp
      * is missing a matching free() call... @n
      * @n
      * 102,400 bytes in 1 blocks are definitely lost in loss record 248 of 249 @n
      *   in FFmpeg_Transcoder::open_input_file(VIRTUALFILE*, FileIO*) in /home/norbert/dev/prj/ffmpegfs/src/ffmpeg_transcoder.cc:368 @n
      *   1: realloc in ./coregrind/m_replacemalloc/vg_replace_malloc.c:834 @n
      *   2: av_realloc_f in /usr/lib/x86_64-linux-gnu/libavutil.so.56.51.100 @n
      *   3: /usr/lib/x86_64-linux-gnu/libavformat.so.58.45.100 @n
      *   4: av_probe_input_buffer2 in /usr/lib/x86_64-linux-gnu/libavformat.so.58.45.100 @n
      *   5: avformat_open_input in /usr/lib/x86_64-linux-gnu/libavformat.so.58.45.100 @n
      *   6: FFmpeg_Transcoder::open_input_file(VIRTUALFILE*, FileIO*) in /home/norbert/dev/prj/ffmpegfs/src/ffmpeg_transcoder.cc:368 @n
      *   7: transcoder_predict_filesize(VIRTUALFILE*, Cache_Entry*) in /home/norbert/dev/prj/ffmpegfs/src/transcode.cc:320 @n
      *   8: transcoder_new(VIRTUALFILE*, bool) in /home/norbert/dev/prj/ffmpegfs/src/transcode.cc:425 @n
      *   9: ffmpegfs_getattr(char const*, stat*) in /home/norbert/dev/prj/ffmpegfs/src/fuseops.cc:1323 @n
      *   10: /usr/lib/x86_64-linux-gnu/libfuse.so.2.9.9 @n
      *   11: /usr/lib/x86_64-linux-gnu/libfuse.so.2.9.9 @n
      *   12: /usr/lib/x86_64-linux-gnu/libfuse.so.2.9.9 @n
      *   13: /usr/lib/x86_64-linux-gnu/libfuse.so.2.9.9 @n
      *   14: start_thread in ./nptl/pthread_create.c:477 @n
      *   15: clone in ./misc/../sysdeps/unix/sysv/linux/x86_64/clone.S:95 @n
      */

    // Open the input file to read from it.
    ret = avformat_open_input(&m_in.m_format_ctx, filename(), infmt, &opt);
    if (ret < 0)
    {
        Logging::error(filename(), "Could not open input file (error '%1').", ffmpeg_geterror(ret).c_str());
        return ret;
    }

    m_in.m_filetype = get_filetype_from_list(m_in.m_format_ctx->iformat->name);

    ret = dict_set_with_check(&opt, "scan_all_pmts", nullptr, AV_DICT_MATCH_CASE, filename());
    if (ret < 0)
    {
        return ret;
    }

    AVDictionaryEntry * t = av_dict_get(opt, "", nullptr, AV_DICT_IGNORE_SUFFIX);
    if (t != nullptr)
    {
        Logging::error(filename(), "Option %1 not found.", t->key);
        return (EOF); // Couldn't open file
    }

#if HAVE_AV_FORMAT_INJECT_GLOBAL_SIDE_DATA
    av_format_inject_global_side_data(m_in.m_format_ctx);
#endif

    // Get information on the input file (number of streams etc.).
    ret = avformat_find_stream_info(m_in.m_format_ctx, nullptr);
    if (ret < 0)
    {
        Logging::error(filename(), "Could not find stream info (error '%1').", ffmpeg_geterror(ret).c_str());
        return ret;
    }

#ifdef USE_LIBDVD
    if (m_virtualfile->m_type == VIRTUALTYPE_DVD)
    {
        // FFmpeg API calculcates a wrong duration, so use value from IFO
        m_in.m_format_ctx->duration = m_fileio->duration();
    }
#endif // USE_LIBDVD
#ifdef USE_LIBBLURAY
    if (m_virtualfile->m_type == VIRTUALTYPE_BLURAY)
    {
        // FFmpeg API calculcates a wrong duration, so use value from Bluray directory
        m_in.m_format_ctx->duration = m_fileio->duration();
    }
#endif // USE_LIBBLURAY

    m_virtualfile->m_duration = m_in.m_format_ctx->duration;

    // Issue #80: Open input video codec, but only if target supports video.
    // Saves resources: no need to decode video frames if not used.
    if (m_current_format->video_codec_id() != AV_CODEC_ID_NONE)
    {
        // Open best match video codec
        ret = open_bestmatch_decoder(&m_in.m_video.m_codec_ctx, &m_in.m_video.m_stream_idx, AVMEDIA_TYPE_VIDEO);
        if (ret < 0 && ret != AVERROR_STREAM_NOT_FOUND)    // AVERROR_STREAM_NOT_FOUND is not an error
        {
            Logging::error(filename(), "Failed to open video codec (error '%1').", ffmpeg_geterror(ret).c_str());
            return ret;
        }

        if (m_in.m_video.m_stream_idx != INVALID_STREAM)
        {
            // We have a video stream
            // Check to see if encoder hardware acceleration is both requested and supported by codec.
            std::string hw_encoder_codec_name;
            if (!get_hw_encoder_name(m_current_format->video_codec_id(), &hw_encoder_codec_name))
            {
                // API supports hardware frame buffers
                m_hwaccel_enable_enc_buffering = (params.m_hwaccel_enc_device_type != AV_HWDEVICE_TYPE_NONE);
            }

            if (m_hwaccel_enable_enc_buffering)
            {
                // Hardware buffers available, enabling encoder hardware accceleration.
                Logging::info(destname(), "Hardware encoder frame buffering %1 enabled.", get_hwaccel_API_text(params.m_hwaccel_enc_API).c_str());
                ret = hwdevice_ctx_create(&m_hwaccel_enc_device_ctx, params.m_hwaccel_enc_device_type, params.m_hwaccel_enc_device);
                if (ret < 0)
                {
                    Logging::error(destname(), "Failed to create a %1 device for encoding (error %2).", get_hwaccel_API_text(params.m_hwaccel_enc_API).c_str(), ffmpeg_geterror(ret).c_str());
                    return ret;
                }
                Logging::debug(destname(), "Hardware encoder acceleration and frame buffering active using codec '%1'.", hw_encoder_codec_name.c_str());
            }
            else if (params.m_hwaccel_enc_device_type != AV_HWDEVICE_TYPE_NONE)
            {
                // No hardware acceleration, fallback to software,
                Logging::debug(destname(), "Hardware encoder frame buffering %1 not suported by codec '%2'. Falling back to software.", get_hwaccel_API_text(params.m_hwaccel_enc_API).c_str(), get_codec_name(m_in.m_video.m_codec_ctx->codec_id, true));
            }
            else if (!hw_encoder_codec_name.empty())
            {
                // No frame buffering (e.g. OpenMAX or MMAL), but hardware acceleration possible.
                Logging::debug(destname(), "Hardware encoder acceleration active using codec '%1'.", hw_encoder_codec_name.c_str());
            }

            m_in.m_video.m_stream               = m_in.m_format_ctx->streams[m_in.m_video.m_stream_idx];

#ifdef USE_LIBDVD
            if (m_virtualfile->m_type == VIRTUALTYPE_DVD)
            {
                // FFmpeg API calculcates a wrong duration, so use value from IFO
                m_in.m_video.m_stream->duration = av_rescale_q(m_in.m_format_ctx->duration, av_get_time_base_q(), m_in.m_video.m_stream->time_base);
            }
#endif // USE_LIBDVD
#ifdef USE_LIBBLURAY
            if (m_virtualfile->m_type == VIRTUALTYPE_BLURAY)
            {
                // FFmpeg API calculcates a wrong duration, so use value from Bluray
                m_in.m_video.m_stream->duration = av_rescale_q(m_in.m_format_ctx->duration, av_get_time_base_q(), m_in.m_video.m_stream->time_base);
            }
#endif // USE_LIBBLURAY

            video_info(false, m_in.m_format_ctx, m_in.m_video.m_stream);

            m_is_video = is_video();

#ifdef AV_CODEC_CAP_TRUNCATED
            if (m_in.m_video.m_codec_ctx->codec->capabilities & AV_CODEC_CAP_TRUNCATED)
            {
                m_in.m_video.m_codec_ctx->flags|= AV_CODEC_FLAG_TRUNCATED; // we do not send complete frames
            }
#else
#warning "Your FFMPEG distribution is missing AV_CODEC_CAP_TRUNCATED flag. Probably requires fixing!"
#endif
        }
    }

    // Open best match audio codec
    ret = open_bestmatch_decoder(&m_in.m_audio.m_codec_ctx, &m_in.m_audio.m_stream_idx, AVMEDIA_TYPE_AUDIO);
    if (ret < 0 && ret != AVERROR_STREAM_NOT_FOUND)    // AVERROR_STREAM_NOT_FOUND is not an error
    {
        Logging::error(filename(), "Failed to open audio codec (error '%1').", ffmpeg_geterror(ret).c_str());
        return ret;
    }

    if (m_in.m_audio.m_stream_idx != INVALID_STREAM)
    {
        // We have an audio stream
        m_in.m_audio.m_stream = m_in.m_format_ctx->streams[m_in.m_audio.m_stream_idx];

#ifdef USE_LIBDVD
        if (m_virtualfile->m_type == VIRTUALTYPE_DVD)
        {
            // FFmpeg API calculcates a wrong duration, so use value from IFO
            m_in.m_audio.m_stream->duration = av_rescale_q(m_in.m_format_ctx->duration, av_get_time_base_q(), m_in.m_audio.m_stream->time_base);
        }
#endif // USE_LIBDVD
#ifdef USE_LIBBLURAY
        if (m_virtualfile->m_type == VIRTUALTYPE_BLURAY)
        {
            // FFmpeg API calculcates a wrong duration, so use value from Bluray directory
            m_in.m_audio.m_stream->duration = av_rescale_q(m_in.m_format_ctx->duration, av_get_time_base_q(), m_in.m_audio.m_stream->time_base);
        }
#endif // USE_LIBBLURAY

        audio_info(false, m_in.m_format_ctx, m_in.m_audio.m_stream);
    }

    if (m_in.m_audio.m_stream_idx == INVALID_STREAM && m_in.m_video.m_stream_idx == INVALID_STREAM)
    {
        Logging::error(filename(), "File contains neither a video nor an audio stream.");
        return AVERROR(EINVAL);
    }

    // Predict size of transcoded file as exact as possible
    m_virtualfile->m_predicted_size = calculate_predicted_filesize();

    // Calculate number or video frames in file based on duration and frame rate
    if (m_in.m_video.m_stream != nullptr && m_in.m_video.m_stream->avg_frame_rate.den)
    {
        // Number of frames: should be quite accurate
        m_virtualfile->m_video_frame_count = static_cast<uint32_t>(av_rescale_q(m_in.m_video.m_stream->duration, m_in.m_video.m_stream->time_base, av_inv_q(m_in.m_video.m_stream->avg_frame_rate)));
    }

    // Make sure this is set, although should already have happened
    m_virtualfile->m_format_idx = params.guess_format_idx(filename());

    // Open album art streams if present and supported by both source and target
    if (!params.m_noalbumarts && m_in.m_audio.m_stream != nullptr)
    {
        for (int stream_idx = 0; stream_idx < static_cast<int>(m_in.m_format_ctx->nb_streams); stream_idx++)
        {
            AVStream *input_stream = m_in.m_format_ctx->streams[stream_idx];

            if (is_album_art(CODECPAR(input_stream)->codec_id, &input_stream->r_frame_rate))
            {
                STREAMREF streamref;
                AVCodecContext * input_codec_ctx;

                Logging::trace(filename(), "Found album art");

                ret = open_decoder(&input_codec_ctx, stream_idx, nullptr, AVMEDIA_TYPE_VIDEO);
                if (ret < 0)
                {
                    Logging::error(filename(), "Failed to open album art codec (error '%1').", ffmpeg_geterror(ret).c_str());
                    return ret;
                }

                streamref.m_codec_ctx  = input_codec_ctx;
                streamref.m_stream     = input_stream;
                streamref.m_stream_idx = input_stream->index;

                m_in.m_album_art.push_back(streamref);
            }
        }
    }

    if (m_virtualfile->m_flags & VIRTUALFLAG_CUESHEET)
    {
        // Position to start of cue sheet track
        ret = av_seek_frame(m_in.m_format_ctx, -1, m_virtualfile->m_cuesheet.m_start, 0);
        if (ret < 0)
        {
            Logging::error(filename(), "Failed to seek track start (error '%1').", ffmpeg_geterror(ret).c_str());
            return ret;
        }
    }

    return 0;
}

bool FFmpeg_Transcoder::can_copy_stream(const AVStream *stream) const
{
    if (params.m_autocopy == AUTOCOPY_OFF)
    {
        // Auto copy disabled
        return false;
    }

    if (stream == nullptr)
    {
        // Should normally not happen: Input stream stream unknown, no way to check - no auto copy
        return false;
    }

    if ((params.m_autocopy == AUTOCOPY_MATCH || params.m_autocopy == AUTOCOPY_MATCHLIMIT))
    {
        // Any codec supported by output format OK
        const AVOutputFormat* oformat = av_guess_format(nullptr, destname(), nullptr);
        if (oformat->codec_tag == nullptr ||
                av_codec_get_tag(oformat->codec_tag, CODECPAR(stream)->codec_id) <= 0)
        {
            // Codec not supported - no auto copy
            return false;
        }
    }
    else if ((params.m_autocopy == AUTOCOPY_STRICT || params.m_autocopy == AUTOCOPY_STRICTLIMIT))
    {
        // Output codec must strictly match
        if (CODECPAR(stream)->codec_id != m_current_format->audio_codec_id())
        {
            // Different codecs - no auto copy
            return false;
        }
    }

    if (params.m_autocopy == AUTOCOPY_MATCHLIMIT || params.m_autocopy == AUTOCOPY_STRICTLIMIT)
    {
        BITRATE orig_bit_rate = (CODECPAR(stream)->bit_rate != 0) ? CODECPAR(stream)->bit_rate : m_in.m_format_ctx->bit_rate;
        if (get_output_bit_rate(orig_bit_rate, params.m_audiobitrate))
        {
            // Bit rate changed, no auto copy
            Logging::info(destname(), "Bit rate changed, no auto copy possible.");
            return false;
        }
    }

    return true;
}

int FFmpeg_Transcoder::open_output_file(Buffer *buffer)
{
    assert(buffer != nullptr);

    m_out.m_filetype    = m_current_format->filetype();

    Logging::debug(destname(), "Opening output file.");

    if (m_in.m_audio.m_stream_idx == INVALID_STREAM && m_current_format->video_codec_id() == AV_CODEC_ID_NONE)
    {
        Logging::error(destname(), "Unable to transcode, source contains no audio stream, but target just supports audio.");
        m_virtualfile->m_flags |= VIRTUALFLAG_HIDDEN;   // Hide file from now on
        return AVERROR(ENOENT);                         // Report file not found
    }

    if (!is_frameset())
    {
        // Not a frame set, open regular buffer
        return open_output(buffer);
    }
    else
    {
        Logging::debug(destname(), "Opening frame set type '%1'.", m_current_format->desttype().c_str());

        // Open frame set buffer
        return open_output_frame_set(buffer);
    }
}

int FFmpeg_Transcoder::open_bestmatch_decoder(AVCodecContext **avctx, int *stream_idx, AVMediaType type)
{
#if IF_DECLARED_CONST
    const AVCodec *input_codec = nullptr;
#else // !IF_DECLARED_CONST
    AVCodec *input_codec = nullptr;
#endif // !IF_DECLARED_CONST
    int ret;

    ret = av_find_best_stream(m_in.m_format_ctx, type, INVALID_STREAM, INVALID_STREAM, &input_codec, 0);
    if (ret < 0)
    {
        if (ret != AVERROR_STREAM_NOT_FOUND)    // Not an error
        {
            Logging::error(filename(), "Could not find %1 stream in input file (error '%2').", get_media_type_string(type), ffmpeg_geterror(ret).c_str());
        }
        return ret;
    }

    *stream_idx = ret;

    return open_decoder(avctx, *stream_idx, input_codec, type);
}

#if IF_DECLARED_CONST
AVPixelFormat FFmpeg_Transcoder::get_hw_pix_fmt(const AVCodec *codec, AVHWDeviceType dev_type, bool use_device_ctx) const
#else // !IF_DECLARED_CONST
AVPixelFormat FFmpeg_Transcoder::get_hw_pix_fmt(AVCodec *codec, AVHWDeviceType dev_type, bool use_device_ctx) const
#endif // !IF_DECLARED_CONST
{
    AVPixelFormat hw_pix_fmt = AV_PIX_FMT_NONE;

    if (codec != nullptr && dev_type != AV_HWDEVICE_TYPE_NONE)
    {
        int method = use_device_ctx ? AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX : AV_CODEC_HW_CONFIG_METHOD_HW_FRAMES_CTX;

        for (int i = 0;; i++)
        {
            const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
            if (!config)
            {
                Logging::error(av_codec_is_decoder(codec) ? filename() : destname(), "%1 '%2' does not support device type %3.\n", av_codec_is_encoder(codec) ? "Encoder" : "Decoder", codec->name, hwdevice_get_type_name(dev_type));
                break;
            }

            if ((config->methods & method) && (config->device_type == dev_type))
            {
                hw_pix_fmt = config->pix_fmt;
                Logging::info(av_codec_is_decoder(codec) ? filename() : destname(), "%1 '%2' requests %3 for device type %4.\n", av_codec_is_encoder(codec) ? "Encoder" : "Decoder", codec->name, av_get_pix_fmt_name(hw_pix_fmt), hwdevice_get_type_name(dev_type));
                break;
            }
        }
    }

    return hw_pix_fmt;
}

#if IF_DECLARED_CONST
int FFmpeg_Transcoder::open_decoder(AVCodecContext **avctx, int stream_idx, const AVCodec *input_codec, AVMediaType type)
#else // !IF_DECLARED_CONST
int FFmpeg_Transcoder::open_decoder(AVCodecContext **avctx, int stream_idx, AVCodec *input_codec, AVMediaType type)
#endif // !IF_DECLARED_CONST
{
    while (true)
    {
        AVCodecContext *input_codec_ctx     = nullptr;
        AVStream *      input_stream        = nullptr;
        AVDictionary *  opt                 = nullptr;
        AVCodecID       codec_id            = AV_CODEC_ID_NONE;
        int ret;

        input_stream = m_in.m_format_ctx->streams[stream_idx];

        // Init the decoders, with or without reference counting
        // av_dict_set_with_check(&opt, "refcounted_frames", refcount ? "1" : "0", 0);

#if LAVF_DEP_AVSTREAM_CODEC
        // allocate a new decoding context
        input_codec_ctx = avcodec_alloc_context3(nullptr);
        if (input_codec_ctx == nullptr)
        {
            Logging::error(filename(), "Could not allocate a decoding context.");
            return AVERROR(ENOMEM);
        }

        // initialise the stream parameters with demuxer information
        ret = avcodec_parameters_to_context(input_codec_ctx, input_stream->codecpar);
        if (ret < 0)
        {
            return ret;
        }

        codec_id = input_stream->codecpar->codec_id;
#else
        input_codec_ctx = input_stream->codec;

        codec_id = input_codec_ctx->codec_id;
#endif

        if (type == AVMEDIA_TYPE_VIDEO && m_hwaccel_dec_mode != HWACCELMODE_FALLBACK)
        {
            // Decide whether to use a hardware decoder
            // Check to see if decoder hardware acceleration is both requested and supported by codec.
            std::string hw_decoder_codec_name;
            if (!get_hw_decoder_name(input_codec_ctx->codec_id, &hw_decoder_codec_name))
            {
                m_dec_hw_pix_fmt = get_hw_pix_fmt(input_codec, params.m_hwaccel_dec_device_type, true);

                m_hwaccel_enable_dec_buffering = (params.m_hwaccel_dec_device_type != AV_HWDEVICE_TYPE_NONE && m_dec_hw_pix_fmt != AV_PIX_FMT_NONE);
                //{
                //    std::string fourcc2str;
                //    fourcc_make_string(&fourcc2str, input_codec_ctx->codec_tag);
                //    fprintf(stderr, "fourcc2str %s\n", fourcc2str.c_str());
                //}
            }

            if (m_hwaccel_enable_dec_buffering)
            {
                // Hardware buffers available, enabling decoder hardware acceleration.
                Logging::info(filename(), "Hardware decoder frame buffering %1 enabled.", get_hwaccel_API_text(params.m_hwaccel_dec_API).c_str());
                ret = hwdevice_ctx_create(&m_hwaccel_dec_device_ctx, params.m_hwaccel_dec_device_type, params.m_hwaccel_dec_device);
                if (ret < 0)
                {
                    Logging::error(filename(), "Failed to create a %1 device for decoding (error %2).", get_hwaccel_API_text(params.m_hwaccel_dec_API).c_str(), ffmpeg_geterror(ret).c_str());
                    return ret;
                }
                Logging::debug(filename(), "Hardware decoder acceleration and frame buffering active using codec '%1'.", input_codec->name);

                m_hwaccel_dec_mode = HWACCELMODE_ENABLED; // Hardware acceleration active
            }
            else if (params.m_hwaccel_dec_device_type != AV_HWDEVICE_TYPE_NONE)
            {
                // No hardware acceleration, fallback to software,
                Logging::info(filename(), "Hardware decoder frame buffering %1 not supported by codec '%2'. Falling back to software.", get_hwaccel_API_text(params.m_hwaccel_dec_API).c_str(), get_codec_name(input_codec_ctx->codec_id, true));
            }
            else if (!hw_decoder_codec_name.empty())
            {
                // No frame buffering (e.g. OpenMAX or MMAL), but hardware acceleration possible.
                Logging::info(filename(), "Hardware decoder acceleration active using codec '%1'.", hw_decoder_codec_name.c_str());

                // Open hw_decoder_codec_name codec here
                input_codec = avcodec_find_decoder_by_name(hw_decoder_codec_name.c_str());

                if (input_codec == nullptr)
                {
                    Logging::error(filename(), "Could not find decoder '%1'.", hw_decoder_codec_name.c_str());
                    return AVERROR(EINVAL);
                }

                Logging::info(filename(), "Hardware decoder acceleration enabled. Codec '%1'.", input_codec->name);

                m_hwaccel_dec_mode = HWACCELMODE_ENABLED; // Hardware acceleration active
            }

            if (m_hwaccel_enable_dec_buffering)
            {
                ret = hwdevice_ctx_add_ref(input_codec_ctx);
                if (ret < 0)
                {
                    return ret;
                }
            }
        }

        if (input_codec == nullptr)
        {
            // Find a decoder for the stream.
            input_codec = avcodec_find_decoder(codec_id);

            if (input_codec == nullptr)
            {
                Logging::error(filename(), "Failed to find %1 input codec '%2'.", get_media_type_string(type), avcodec_get_name(codec_id));
                return AVERROR(EINVAL);
            }
        }

        input_codec_ctx->codec_id = input_codec->id;

        //input_codec_ctx->time_base = input_stream->time_base;

        ret = avcodec_open2(input_codec_ctx, input_codec, &opt);

        av_dict_free(&opt);

        if (ret < 0)
        {
            if (m_hwaccel_dec_mode == HWACCELMODE_ENABLED)
            {
                Logging::info(filename(), "Unable to use %1 input codec '%2' with hardware acceleration. Falling back to software.", get_media_type_string(type), avcodec_get_name(codec_id));

                m_hwaccel_dec_mode              = HWACCELMODE_FALLBACK;
                m_hwaccel_enable_dec_buffering  = false;
                m_dec_hw_pix_fmt                = AV_PIX_FMT_NONE;

                // Free hardware device contexts if open
                hwdevice_ctx_free(&m_hwaccel_dec_device_ctx);

                avcodec_close(m_in.m_video.m_codec_ctx);
                avcodec_free_context(&m_in.m_video.m_codec_ctx);
                m_in.m_video.m_codec_ctx = nullptr;

                // Try again with a software decoder
                continue;
            }

            Logging::error(filename(), "Failed to open %1 input codec for stream #%1 (error '%2').", get_media_type_string(type), input_stream->index, ffmpeg_geterror(ret).c_str());
            return ret;
        }

        Logging::debug(filename(), "Opened input codec for stream #%1: %2", input_stream->index, get_codec_name(codec_id, true));

        *avctx = input_codec_ctx;

        return 0;
    };
}

int FFmpeg_Transcoder::open_output_frame_set(Buffer *buffer)
{
    const AVCodec * output_codec        = nullptr;
    AVCodecContext *output_codec_ctx    = nullptr;
    int ret = 0;

    m_buffer            = buffer;
    {
        std::lock_guard<std::recursive_mutex> lck (m_seek_to_fifo_mutex);
        while (m_seek_to_fifo.size())
        {
            m_seek_to_fifo.pop();
        }
    }
    m_have_seeked       = false;

    output_codec = avcodec_find_encoder(m_current_format->video_codec_id());
    if (output_codec == nullptr)
    {
        Logging::error(destname(), "Codec not found");
        return AVERROR(EINVAL);
    }

    output_codec_ctx = avcodec_alloc_context3(output_codec);
    if (output_codec_ctx == nullptr)
    {
        Logging::error(destname(), "Could not allocate video codec context");
        return AVERROR(ENOMEM);
    }

    output_codec_ctx->bit_rate             = 400000;   /**  @todo Make frame image compression rate command line settable */
    output_codec_ctx->width                = m_in.m_video.m_codec_ctx->width;
    output_codec_ctx->height               = m_in.m_video.m_codec_ctx->height;
    output_codec_ctx->time_base            = {1, 25};

    const AVPixFmtDescriptor *dst_desc = av_pix_fmt_desc_get(m_in.m_video.m_codec_ctx->pix_fmt);
    int loss = 0;

    output_codec_ctx->pix_fmt = avcodec_find_best_pix_fmt_of_list(output_codec->pix_fmts, m_in.m_video.m_codec_ctx->pix_fmt, dst_desc->flags & AV_PIX_FMT_FLAG_ALPHA, &loss);

    if (output_codec_ctx->pix_fmt == AV_PIX_FMT_NONE)
    {
        // No best match found, use default
        switch (m_current_format->video_codec_id())
        {
        case AV_CODEC_ID_MJPEG:
        {
            output_codec_ctx->pix_fmt   = AV_PIX_FMT_YUVJ444P;
            break;
        }
        case AV_CODEC_ID_PNG:
        {
            output_codec_ctx->pix_fmt   = AV_PIX_FMT_RGB24;
            break;
        }
        case AV_CODEC_ID_BMP:
        {
            output_codec_ctx->pix_fmt   = AV_PIX_FMT_BGR24;
            break;
        }
        default:
        {
            assert(false);
            break;
        }
        }
        Logging::debug(destname(), "No best match output pixel format found, using default: %1", get_pix_fmt_name(output_codec_ctx->pix_fmt).c_str());
    }
    else
    {
        Logging::debug(destname(), "Output pixel format: %1", get_pix_fmt_name(output_codec_ctx->pix_fmt).c_str());
    }

    //codec_context->sample_aspect_ratio  = frame->sample_aspect_ratio;
    //codec_context->sample_aspect_ratio  = m_in.m_video.m_codec_ctx->sample_aspect_ratio;

    ret = avcodec_open2(output_codec_ctx, output_codec, nullptr);
    if (ret < 0)
    {
        Logging::error(destname(), "Could not open image codec.");
        return ret;
    }

    // Initialise pixel format conversion and rescaling if necessary
    get_pix_formats(&m_in.m_pix_fmt, &m_out.m_pix_fmt, output_codec_ctx);

    ret = init_rescaler(m_in.m_pix_fmt, CODECPAR(m_in.m_video.m_stream)->width, CODECPAR(m_in.m_video.m_stream)->height, m_out.m_pix_fmt, output_codec_ctx->width, output_codec_ctx->height);
    if (ret < 0)
    {
        return ret;
    }

    if (params.m_deinterlace)
    {
        ret = init_deinterlace_filters(output_codec_ctx, m_in.m_pix_fmt, m_in.m_video.m_stream->avg_frame_rate, m_in.m_video.m_stream->time_base);
        if (ret < 0)
        {
            return ret;
        }
    }

    m_out.m_video.m_codec_ctx               = output_codec_ctx;
    m_out.m_video.m_stream_idx              = INVALID_STREAM;
    m_out.m_video.m_stream                  = nullptr;

    // No audio
    m_out.m_audio.m_codec_ctx               = nullptr;
    m_out.m_audio.m_stream_idx              = INVALID_STREAM;
    m_out.m_audio.m_stream                  = nullptr;

    // Open for read/write
    if (!buffer->open_file(0, CACHE_FLAG_RW))
    {
        return AVERROR(EPERM);
    }

    // Pre-allocate the predicted file size to reduce memory reallocations
    size_t buffsize = 600 * 1024  * 1024 /*predicted_filesize() * m_video_frame_count*/;
    if (buffer->size() < buffsize && !buffer->reserve(buffsize))
    {
        int _errno = errno;
        Logging::error(destname(), "Error pre-allocating %1 bytes buffer: (%2) %3", buffsize, errno, strerror(errno));
        return AVERROR(_errno);
    }

    return 0;
}

int FFmpeg_Transcoder::open_output(Buffer *buffer)
{
    int ret = 0;

    m_buffer            = buffer;

    if (!m_out.m_video_pts && is_hls())
    {
        m_current_segment = 1;
        Logging::info(destname(), "Starting HLS segment no. %1.", m_current_segment);
    }

    while (true)
    {
        // Open the output file for writing. If buffer == nullptr continue using existing buffer.
        ret = open_output_filestreams(buffer);
        if (ret)
        {
            if (m_hwaccel_enc_mode == HWACCELMODE_ENABLED)
            {
                Logging::info(filename(), "Unable to use ouput codec '%1' with hardware acceleration. Falling back to software.", avcodec_get_name(m_current_format->video_codec_id()));

                m_hwaccel_enc_mode              = HWACCELMODE_FALLBACK;
                m_hwaccel_enable_enc_buffering  = false;
                m_enc_hw_pix_fmt                = AV_PIX_FMT_NONE;

                // Free hardware device contexts if open
                hwdevice_ctx_free(&m_hwaccel_enc_device_ctx);

                close_output_file();

                // Try again with a software decoder
                continue;
            }
            return ret;
        }
        break;
    }

    if (m_out.m_audio.m_stream_idx != INVALID_STREAM)
    {
        audio_info(true, m_out.m_format_ctx, m_out.m_audio.m_stream);

        if (m_out.m_audio.m_codec_ctx != nullptr)
        {
            // If not just copying the stream, initialise the FIFO buffer to store audio samples to be encoded.
            ret = init_fifo();
            if (ret)
            {
                return ret;
            }
        }
    }

    if (m_out.m_video.m_stream_idx != INVALID_STREAM)
    {
        video_info(true, m_out.m_format_ctx, m_out.m_video.m_stream);
    }

    // Open for read/write
    if (!buffer->open_file(0, CACHE_FLAG_RW))
    {
        return AVERROR(EPERM);
    }

    // Pre-allocate the predicted file size to reduce memory reallocations
    size_t buffsize = predicted_filesize();
    if (buffer->size() < buffsize && !buffer->reserve(buffsize))
    {
        int _errno = errno;
        Logging::error(destname(), "Error pre-allocating %1 bytes buffer: (%2) %3", buffsize, errno, strerror(errno));
        return AVERROR(_errno);
    }

    ret = process_output();
    if (ret)
    {
        return ret;
    }

    // process_output() calls avformat_write_header which feels free to change the stream time bases sometimes.
    // This means we have to do the following calculations here to use the correct values, otherwise this can cause
    // a lot of havoc.

    if (m_in.m_audio.m_stream != nullptr && m_out.m_audio.m_stream != nullptr && m_in.m_audio.m_stream->start_time != AV_NOPTS_VALUE)
    {
        m_in.m_audio_start_time                 = m_in.m_audio.m_stream->start_time;
        m_out.m_audio_start_time                = av_rescale_q_rnd(m_in.m_audio.m_stream->start_time, m_in.m_audio.m_stream->time_base, m_out.m_audio.m_stream->time_base, static_cast<AVRounding>(AV_ROUND_UP | AV_ROUND_PASS_MINMAX));
        m_out.m_audio.m_stream->start_time     = m_out.m_audio_start_time;
    }
    else
    {
        m_in.m_audio_start_time                 = 0;
        m_out.m_audio_start_time                = 0;
        if (m_out.m_audio.m_stream)
        {
            m_out.m_audio.m_stream->start_time  = AV_NOPTS_VALUE;
        }
    }

    if (m_in.m_video.m_stream != nullptr && m_out.m_video.m_stream != nullptr && m_in.m_video.m_stream->start_time != AV_NOPTS_VALUE)
    {
        m_in.m_video_start_time                 = m_in.m_video.m_stream->start_time;
        m_out.m_video_start_time                = av_rescale_q_rnd(m_in.m_video.m_stream->start_time, m_in.m_video.m_stream->time_base, m_out.m_video.m_stream->time_base, static_cast<AVRounding>(AV_ROUND_UP | AV_ROUND_PASS_MINMAX));
        m_out.m_video.m_stream->start_time      = m_out.m_video_start_time;
    }
    else
    {
        m_in.m_video_start_time                 = 0;
        m_out.m_video_start_time                = 0;
        if (m_out.m_video.m_stream)
        {
            m_out.m_video.m_stream->start_time  = AV_NOPTS_VALUE;
        }
    }

    m_out.m_audio_pts         = m_out.m_audio_start_time;
    m_out.m_video_pts         = m_out.m_video_start_time;
    m_out.m_last_mux_dts      = AV_NOPTS_VALUE;

    return 0;
}

int FFmpeg_Transcoder::process_output()
{
    // Process metadata. The decoder will call the encoder to set appropriate
    // tag values for the output file.
    int ret = process_metadata();
    if (ret)
    {
        return ret;
    }

    // Write the header of the output file container.
    ret = write_output_file_header();
    if (ret)
    {
        return ret;
    }

    // Process album arts: copy all from source file to target.
    return process_albumarts();
}

bool FFmpeg_Transcoder::get_output_sample_rate(int input_sample_rate, int max_sample_rate, int *output_sample_rate /*= nullptr*/)
{
    if (input_sample_rate > max_sample_rate)
    {
        if (output_sample_rate != nullptr)
        {
            *output_sample_rate = max_sample_rate;
        }
        return true;
    }
    else
    {
        if (output_sample_rate != nullptr)
        {
            *output_sample_rate = input_sample_rate;
        }
        return false;
    }
}

bool FFmpeg_Transcoder::get_output_bit_rate(BITRATE input_bit_rate, BITRATE max_bit_rate, BITRATE * output_bit_rate /*= nullptr*/)
{
    if (!input_bit_rate || input_bit_rate > max_bit_rate)
    {
        if (output_bit_rate != nullptr)
        {
            *output_bit_rate = max_bit_rate;
        }
        return true;
    }
    else
    {
        if (output_bit_rate != nullptr)
        {
            *output_bit_rate = input_bit_rate;
        }
        return false;
    }
}

bool FFmpeg_Transcoder::get_aspect_ratio(int width, int height, const AVRational & sar, AVRational *ar) const
{
    // Try to determine display aspect ratio
    AVRational dar;
    av_reduce(&dar.num, &dar.den,
              width  * sar.num,
              height * sar.den,
              1024 * 1024);

    ar->num = ar->den = 0;

    if (dar.num && dar.den)
    {
        *ar = dar;
    }

    // If that fails, try sample aspect ratio instead
    if (!ar->den && sar.num != 0 && sar.den != 0)
    {
        *ar = sar;
    }

    // If even that fails, try to use video size
    if (!ar->den && height)
    {
        ar->num = width;
        ar->den = height;
    }

    if (!ar->den)
    {
        // Return false if all above failed
        return false;
    }

    av_reduce(&ar->num, &ar->den,
              ar->num,
              ar->den,
              1024 * 1024);

    return true;
}

bool FFmpeg_Transcoder::get_video_size(int *output_width, int *output_height) const
{
    if (!params.m_videowidth && !params.m_videoheight)
    {
        // No options, leave as is
        return false;
    }

    int input_width     = CODECPAR(m_in.m_video.m_stream)->width;
    int input_height    = CODECPAR(m_in.m_video.m_stream)->height;
    AVRational sar      = CODECPAR(m_in.m_video.m_stream)->sample_aspect_ratio;

    if (params.m_videowidth && params.m_videoheight)
    {
        // Both width/source set. May look strange, but this is an order...
        *output_width   = params.m_videowidth;
        *output_height  = params.m_videoheight;
    }
    else if (params.m_videowidth)
    {
        // Only video width
        AVRational ar;

        *output_width      = params.m_videowidth;

        if (!get_aspect_ratio(input_width, input_height, sar, &ar))
        {
            *output_height = input_height;
        }
        else
        {
            *output_height = static_cast<int>(params.m_videowidth / av_q2d(ar));
            *output_height &= ~(static_cast<int>(0x1)); // height must be multiple of 2
        }
    }
    else //if (params.m_videoheight)
    {
        // Only video height
        AVRational ar;

        if (!get_aspect_ratio(input_width, input_height, sar, &ar))
        {
            *output_width  = input_width;
        }
        else
        {
            *output_width  = static_cast<int>(params.m_videoheight / av_q2d(ar));
            *output_width  &= ~(static_cast<int>(0x1)); // width must be multiple of 2
        }
        *output_height     = params.m_videoheight;
    }

    return (input_width > *output_width || input_height > *output_height);
}

int FFmpeg_Transcoder::update_codec(void *opt, LPCPROFILE_OPTION profile_option) const
{
    int ret = 0;

    if (profile_option == nullptr)
    {
        return 0;
    }

    for (LPCPROFILE_OPTION p = profile_option; p->m_key != nullptr; p++)
    {
        if ((m_hwaccel_enable_enc_buffering && p->m_options & OPT_SW_ONLY) || (!m_hwaccel_enable_enc_buffering && p->m_options & OPT_HW_ONLY))
        {
            continue;
        }

        Logging::trace(destname(), "Profile codec option -%1%2%3.", p->m_key, *p->m_value ? " " : "", p->m_value);

        ret = opt_set_with_check(opt, p->m_key, p->m_value, p->m_flags, destname());
        if (ret < 0)
        {
            break;
        }
    }
    return ret;
}

int FFmpeg_Transcoder::prepare_codec(void *opt, FILETYPE filetype) const
{
    int ret = 0;

    for (int n = 0; m_profile[n].m_profile != PROFILE_INVALID; n++)
    {
        if (m_profile[n].m_filetype == filetype && m_profile[n].m_profile == params.m_profile)
        {
            ret = update_codec(opt, m_profile[n].m_option_codec);
            break;
        }
    }

    return ret;
}

int FFmpeg_Transcoder::init_rescaler(AVPixelFormat in_pix_fmt, int in_width, int in_height, AVPixelFormat out_pix_fmt, int out_width, int out_height)
{
    if (in_pix_fmt != out_pix_fmt || in_width != out_width || in_height != out_height)
    {
        // Rescale image if required
        if (in_pix_fmt != out_pix_fmt)
        {
            Logging::trace(destname(), "Initialising pixel format conversion from %1 to %2.", get_pix_fmt_name(in_pix_fmt).c_str(), get_pix_fmt_name(out_pix_fmt).c_str());
        }

        if (in_width != out_width || in_height != out_height)
        {
            Logging::debug(destname(), "Rescaling video size from %1:%2 to %3:%4.",
                           in_width, in_height,
                           out_width, out_height);
        }

        m_sws_ctx = sws_getContext(
                    // Source settings
                    in_width,               // width
                    in_height,              // height
                    in_pix_fmt,             // format
                    // Target settings
                    out_width,              // width
                    out_height,             // height
                    out_pix_fmt,            // format
                    SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);    // Maybe SWS_LANCZOS | SWS_ACCURATE_RND
        if (m_sws_ctx == nullptr)
        {
            Logging::error(destname(), "Could not allocate scaling/conversion context.");
            return AVERROR(ENOMEM);
        }
    }

    return 0;
}

int FFmpeg_Transcoder::add_stream(AVCodecID codec_id)
{
    AVCodecContext *output_codec_ctx    = nullptr;
    AVStream *      output_stream       = nullptr;
#if IF_DECLARED_CONST
    const AVCodec * output_codec        = nullptr;
#else // !IF_DECLARED_CONST
    AVCodec * output_codec              = nullptr;
#endif // !IF_DECLARED_CONST
    AVDictionary *  opt                 = nullptr;
    int ret;

    std::string codec_name;

    if (get_hw_encoder_name(codec_id, &codec_name) || m_hwaccel_enc_mode == HWACCELMODE_FALLBACK)
    {
        // find the encoder
        output_codec = avcodec_find_encoder(codec_id);

        if (output_codec == nullptr)
        {
            Logging::error(destname(), "Could not find encoder '%1'.", avcodec_get_name(codec_id));
            return AVERROR(EINVAL);
        }
    }
    else
    {
        output_codec = avcodec_find_encoder_by_name(codec_name.c_str());

        if (output_codec == nullptr)
        {
            Logging::error(destname(), "Could not find encoder '%1'.", codec_name.c_str());
            return AVERROR(EINVAL);
        }

        Logging::info(destname(), "Hardware encoder acceleration enabled. Codec '%1'.", output_codec->name);

        m_hwaccel_enc_mode = HWACCELMODE_ENABLED;
    }

    output_stream = avformat_new_stream(m_out.m_format_ctx, output_codec);
    if (output_stream == nullptr)
    {
        Logging::error(destname(), "Could not allocate stream for encoder '%1'.",  avcodec_get_name(codec_id));
        return AVERROR(ENOMEM);
    }
    output_stream->id = static_cast<int>(m_out.m_format_ctx->nb_streams - 1);

#if FFMPEG_VERSION3 // Check for FFmpeg 3
    output_codec_ctx = avcodec_alloc_context3(output_codec);
    if (output_codec_ctx == nullptr)
    {
        Logging::error(destname(), "Could not alloc an encoding context.");
        return AVERROR(ENOMEM);
    }
#else
    output_codec_ctx = output_stream->codec;
#endif

    switch (output_codec->type)
    {
    case AVMEDIA_TYPE_AUDIO:
    {
        BITRATE orig_bit_rate;
        int orig_sample_rate;

        // Set the basic encoder parameters
        orig_bit_rate = (CODECPAR(m_in.m_audio.m_stream)->bit_rate != 0) ? CODECPAR(m_in.m_audio.m_stream)->bit_rate : m_in.m_format_ctx->bit_rate;
        if (get_output_bit_rate(orig_bit_rate, params.m_audiobitrate, &output_codec_ctx->bit_rate))
        {
            // Limit bit rate
            Logging::trace(destname(), "Limiting audio bit rate from %1 to %2.",
                           format_bitrate(orig_bit_rate).c_str(),
                           format_bitrate(output_codec_ctx->bit_rate).c_str());
        }

        if (params.m_audiochannels > 0 && m_in.m_audio.m_codec_ctx->channels > params.m_audiochannels)
        {
            Logging::trace(destname(), "Limiting audio channels from %1 to %2.",
                           m_in.m_audio.m_codec_ctx->channels,
                           params.m_audiochannels);
            output_codec_ctx->channels         = params.m_audiochannels;
        }
        else
        {
            output_codec_ctx->channels          = m_in.m_audio.m_codec_ctx->channels;
        }

        output_codec_ctx->channel_layout        = static_cast<uint64_t>(av_get_default_channel_layout(output_codec_ctx->channels));
        output_codec_ctx->sample_rate           = m_in.m_audio.m_codec_ctx->sample_rate;
        orig_sample_rate                        = m_in.m_audio.m_codec_ctx->sample_rate;
        if (get_output_sample_rate(CODECPAR(m_in.m_audio.m_stream)->sample_rate, params.m_audiosamplerate, &output_codec_ctx->sample_rate))
        {
            // Limit sample rate
            Logging::trace(destname(), "Limiting audio sample rate from %1 to %2.",
                           format_samplerate(orig_sample_rate).c_str(),
                           format_samplerate(output_codec_ctx->sample_rate).c_str());
            orig_sample_rate = output_codec_ctx->sample_rate;
        }

        if (output_codec->supported_samplerates != nullptr)
        {
            // Go through supported sample rates and adjust if necessary
            bool supported = false;

            for (int n = 0; output_codec->supported_samplerates[n] != 0; n++)
            {
                if (output_codec->supported_samplerates[n] == output_codec_ctx->sample_rate)
                {
                    // Is supported
                    supported = true;
                    break;
                }
            }

            if (!supported)
            {
                int min_samplerate = 0;
                int max_samplerate = INT_MAX;

                // Find next lower sample rate in probably unsorted list
                for (int n = 0; output_codec->supported_samplerates[n] != 0; n++)
                {
                    if (min_samplerate <= output_codec->supported_samplerates[n] && output_codec_ctx->sample_rate >= output_codec->supported_samplerates[n])
                    {
                        min_samplerate = output_codec->supported_samplerates[n];
                    }
                }

                // Find next higher sample rate in probably unsorted list
                for (int n = 0; output_codec->supported_samplerates[n] != 0; n++)
                {
                    if (max_samplerate >= output_codec->supported_samplerates[n] && output_codec_ctx->sample_rate <= output_codec->supported_samplerates[n])
                    {
                        max_samplerate = output_codec->supported_samplerates[n];
                    }
                }

                if (min_samplerate != 0 && max_samplerate != INT_MAX)
                {
                    // set to nearest value
                    if (output_codec_ctx->sample_rate - min_samplerate < max_samplerate - output_codec_ctx->sample_rate)
                    {
                        output_codec_ctx->sample_rate = min_samplerate;
                    }
                    else
                    {
                        output_codec_ctx->sample_rate = max_samplerate;
                    }
                }
                else if (min_samplerate != 0)
                {
                    // No higher sample rate, use next lower
                    output_codec_ctx->sample_rate = min_samplerate;
                }
                else if (max_samplerate != INT_MAX)
                {
                    // No lower sample rate, use higher lower
                    output_codec_ctx->sample_rate = max_samplerate;
                }
                else
                {
                    // Should never happen... There must at least be one.
                    Logging::error(destname(), "Audio sample rate to %1 not supported by codec.", format_samplerate(output_codec_ctx->sample_rate).c_str());
                    return AVERROR(EINVAL);
                }

                Logging::debug(destname(), "Changed audio sample rate from %1 to %2 because requested value is not supported by codec.",
                               format_samplerate(orig_sample_rate).c_str(),
                               format_samplerate(output_codec_ctx->sample_rate).c_str());
            }
        }

        if (output_codec->sample_fmts != nullptr)
        {
            // Check if input sample format is supported and if so, use it (avoiding resampling)
            AVSampleFormat input_fmt_planar = av_get_planar_sample_fmt(m_in.m_audio.m_codec_ctx->sample_fmt);

            output_codec_ctx->sample_fmt        = AV_SAMPLE_FMT_NONE;

            for (const AVSampleFormat *sample_fmt = output_codec->sample_fmts; *sample_fmt != -1; sample_fmt++)
            {
                AVSampleFormat output_fmt_planar = av_get_planar_sample_fmt(*sample_fmt);

                if (*sample_fmt == m_in.m_audio.m_codec_ctx->sample_fmt ||
                        (input_fmt_planar != AV_SAMPLE_FMT_NONE &&
                         input_fmt_planar == output_fmt_planar))
                {
                    output_codec_ctx->sample_fmt    = *sample_fmt;
                    break;
                }
            }

            // If none of the supported formats match use the first supported
            if (output_codec_ctx->sample_fmt == AV_SAMPLE_FMT_NONE)
            {
                output_codec_ctx->sample_fmt    = output_codec->sample_fmts[0];
            }
        }
        else
        {
            // If suppported sample formats are unknown simply take input format and cross our fingers it works...
            output_codec_ctx->sample_fmt        = m_in.m_audio.m_codec_ctx->sample_fmt;
        }

        // Set the sample rate for the container.
        output_stream->time_base.den            = output_codec_ctx->sample_rate;
        output_stream->time_base.num            = 1;
        output_codec_ctx->time_base             = output_stream->time_base;

        //#if !FFMPEG_VERSION3 // Check for FFmpeg 3
        // set -strict -2 for aac (required for FFmpeg 2)
        dict_set_with_check(&opt, "strict", "-2", 0);

        // Allow the use of the experimental AAC encoder
        output_codec_ctx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
        //#endif

        // Set duration as hint for muxer
        if (m_in.m_audio.m_stream->duration != AV_NOPTS_VALUE)
        {
            output_stream->duration             = av_rescale_q(m_in.m_audio.m_stream->duration, m_in.m_audio.m_stream->time_base, output_stream->time_base);
        }
        else if (m_in.m_format_ctx->duration != AV_NOPTS_VALUE)
        {
            output_stream->duration             = av_rescale_q(m_in.m_format_ctx->duration, av_get_time_base_q(), output_stream->time_base);
        }

        //av_dict_set_int(&output_stream->metadata, "DURATION", output_stream->duration, AV_DICT_IGNORE_SUFFIX);

        // Save the encoder context for easier access later.
        m_out.m_audio.m_codec_ctx               = output_codec_ctx;
        // Save the stream index
        m_out.m_audio.m_stream_idx              = output_stream->index;
        // Save output audio stream for faster reference
        m_out.m_audio.m_stream                  = output_stream;
        break;
    }
    case AVMEDIA_TYPE_VIDEO:
    {
        BITRATE orig_bit_rate;

        if (m_hwaccel_enable_enc_buffering && m_hwaccel_enc_device_ctx != nullptr)
        {
            Logging::debug(destname(), "Hardware encoder init: Creating new hardware frame context for %1 encoder.", get_hwaccel_API_text(params.m_hwaccel_enc_API).c_str());

            m_enc_hw_pix_fmt = get_hw_pix_fmt(output_codec, params.m_hwaccel_enc_device_type, false);

            ret = hwframe_ctx_set(output_codec_ctx, m_in.m_video.m_codec_ctx, m_hwaccel_enc_device_ctx);
            if (ret < 0)
            {
                return ret;
            }
        }

        output_codec_ctx->codec_id = codec_id;

        // Set the basic encoder parameters
        orig_bit_rate = (CODECPAR(m_in.m_video.m_stream)->bit_rate != 0) ? CODECPAR(m_in.m_video.m_stream)->bit_rate : m_in.m_format_ctx->bit_rate;
        if (get_output_bit_rate(orig_bit_rate, params.m_videobitrate, &output_codec_ctx->bit_rate))
        {
            // Limit sample rate
            Logging::trace(destname(), "Limiting video bit rate from %1 to %2.",
                           format_bitrate(orig_bit_rate).c_str(),
                           format_bitrate(output_codec_ctx->bit_rate).c_str());
        }

        // output_codec_ctx->rc_min_rate = output_codec_ctx->bit_rate * 75 / 100;
        // output_codec_ctx->rc_max_rate = output_codec_ctx->bit_rate * 125 / 100;

        // output_codec_ctx->qmin = 1;
        // output_codec_ctx->qmax = 31;

        int width = 0;
        int height = 0;
        if (get_video_size(&width, &height))
        {
            Logging::trace(destname(), "Changing video size from %1/%2 to %3/%4.", output_codec_ctx->width, output_codec_ctx->height, width, height);
            output_codec_ctx->width             = width;
            output_codec_ctx->height            = height;
        }
        else
        {
            output_codec_ctx->width             = CODECPAR(m_in.m_video.m_stream)->width;
            output_codec_ctx->height            = CODECPAR(m_in.m_video.m_stream)->height;
        }

#if LAVF_DEP_AVSTREAM_CODEC
        video_stream_setup(output_codec_ctx, output_stream, m_in.m_video.m_codec_ctx, m_in.m_video.m_stream->avg_frame_rate, m_enc_hw_pix_fmt);
#else
        video_stream_setup(output_codec_ctx, output_stream, m_in.m_video.m_codec_ctx, m_in.m_video.m_stream->codec->framerate, m_enc_hw_pix_fmt);
#endif

        AVRational sample_aspect_ratio                      = CODECPAR(m_in.m_video.m_stream)->sample_aspect_ratio;

        if (output_codec_ctx->codec_id != AV_CODEC_ID_VP9)
        {
            output_codec_ctx->sample_aspect_ratio           = sample_aspect_ratio;
            CODECPAR(output_stream)->sample_aspect_ratio    = sample_aspect_ratio;
        }

        else
        {
            // WebM does not respect the aspect ratio and always uses 1:1 so we need to rescale "manually".
            /**
             * @todo FFmpeg actually *can* transcode while presevering the SAR.
             * FFmpegfs rescales to fix that problem.
             * Need to find out what I am doing wrong here...
             */

            output_codec_ctx->sample_aspect_ratio           = { 1, 1 };
            CODECPAR(output_stream)->sample_aspect_ratio    = { 1, 1 };

            // Make sure we do not zero width
            if (sample_aspect_ratio.num && sample_aspect_ratio.den)
            {
                output_codec_ctx->width                       = output_codec_ctx->width * sample_aspect_ratio.num / sample_aspect_ratio.den;
            }
            //output_codec_ctx->height                        *= sample_aspect_ratio.den;
        }

        // Set up optimisations
        switch (output_codec_ctx->codec_id)
        {
        case AV_CODEC_ID_H264:
        {
            ret = prepare_codec(output_codec_ctx->priv_data, m_out.m_filetype);
            if (ret < 0)
            {
                Logging::error(destname(), "Could not set profile for %1 output codec %2 (error '%3').", get_media_type_string(output_codec->type), get_codec_name(codec_id, false), ffmpeg_geterror(ret).c_str());
                return ret;
            }

            // Set constant rate factor to avoid getting huge result files
            // The default is 23, but values between 30..40 create properly sized results. Possible values are 0 (lossless) to 51 (very small but ugly results).
            //ret = av_opt_set(output_codec_ctx->priv_data, "crf", "36", AV_OPT_SEARCH_CHILDREN);
            //if (ret < 0)
            //{
            //    Logging::error(destname(), "Could not set 'crf' for %1 output codec %2 (error '%3').", get_media_type_string(output_codec->type), get_codec_name(codec_id, false), ffmpeg_geterror(ret).c_str());
            // 	return ret;
            //}

            if (m_hwaccel_enable_enc_buffering)
            {
                // From libavcodec/vaapi_encode.c:
                //
                // Rate control mode selection:
                // * If the user has set a mode explicitly with the rc_mode option,
                //   use it and fail if it is not available.
                // * If an explicit QP option has been set, use CQP.
                // * If the codec is CQ-only, use CQP.
                // * If the QSCALE avcodec option is set, use CQP.
                // * If bitrate and quality are both set, try QVBR.
                // * If quality is set, try ICQ, then CQP.
                // * If bitrate and maxrate are set and have the same value, try CBR.
                // * If a bitrate is set, try AVBR, then VBR, then CBR.
                // * If no bitrate is set, try ICQ, then CQP.

                //ret = av_opt_set(output_codec_ctx->priv_data, "rc_mode", "CQP", AV_OPT_SEARCH_CHILDREN);
                //if (ret < 0)
                //{
                //    Logging::error(destname(), "Could not set 'rc_mode=CQP' for %1 output codec %2 (error '%3').", get_media_type_string(output_codec->type), get_codec_name(codec_id, false), ffmpeg_geterror(ret).c_str());
                //    return ret;
                //}
                //ret = av_opt_set(output_codec_ctx->priv_data, "qp", "23", AV_OPT_SEARCH_CHILDREN);
                //if (ret < 0)
                //{
                //    Logging::error(destname(), "Could not set 'qp' for %1 output codec %2 (error '%3').", get_media_type_string(output_codec->type), get_codec_name(codec_id, false), ffmpeg_geterror(ret).c_str());
                //    return ret;
                //}
                output_codec_ctx->global_quality = 34;
            }

            // Avoid mismatches for H264 and profile
            uint8_t   *out_val;
            ret = av_opt_get(output_codec_ctx->priv_data, "profile", 0, &out_val);
            if (!ret)
            {
                if (!strcasecmp(reinterpret_cast<const char *>(out_val), "high"))
                {
                    switch (output_codec_ctx->pix_fmt)
                    {
                    case AV_PIX_FMT_YUYV422:
                    case AV_PIX_FMT_YUV422P:
                    case AV_PIX_FMT_YUVJ422P:
                    case AV_PIX_FMT_UYVY422:
                    case AV_PIX_FMT_YUV422P16LE:
                    case AV_PIX_FMT_YUV422P16BE:
                    case AV_PIX_FMT_YUV422P10BE:
                    case AV_PIX_FMT_YUV422P10LE:
                    case AV_PIX_FMT_YUV422P9BE:
                    case AV_PIX_FMT_YUV422P9LE:
                    case AV_PIX_FMT_YUVA422P9BE:
                    case AV_PIX_FMT_YUVA422P9LE:
                    case AV_PIX_FMT_YUVA422P10BE:
                    case AV_PIX_FMT_YUVA422P10LE:
                    case AV_PIX_FMT_YUVA422P16BE:
                    case AV_PIX_FMT_YUVA422P16LE:
                    case AV_PIX_FMT_NV16:
                    case AV_PIX_FMT_NV20LE:
                    case AV_PIX_FMT_NV20BE:
                    case AV_PIX_FMT_YVYU422:
                    case AV_PIX_FMT_YUVA422P:
                    case AV_PIX_FMT_YUV422P12BE:
                    case AV_PIX_FMT_YUV422P12LE:
                    case AV_PIX_FMT_YUV422P14BE:
                    case AV_PIX_FMT_YUV422P14LE:
                    {
                        ret = av_opt_set(output_codec_ctx->priv_data, "profile", "high422", 0);
                        if (ret < 0)
                        {
                            Logging::error(destname(), "Could not set profile=high422 for %1 output codec %2 (error '%3').", get_media_type_string(output_codec->type), get_codec_name(codec_id, false), ffmpeg_geterror(ret).c_str());
                            return ret;
                        }
                        break;
                    }
                    case AV_PIX_FMT_YUV444P:
                    case AV_PIX_FMT_YUVJ444P:
                    case AV_PIX_FMT_YUV444P16LE:
                    case AV_PIX_FMT_YUV444P16BE:
                    case AV_PIX_FMT_RGB444LE:
                    case AV_PIX_FMT_RGB444BE:
                    case AV_PIX_FMT_BGR444LE:
                    case AV_PIX_FMT_BGR444BE:
                    case AV_PIX_FMT_YUV444P9BE:
                    case AV_PIX_FMT_YUV444P9LE:
                    case AV_PIX_FMT_YUV444P10BE:
                    case AV_PIX_FMT_YUV444P10LE:
                    case AV_PIX_FMT_GBRP:
                    case AV_PIX_FMT_GBRP9BE:
                    case AV_PIX_FMT_GBRP9LE:
                    case AV_PIX_FMT_GBRP10BE:
                    case AV_PIX_FMT_GBRP10LE:
                    case AV_PIX_FMT_GBRP16BE:
                    case AV_PIX_FMT_GBRP16LE:
                    case AV_PIX_FMT_YUVA444P9BE:
                    case AV_PIX_FMT_YUVA444P9LE:
                    case AV_PIX_FMT_YUVA444P10BE:
                    case AV_PIX_FMT_YUVA444P10LE:
                    case AV_PIX_FMT_YUVA444P16BE:
                    case AV_PIX_FMT_YUVA444P16LE:
                    case AV_PIX_FMT_XYZ12LE:
                    case AV_PIX_FMT_XYZ12BE:
                    case AV_PIX_FMT_YUVA444P:
                    case AV_PIX_FMT_GBRAP:
                    case AV_PIX_FMT_GBRAP16BE:
                    case AV_PIX_FMT_GBRAP16LE:
                    case AV_PIX_FMT_YUV444P12BE:
                    case AV_PIX_FMT_YUV444P12LE:
                    case AV_PIX_FMT_YUV444P14BE:
                    case AV_PIX_FMT_YUV444P14LE:
                    case AV_PIX_FMT_GBRP12BE:
                    case AV_PIX_FMT_GBRP12LE:
                    case AV_PIX_FMT_GBRP14BE:
                    case AV_PIX_FMT_GBRP14LE:
                    case AV_PIX_FMT_AYUV64LE:
                    case AV_PIX_FMT_AYUV64BE:
                    {
                        ret = av_opt_set(output_codec_ctx->priv_data, "profile", "high444", 0);
                        if (ret < 0)
                        {
                            Logging::error(destname(), "Could not set profile=high444 for %1 output codec %2 (error '%3').", get_media_type_string(output_codec->type), get_codec_name(codec_id, false), ffmpeg_geterror(ret).c_str());
                            return ret;
                        }
                        break;
                    }
                    default:
                    {
                        break;
                    }
                    }
                }
                av_free(out_val);
            }
            break;
        }
        case AV_CODEC_ID_VP9:
        {
            ret = prepare_codec(output_codec_ctx->priv_data, FILETYPE_WEBM);
            if (ret < 0)
            {
                Logging::error(destname(), "Could not set profile for %1 output codec %2 (error '%3').", get_media_type_string(output_codec->type), get_codec_name(codec_id, false), ffmpeg_geterror(ret).c_str());
                return ret;
            }
            break;
        }
        case AV_CODEC_ID_PRORES:
        {
            ret = prepare_codec(output_codec_ctx->priv_data, FILETYPE_PRORES);
            if (ret < 0)
            {
                Logging::error(destname(), "Could not set profile for %1 output codec %2 (error '%3').", get_media_type_string(output_codec->type), get_codec_name(codec_id, false), ffmpeg_geterror(ret).c_str());
                return ret;
            }

            // 0 = ‘proxy’,
            // 1 = ‘lt’,
            // 2 = ‘standard’,
            // 3 = ‘hq’
            output_codec_ctx->profile = params.m_level;
            break;
        }
        case AV_CODEC_ID_ALAC:
        {
            ret = prepare_codec(output_codec_ctx->priv_data, FILETYPE_ALAC);
            if (ret < 0)
            {
                Logging::error(destname(), "Could not set profile for %1 output codec %2 (error '%3').", get_media_type_string(output_codec->type), get_codec_name(codec_id, false), ffmpeg_geterror(ret).c_str());
                return ret;
            }
            break;
        }
        default:
        {
            break;
        }
        }

        // Initialise pixel format conversion and rescaling if necessary
        get_pix_formats(&m_in.m_pix_fmt, &m_out.m_pix_fmt, output_codec_ctx);

        ret = init_rescaler(m_in.m_pix_fmt, CODECPAR(m_in.m_video.m_stream)->width, CODECPAR(m_in.m_video.m_stream)->height, m_out.m_pix_fmt, output_codec_ctx->width, output_codec_ctx->height);
        if (ret < 0)
        {
            return ret;
        }

#ifdef _DEBUG
        print_stream_info(output_stream);
#endif // _DEBUG

        // Set duration as hint for muxer
        if (m_in.m_video.m_stream->duration != AV_NOPTS_VALUE)
        {
            output_stream->duration             = av_rescale_q(m_in.m_video.m_stream->duration, m_in.m_video.m_stream->time_base, output_stream->time_base);
        }
        else if (m_in.m_format_ctx->duration != AV_NOPTS_VALUE)
        {
            output_stream->duration             = av_rescale_q(m_in.m_format_ctx->duration, av_get_time_base_q(), output_stream->time_base);
        }

        //av_dict_set_int(&output_stream->metadata, "DURATION", output_stream->duration, AV_DICT_IGNORE_SUFFIX);

        // Save the encoder context for easier access later.
        m_out.m_video.m_codec_ctx               = output_codec_ctx;
        // Save the stream index
        m_out.m_video.m_stream_idx              = output_stream->index;
        // Save output video stream for faster reference
        m_out.m_video.m_stream                  = output_stream;
        break;
    }
    default:
        break;
    }

    // Although docs state this is "Demuxing only", this is actually used by encoders like Matroska/WebM, so we need to set this here.
    m_out.m_format_ctx->duration = m_in.m_format_ctx->duration;
    if (m_virtualfile->m_flags & VIRTUALFLAG_CUESHEET)
    {
        av_dict_set_int(&m_out.m_format_ctx->metadata, "DURATION", m_virtualfile->m_cuesheet.m_duration, AV_DICT_IGNORE_SUFFIX);
    }
    else
    {
        av_dict_set_int(&m_out.m_format_ctx->metadata, "DURATION", m_out.m_format_ctx->duration, AV_DICT_IGNORE_SUFFIX);
    }

    // Some formats want stream headers to be separate.
    if (m_out.m_format_ctx->oformat->flags & AVFMT_GLOBALHEADER)
    {
        output_codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    if (!av_dict_get(opt, "threads", nullptr, 0))
    {
        Logging::trace(destname(), "Setting threads to auto for codec %1.", get_codec_name(output_codec_ctx->codec_id, false));
        dict_set_with_check(&opt, "threads", "auto", 0, destname());
    }

    // Open the encoder for the stream to use it later.
    ret = avcodec_open2(output_codec_ctx, output_codec, &opt);
    if (ret < 0)
    {
        Logging::error(destname(), "Could not open %1 output codec %2 for stream #%3 (error '%4').", get_media_type_string(output_codec->type), get_codec_name(codec_id, false), output_stream->index, ffmpeg_geterror(ret).c_str());
        return ret;
    }

    Logging::debug(destname(), "Opened %1 output codec %2 for stream #%3.", get_media_type_string(output_codec->type), get_codec_name(codec_id, true), output_stream->index);

#if FFMPEG_VERSION3 // Check for FFmpeg 3
    ret = avcodec_parameters_from_context(output_stream->codecpar, output_codec_ctx);
    if (ret < 0)
    {
        Logging::error(destname(), "Could not initialise stream parameters (error '%1').", ffmpeg_geterror(ret).c_str());
        return ret;
    }
#endif

    return 0;
}

int FFmpeg_Transcoder::add_stream_copy(AVCodecID codec_id, AVMediaType codec_type)
{
    AVStream *      output_stream       = nullptr;
    int ret;

    output_stream = avformat_new_stream(m_out.m_format_ctx, nullptr);
    if (output_stream == nullptr)
    {
        Logging::error(destname(), "Could not allocate stream for encoder '%1'.",  avcodec_get_name(codec_id));
        return AVERROR(ENOMEM);
    }
    output_stream->id = static_cast<int>(m_out.m_format_ctx->nb_streams - 1);

    switch (codec_type)
    {
    case AVMEDIA_TYPE_AUDIO:
    {
#if FFMPEG_VERSION3 // Check for FFmpeg 3

        ret = avcodec_parameters_copy(output_stream->codecpar, m_in.m_audio.m_stream->codecpar);
        if (ret < 0)
        {
            Logging::error(destname(), "Could not alloc an encoding context (error '%2').", ffmpeg_geterror(ret).c_str());
            return ret;
        }
#else
        AVCodecContext *output_codec_ctx = output_stream->codec;

        ret = avcodec_copy_context(output_codec_ctx /*output_stream->codec*/, m_in.m_audio.m_stream->codec);
        if (ret != 0)
        {
            return ret;
        }
#endif

        // Set the sample rate for the container.
        output_stream->time_base                = m_in.m_audio.m_stream->time_base;

        // Set duration as hint for muxer
        output_stream->duration                 = av_rescale_q(m_in.m_audio.m_stream->duration, m_in.m_audio.m_stream->time_base, output_stream->time_base);

        // Save the encoder context for easier access later.
        m_out.m_audio.m_codec_ctx               = nullptr;
        // Save the stream index
        m_out.m_audio.m_stream_idx              = output_stream->index;
        // Save output audio stream for faster reference
        m_out.m_audio.m_stream                  = output_stream;
        break;
    }
    case AVMEDIA_TYPE_VIDEO:
    {
#if FFMPEG_VERSION3 // Check for FFmpeg 3

        ret = avcodec_parameters_copy(output_stream->codecpar, m_in.m_video.m_stream->codecpar);
        if (ret < 0)
        {
            Logging::error(destname(), "Could not alloc an encoding context (error '%2').", ffmpeg_geterror(ret).c_str());
            return ret;
        }
#else
        AVCodecContext *output_codec_ctx = output_stream->codec;

        ret = avcodec_copy_context(output_codec_ctx /*output_stream->codec*/, m_in.m_video.m_stream->codec);
        if (ret != 0)
        {
            return ret;
        }
#endif
        output_stream->time_base                = m_in.m_video.m_stream->time_base;

#ifdef _DEBUG
        print_stream_info(output_stream);
#endif // _DEBUG

        // Set duration as hint for muxer
        output_stream->duration                 = av_rescale_q(m_in.m_video.m_stream->duration, m_in.m_video.m_stream->time_base, output_stream->time_base);

        // Save the encoder context for easier access later.
        m_out.m_video.m_codec_ctx               = nullptr;
        // Save the stream index
        m_out.m_video.m_stream_idx              = output_stream->index;
        // Save output video stream for faster reference
        m_out.m_video.m_stream                  = output_stream;

        break;
    }
    default:
        break;
    }

    CODECPAR(output_stream)->codec_tag = 0;

    return 0;
}

int FFmpeg_Transcoder::add_albumart_stream(const AVCodecContext * input_codec_ctx)
{
    AVCodecContext * output_codec_ctx   = nullptr;
    AVStream * output_stream            = nullptr;
    const AVCodec * input_codec         = input_codec_ctx->codec;
    const AVCodec * output_codec        = nullptr;
    AVDictionary *  opt                 = nullptr;
    int ret;

    // find the encoder
    output_codec = avcodec_find_encoder(input_codec->id);
    if (output_codec == nullptr)
    {
        Logging::error(destname(), "Could not find encoder '%1'.", avcodec_get_name(input_codec->id));
        return AVERROR(EINVAL);
    }

    // Must be a video codec
    if (output_codec->type != AVMEDIA_TYPE_VIDEO)
    {
        Logging::error(destname(), "INTERNAL TROUBLE! Encoder '%1' is not a video codec.", avcodec_get_name(input_codec->id));
        return AVERROR(EINVAL);
    }

    output_stream = avformat_new_stream(m_out.m_format_ctx, output_codec);
    if (output_stream == nullptr)
    {
        Logging::error(destname(), "Could not allocate stream for encoder '%1'.", avcodec_get_name(input_codec->id));
        return AVERROR(ENOMEM);
    }
    output_stream->id = static_cast<int>(m_out.m_format_ctx->nb_streams - 1);

#if FFMPEG_VERSION3 // Check for FFmpeg 3
    output_codec_ctx = avcodec_alloc_context3(output_codec);
    if (output_codec_ctx == nullptr)
    {
        Logging::error(destname(), "Could not alloc an encoding context.");
        return AVERROR(ENOMEM);
    }
#else
    output_codec_ctx = output_stream->codec;
#endif

    // Ignore missing width/height when adding album arts
#if !IF_DECLARED_CONST
    m_out.m_format_ctx->oformat->flags |= AVFMT_NODIMENSIONS;
#endif // !IF_DECLARED_CONST
    // This is required for some reason (let encoder decide?)
    // If not set, write header will fail!
    //output_codec_ctx->codec_tag = 0; //av_codec_get_tag(of->codec_tag, codec->codec_id);

    //output_stream->codec->framerate = { 1, 0 };

    /** @todo Add support for album arts */
    // mp4 album arts do not work with ipod profile. Set mp4.
    //if (m_out.m_format_ctx->oformat->mime_type != nullptr && (!strcmp(m_out.m_format_ctx->oformat->mime_type, "application/mp4") || !strcmp(m_out.m_format_ctx->oformat->mime_type, "video/mp4")))
    //{
    //    m_out.m_format_ctx->oformat->name = "mp4";
    //    m_out.m_format_ctx->oformat->mime_type = "application/mp4";
    //}

    // copy disposition
    // output_stream->disposition = input_stream->disposition;
    output_stream->disposition = AV_DISPOSITION_ATTACHED_PIC;

    // copy estimated duration as a hint to the muxer
    if (output_stream->duration <= 0 && m_in.m_audio.m_stream->duration > 0)
    {
        output_stream->duration = av_rescale_q(m_in.m_audio.m_stream->duration, m_in.m_audio.m_stream->time_base, output_stream->time_base);
    }

    output_codec_ctx->time_base = { 1, 90000 };
    output_stream->time_base    = { 1, 90000 };

    output_codec_ctx->pix_fmt   = input_codec_ctx->pix_fmt;
    output_codec_ctx->width     = input_codec_ctx->width;
    output_codec_ctx->height    = input_codec_ctx->height;

    // Some formats want stream headers to be separate.
    if (m_out.m_format_ctx->oformat->flags & AVFMT_GLOBALHEADER)
    {
        output_codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    // Open the encoder for the stream to use it later.
    ret = avcodec_open2(output_codec_ctx, output_codec, &opt);
    if (ret < 0)
    {
        Logging::error(destname(), "Could not open %1 output codec %2 for stream #%3 (error '%4').", get_media_type_string(output_codec->type), get_codec_name(input_codec->id, false), output_stream->index, ffmpeg_geterror(ret).c_str());
        return ret;
    }

    Logging::debug(destname(), "Opened album art output codec %1 for stream #%2 (dimensions %3x%4).", get_codec_name(input_codec->id, true), output_stream->index, output_codec_ctx->width, output_codec_ctx->height);

#if FFMPEG_VERSION3 // Check for FFmpeg 3
    ret = avcodec_parameters_from_context(output_stream->codecpar, output_codec_ctx);
    if (ret < 0)
    {
        Logging::error(destname(), "Could not initialise stream parameters stream #%1 (error '%2').", output_stream->index, ffmpeg_geterror(ret).c_str());
        return ret;
    }
#endif

    STREAMREF stream;

    stream.m_codec_ctx     = output_codec_ctx;
    stream.m_stream        = output_stream;
    stream.m_stream_idx    = output_stream->index;

    m_out.m_album_art.push_back(stream);

    return 0;
}

int FFmpeg_Transcoder::add_albumart_frame(AVStream *output_stream, AVPacket *pkt_in)
{
    AVPacket *tmp_pkt;
    int ret = 0;

#if LAVF_DEP_AV_COPY_PACKET
    tmp_pkt = av_packet_clone(pkt_in);
    if (tmp_pkt == nullptr)
    {
        ret = AVERROR(ENOMEM);
        Logging::error(destname(), "Could not write album art packet (error '%1').", ffmpeg_geterror(ret).c_str());
        return ret;
    }
#else
    AVPacket pkt;

    tmp_pkt = &pkt;

    ret = av_copy_packet(tmp_pkt, pkt_in);
    if (ret < 0)
    {
        Logging::error(destname(), "Could not write album art packet (error '%1').", ffmpeg_geterror(ret).c_str());
        return ret;
    }
#endif

    Logging::trace(destname(), "Adding album art stream #%u.", output_stream->index);

    tmp_pkt->stream_index = output_stream->index;
    tmp_pkt->flags |= AV_PKT_FLAG_KEY;
    tmp_pkt->pos = 0;
    tmp_pkt->dts = 0;

    ret = store_packet(tmp_pkt, AVMEDIA_TYPE_ATTACHMENT);

#if LAVF_DEP_AV_COPY_PACKET
    av_packet_unref(tmp_pkt);
#else
    av_free_packet(tmp_pkt);
#endif

    return ret;
}

int FFmpeg_Transcoder::open_output_filestreams(Buffer *buffer)
{
    int             ret = 0;

    m_out.m_filetype = m_current_format->filetype();

    Logging::debug(destname(), "Opening format type '%1'.", m_current_format->desttype().c_str());

    // Reset active streams
    m_active_stream_msk     = 0;
    m_inhibit_stream_msk    = 0;

    // Check if we can copy audio or video.
    m_copy_audio = can_copy_stream(m_in.m_audio.m_stream);
    m_copy_video = can_copy_stream(m_in.m_video.m_stream);

    // Create a new format context for the output container format.
    if (m_current_format->format_name() != "m4a")
    {
        avformat_alloc_output_context2(&m_out.m_format_ctx, nullptr, m_current_format->format_name().c_str(), nullptr);
    }
    else
    {
        avformat_alloc_output_context2(&m_out.m_format_ctx, nullptr, nullptr, ".m4a");
    }

    if (m_out.m_format_ctx == nullptr)
    {
        Logging::error(destname(), "Could not allocate output format context.");
        return AVERROR(ENOMEM);
    }

    if (!m_is_video)
    {
        m_in.m_video.m_stream_idx = INVALID_STREAM;
    }

    //video_codec_id = m_out.m_format_ctx->oformat->video_codec;

    if (m_in.m_video.m_stream_idx != INVALID_STREAM && m_current_format->video_codec_id() != AV_CODEC_ID_NONE)
    {
        m_active_stream_msk     |= FFMPEGFS_VIDEO;

        if (!m_copy_video)
        {
            ret = add_stream(m_current_format->video_codec_id());
            if (ret < 0)
            {
                return ret;
            }

            if (params.m_deinterlace)
            {
                // Init deinterlace filters
                ret = init_deinterlace_filters(m_in.m_video.m_codec_ctx, m_in.m_pix_fmt, m_in.m_video.m_stream->avg_frame_rate, m_in.m_video.m_stream->time_base);
                if (ret < 0)
                {
                    return ret;
                }
            }
        }
        else
        {
            Logging::info(destname(), "Copying video stream.");

            ret = add_stream_copy(m_current_format->video_codec_id(), AVMEDIA_TYPE_VIDEO);
            if (ret < 0)
            {
                return ret;
            }
        }
    }

    if (m_in.m_audio.m_stream_idx != INVALID_STREAM && m_current_format->audio_codec_id() != AV_CODEC_ID_NONE)
    {
        m_active_stream_msk     |= FFMPEGFS_AUDIO;

        if (!m_copy_audio)
        {
            ret = add_stream(m_current_format->audio_codec_id());
            if (ret < 0)
            {
                return ret;
            }
        }
        else
        {
            Logging::info(destname(), "Copying audio stream.");

            ret = add_stream_copy(m_current_format->audio_codec_id(), AVMEDIA_TYPE_AUDIO);
            if (ret < 0)
            {
                return ret;
            }
        }
    }

    if (!params.m_noalbumarts)
    {
        for (size_t n = 0; n < m_in.m_album_art.size(); n++)
        {
            //ret = add_albumart_stream(codec_id, m_in.m_aAlbumArt.at(n).m_codec_ctx->pix_fmt);
            ret = add_albumart_stream(m_in.m_album_art.at(n).m_codec_ctx);
            if (ret < 0)
            {
                return ret;
            }
        }
    }

    const int buf_size = 5*1024*1024;
    unsigned char *iobuffer = static_cast<unsigned char *>(av_malloc(buf_size + FF_INPUT_BUFFER_PADDING_SIZE));
    if (iobuffer == nullptr)
    {
        Logging::error(destname(), "Out of memory opening output file: Unable to allocate I/O buffer.");
        return AVERROR(ENOMEM);
    }

    // open the output file
    m_out.m_format_ctx->pb = avio_alloc_context(
                iobuffer,
                buf_size,
                1,
                static_cast<void *>(buffer),
                nullptr,        // read not required
                output_write,   // write
                (m_current_format->audio_codec_id() != AV_CODEC_ID_OPUS) ? seek : nullptr);          // seek

    if (m_out.m_format_ctx->pb == nullptr)
    {
        Logging::error(destname(), "Out of memory opening output file: Unable to allocate format context.");
        return AVERROR(ENOMEM);
    }

    return 0;
}

int FFmpeg_Transcoder::init_resampler()
{
    // Fail save: if channel layout not known assume mono or stereo
    if (!m_in.m_audio.m_codec_ctx->channel_layout)
    {
        m_in.m_audio.m_codec_ctx->channel_layout = static_cast<uint64_t>(av_get_default_channel_layout(m_in.m_audio.m_codec_ctx->channels));
    }
    if (!m_in.m_audio.m_codec_ctx->channel_layout)
    {
        m_in.m_audio.m_codec_ctx->channel_layout = AV_CH_LAYOUT_STEREO;
    }
    // Only initialise the resampler if it is necessary, i.e.,
    // if and only if the sample formats differ.
    if (m_in.m_audio.m_codec_ctx->sample_fmt == m_out.m_audio.m_codec_ctx->sample_fmt &&
            m_in.m_audio.m_codec_ctx->sample_rate == m_out.m_audio.m_codec_ctx->sample_rate &&
            m_in.m_audio.m_codec_ctx->channel_layout == m_out.m_audio.m_codec_ctx->channel_layout)

    {
        // Formats are same
        close_resample();
        return 0;
    }

    if (m_audio_resample_ctx == nullptr ||
            m_cur_sample_fmt != m_in.m_audio.m_codec_ctx->sample_fmt ||
            m_cur_sample_rate != m_in.m_audio.m_codec_ctx->sample_rate ||
            m_cur_channel_layout != m_in.m_audio.m_codec_ctx->channel_layout)
    {
        int ret;

        Logging::debug(destname(), "Creating audio resampler: %1 -> %2 / %3 -> %4 / %5 -> %6.",
                       get_sample_fmt_name(m_in.m_audio.m_codec_ctx->sample_fmt).c_str(),
                       get_sample_fmt_name(m_out.m_audio.m_codec_ctx->sample_fmt).c_str(),
                       format_samplerate(m_in.m_audio.m_codec_ctx->sample_rate).c_str(),
                       format_samplerate(m_out.m_audio.m_codec_ctx->sample_rate).c_str(),
                       get_channel_layout_name(m_in.m_audio.m_codec_ctx->channels, m_in.m_audio.m_codec_ctx->channel_layout).c_str(),
                       get_channel_layout_name(m_out.m_audio.m_codec_ctx->channels, m_out.m_audio.m_codec_ctx->channel_layout).c_str());

        close_resample();

        m_cur_sample_fmt        = m_in.m_audio.m_codec_ctx->sample_fmt;
        m_cur_sample_rate       = m_in.m_audio.m_codec_ctx->sample_rate;
        m_cur_channel_layout    = m_in.m_audio.m_codec_ctx->channel_layout;

        // Create a resampler context for the conversion.
        // Set the conversion parameters.
#if LAVR_DEPRECATE
        m_audio_resample_ctx = swr_alloc_set_opts(nullptr,
                                                  static_cast<int64_t>(m_out.m_audio.m_codec_ctx->channel_layout),
                                                  m_out.m_audio.m_codec_ctx->sample_fmt,
                                                  m_out.m_audio.m_codec_ctx->sample_rate,
                                                  static_cast<int64_t>(m_in.m_audio.m_codec_ctx->channel_layout),
                                                  m_in.m_audio.m_codec_ctx->sample_fmt,
                                                  m_in.m_audio.m_codec_ctx->sample_rate,
                                                  0, nullptr);
        if (m_audio_resample_ctx == nullptr)
        {
            Logging::error(destname(), "Could not allocate resample context.");
            return AVERROR(ENOMEM);
        }

        // Open the resampler with the specified parameters.
        ret = swr_init(m_audio_resample_ctx);
        if (ret < 0)
        {
            Logging::error(destname(), "Could not open resampler context (error '%1').", ffmpeg_geterror(ret).c_str());
            swr_free(&m_audio_resample_ctx);
            m_audio_resample_ctx = nullptr;
            return ret;
        }
#else
        // Create a resampler context for the conversion.
        m_audio_resample_ctx = avresample_alloc_context();
        if (m_audio_resample_ctx == nullptr)
        {
            Logging::error(destname(), "Could not allocate resample context.");
            return AVERROR(ENOMEM);
        }

        // Set the conversion parameters.
        // Default channel layouts based on the number of channels
        // are assumed for simplicity (they are sometimes not detected
        // properly by the demuxer and/or decoder).

        av_opt_set_int(m_audio_resample_ctx, "in_channel_layout", av_get_default_channel_layout(m_in.m_audio.m_codec_ctx->channels), 0);
        av_opt_set_int(m_audio_resample_ctx, "out_channel_layout", av_get_default_channel_layout(m_out.m_audio.m_codec_ctx->channels), 0);
        av_opt_set_int(m_audio_resample_ctx, "in_sample_rate", m_in.m_audio.m_codec_ctx->sample_rate, 0);
        av_opt_set_int(m_audio_resample_ctx, "out_sample_rate", m_out.m_audio.m_codec_ctx->sample_rate, 0);
        av_opt_set_int(m_audio_resample_ctx, "in_sample_fmt", m_in.m_audio.m_codec_ctx->sample_fmt, 0);
        av_opt_set_int(m_audio_resample_ctx, "out_sample_fmt", m_out.m_audio.m_codec_ctx->sample_fmt, 0);

        // Open the resampler with the specified parameters.
        ret = avresample_open(m_audio_resample_ctx);
        if (ret < 0)
        {
            Logging::error(destname(), "Could not open resampler context (error '%1').", ffmpeg_geterror(ret).c_str());
            avresample_free(&m_audio_resample_ctx);
            m_audio_resample_ctx = nullptr;
            return ret;
        }
#endif
    }
    return 0;
}

int FFmpeg_Transcoder::init_fifo()
{
    // Create the FIFO buffer based on the specified output sample format.
    m_audio_fifo = av_audio_fifo_alloc(m_out.m_audio.m_codec_ctx->sample_fmt, m_out.m_audio.m_codec_ctx->channels, 1);
    if (m_audio_fifo == nullptr)
    {
        Logging::error(destname(), "Could not allocate FIFO.");
        return AVERROR(ENOMEM);
    }
    return 0;
}

int FFmpeg_Transcoder::update_format(AVDictionary** dict, LPCPROFILE_OPTION option) const
{
    int ret = 0;

    if (option == nullptr)
    {
        return 0;
    }

    for (LPCPROFILE_OPTION p = option; p->m_key != nullptr; p++)
    {
        if ((p->m_options & OPT_AUDIO) && m_out.m_video.m_stream_idx != INVALID_STREAM)
        {
            // Option for audio only, but file contains video stream
            continue;
        }

        if ((p->m_options & OPT_VIDEO) && m_out.m_video.m_stream_idx == INVALID_STREAM)
        {
            // Option for video, but file contains no video stream
            continue;
        }

        Logging::trace(destname(), "Profile format option -%1%2%3.",  p->m_key, *p->m_value ? " " : "", p->m_value);

        ret = dict_set_with_check(dict, p->m_key, p->m_value, p->m_flags, destname());
        if (ret < 0)
        {
            break;
        }
    }
    return ret;
}

int FFmpeg_Transcoder::prepare_format(AVDictionary** dict, FILETYPE filetype) const
{
    int ret = 0;

    for (int n = 0; m_profile[n].m_profile != PROFILE_INVALID; n++)
    {
        if (m_profile[n].m_filetype == filetype && m_profile[n].m_profile == params.m_profile)
        {
            ret = update_format(dict, m_profile[n].m_option_format);
            break;
        }
    }

    if (filetype == FILETYPE_MP4 || filetype == FILETYPE_PRORES || filetype == FILETYPE_TS || filetype == FILETYPE_HLS)
    {
        // All
        dict_set_with_check(dict, "flags:a", "+global_header", 0, destname());
        dict_set_with_check(dict, "flags:v", "+global_header", 0, destname());
    }

    return ret;
}

int FFmpeg_Transcoder::write_output_file_header()
{
    AVDictionary* dict = nullptr;
    int ret;

    ret = prepare_format(&dict, m_out.m_filetype);
    if (ret < 0)
    {
        return ret;
    }

    ret = avformat_write_header(m_out.m_format_ctx, &dict);
    if (ret < 0)
    {
        Logging::error(destname(), "Could not write output file header (error '%1').", ffmpeg_geterror(ret).c_str());
        return ret;
    }

    if (m_out.m_filetype == FILETYPE_WAV)
    {
        // Insert fake WAV header (fill in size fields with estimated values instead of setting to -1)
        AVIOContext * output_io_context = static_cast<AVIOContext *>(m_out.m_format_ctx->pb);
        Buffer *buffer = static_cast<Buffer *>(output_io_context->opaque);
        size_t pos = buffer->tell();
        WAV_HEADER wav_header;
        WAV_LIST_HEADER list_header;
        WAV_DATA_HEADER data_header;

        buffer->copy(reinterpret_cast<uint8_t*>(&wav_header), 0, sizeof(WAV_HEADER));
        buffer->copy(reinterpret_cast<uint8_t*>(&list_header), sizeof(WAV_HEADER), sizeof(WAV_LIST_HEADER));
        buffer->copy(reinterpret_cast<uint8_t*>(&data_header), sizeof(WAV_HEADER) + sizeof(WAV_LIST_HEADER) + list_header.m_data_bytes - 4, sizeof(WAV_DATA_HEADER));

        wav_header.m_wav_size = static_cast<unsigned int>(predicted_filesize() - 8);
        data_header.m_data_bytes = static_cast<unsigned int>(predicted_filesize() - (sizeof(WAV_HEADER) + sizeof(WAV_LIST_HEADER) + sizeof(WAV_DATA_HEADER) + list_header.m_data_bytes - 4));

        buffer->seek(0, SEEK_SET);
        buffer->write(reinterpret_cast<uint8_t*>(&wav_header), sizeof(WAV_HEADER));
        buffer->seek(static_cast<long>(sizeof(WAV_HEADER) + sizeof(WAV_LIST_HEADER) + list_header.m_data_bytes - 4), SEEK_SET);
        buffer->write(reinterpret_cast<uint8_t*>(&data_header), sizeof(WAV_DATA_HEADER));
        buffer->seek(static_cast<long>(pos), SEEK_SET);
    }

    return 0;
}

AVFrame *FFmpeg_Transcoder::alloc_picture(AVPixelFormat pix_fmt, int width, int height)
{
    AVFrame *picture;
    int ret;

    ret = init_frame(&picture, filename());
    if (ret < 0)
    {
        return nullptr;
    }

    picture->format = pix_fmt;
    picture->width  = width;
    picture->height = height;

    // allocate the buffers for the frame data
    ret = av_frame_get_buffer(picture, 32);
    if (ret < 0)
    {
        Logging::error(destname(), "Could not allocate frame data (error '%1').", ffmpeg_geterror(ret).c_str());
        av_frame_free(&picture);
        return nullptr;
    }

    return picture;
}

#if LAVC_NEW_PACKET_INTERFACE
int FFmpeg_Transcoder::decode(AVCodecContext *avctx, AVFrame *frame, int *got_frame, const AVPacket *pkt) const
{
    int ret;

    *got_frame = 0;

    if (pkt != nullptr)
    {
        ret = avcodec_send_packet(avctx, pkt);
        // In particular, we don't expect AVERROR(EAGAIN), because we read all
        // decoded frames with avcodec_receive_frame() until done.
        if (ret < 0 && ret != AVERROR_EOF)
        {
            if (pkt->stream_index == m_in.m_audio.m_stream_idx && m_out.m_audio.m_stream_idx != INVALID_STREAM)
            {
                Logging::error(filename(), "Could not send audio packet at PTS=%1 to decoder (error '%2').", av_rescale_q(pkt->pts, m_in.m_audio.m_stream->time_base, av_get_time_base_q()), ffmpeg_geterror(ret).c_str());
            }
            else if (pkt->stream_index == m_in.m_video.m_stream_idx && m_out.m_video.m_stream_idx != INVALID_STREAM)
            {
                Logging::error(filename(), "Could not send video packet at PTS=%1 to decoder (error '%2').", av_rescale_q(pkt->pts, m_in.m_video.m_stream->time_base, av_get_time_base_q()), ffmpeg_geterror(ret).c_str());
            }
            else
            {
                // Should never come here, but what the heck...
                Logging::error(filename(), "Could not send packet at PTS=%1 to decoder (error '%2').", pkt->pts, ffmpeg_geterror(ret).c_str());
            }
            return ret;
        }
    }

    ret = avcodec_receive_frame(avctx, frame);
    if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
    {
        Logging::error(filename(), "Could not receive packet from decoder (error '%1').", ffmpeg_geterror(ret).c_str());
    }

    /**
      * @note Only after the first hardware decoded video packet arrived we have a
      * @note hardware frame context.
      * @note We should create the output stream now, open a codec etc. and call
      * @note hwframe_ctx_set.
      */

    *got_frame = (ret >= 0) ? 1 : 0;

    return ret;
}
#endif

int FFmpeg_Transcoder::decode_audio_frame(AVPacket *pkt, int *decoded)
{
    int data_present = 0;
    int ret = 0;

    *decoded = 0;

    // Decode the audio frame stored in the temporary packet.
    // The input audio stream decoder is used to do this.
    // If we are at the end of the file, pass an empty packet to the decoder
    // to flush it.

    // Since FFMpeg version >= 3.2 this is deprecated
#if  !LAVC_NEW_PACKET_INTERFACE
    // Temporary storage of the input samples of the frame read from the file.
    AVFrame *frame = nullptr;

    // Initialise temporary storage for one input frame.
    ret = init_frame(&frame, filename());
    if (ret < 0)
    {
        return ret;
    }

    ret = avcodec_decode_audio4(m_in.m_audio.m_codec_ctx, frame, &data_present, pkt);

    if (ret < 0 && ret != AVERROR(EINVAL))
    {
        Logging::error(filename(), "Could not decode audio frame (error '%1').", ffmpeg_geterror(ret).c_str());
        // unused frame
        av_frame_free(&frame);
        return ret;
    }

    *decoded = ret;
    ret = 0;

    {
#else
    bool again = false;

    data_present = 0;

    // read all the output frames (in general there may be any number of them)
    while (ret >= 0)
    {
        AVFrame *frame = nullptr;

        // Initialise temporary storage for one input frame.
        ret = init_frame(&frame, filename());
        if (ret < 0)
        {
            return ret;
        }

        ret = decode(m_in.m_audio.m_codec_ctx, frame, &data_present, again ? nullptr : pkt);
        if (!data_present)
        {
            // unused frame
            av_frame_free(&frame);
            break;
        }

        if (ret < 0)
        {
            // Anything else is an error, report it!
            Logging::error(filename(), "Could not decode audio frame (error '%1').", ffmpeg_geterror(ret).c_str());
            // unused frame
            av_frame_free(&frame);
            break;
        }

        again = true;

        *decoded += pkt->size;
#endif
        // If there is decoded data, convert and store it
        if (data_present && frame->nb_samples)
        {
            // Temporary storage for the converted input samples.
            uint8_t **converted_input_samples = nullptr;
            int nb_output_samples;
#if LAVR_DEPRECATE
            nb_output_samples = (m_audio_resample_ctx != nullptr) ? swr_get_out_samples(m_audio_resample_ctx, frame->nb_samples) : frame->nb_samples;
#else
            nb_output_samples = (m_audio_resample_ctx != nullptr) ? avresample_get_out_samples(m_audio_resample_ctx, frame->nb_samples) : frame->nb_samples;
#endif

            try
            {
                // Initialise the resampler to be able to convert audio sample formats.
                ret = init_resampler();
                if (ret)
                {
                    throw ret;
                }

                // Store audio frame
                // Initialise the temporary storage for the converted input samples.
                ret = init_converted_samples(&converted_input_samples, nb_output_samples);
                if (ret < 0)
                {
                    throw ret;
                }

                // Convert the input samples to the desired output sample format.
                // This requires a temporary storage provided by converted_input_samples.
                ret = convert_samples(frame->extended_data, frame->nb_samples, converted_input_samples, &nb_output_samples);
                if (ret < 0)
                {
                    throw ret;
                }

                // Add the converted input samples to the FIFO buffer for later processing.
                ret = add_samples_to_fifo(converted_input_samples, nb_output_samples);
                if (ret < 0)
                {
                    throw ret;
                }
                ret = 0;
            }
            catch (int _ret)
            {
                ret = _ret;
            }

            if (converted_input_samples != nullptr)
            {
                av_freep(&converted_input_samples[0]);
                av_free(converted_input_samples);
            }
        }
        av_frame_free(&frame);
    }
    return ret;
}

int FFmpeg_Transcoder::decode_video_frame(AVPacket *pkt, int *decoded)
{
    int data_present;
    int ret = 0;

    *decoded = 0;

    // NOTE1: some codecs are stream based (mpegvideo, mpegaudio)
    // and this is the only method to use them because you cannot
    // know the compressed data size before analysing it.

    // BUT some other codecs (msmpeg4, mpeg4) are inherently frame
    // based, so you must call them with all the data for one
    // frame exactly. You must also initialise 'width' and
    // 'height' before initialising them.

    // NOTE2: some codecs allow the raw parameters (frame size,
    // sample rate) to be changed at any frame. We handle this, so
    // you should also take care of it

    // Since FFMpeg version >= 3.2 this is deprecated
#if !LAVC_NEW_PACKET_INTERFACE
    // Temporary storage of the input samples of the frame read from the file.
    AVFrame *frame = nullptr;

    // Initialise temporary storage for one input frame.
    ret = init_frame(&frame, filename());
    if (ret < 0)
    {
        return ret;
    }

    ret = avcodec_decode_video2(m_in.m_video.m_codec_ctx, frame, &data_present, pkt);

    if (ret < 0 && ret != AVERROR(EINVAL))
    {
        Logging::error(filename(), "Could not decode video frame (error '%1').", ffmpeg_geterror(ret).c_str());
        // unused frame
        av_frame_free(&frame);
        return ret;
    }

    *decoded = ret;
    ret = 0;

    {
#else
    bool again = false;

    data_present = 0;

    // read all the output frames (in general there may be any number of them)
    while (ret >= 0)
    {
        AVFrame *frame = nullptr;

        // Initialise temporary storage for one input frame.
        ret = init_frame(&frame, filename());
        if (ret < 0)
        {
            return ret;
        }

        ret = decode(m_in.m_video.m_codec_ctx, frame, &data_present, again ? nullptr : pkt);
        if (!data_present)
        {
            // unused frame
            av_frame_free(&frame);
            break;
        }

        if (ret < 0)
        {
            // Anything else is an error, report it!
            Logging::error(filename(), "Could not decode video frame (error '%1').", ffmpeg_geterror(ret).c_str());
            // unused frame
            av_frame_free(&frame);
            break;
        }

        if (m_hwaccel_enable_dec_buffering && frame != nullptr)
        {
            AVFrame *sw_frame;

            // If decoding is done in hardware, the resulting frame data needs to be copied to software memory
            //ret = hwframe_copy_from_hw(m_in.m_video.m_codec_ctx, &sw_frame, frame);

            ret = init_frame(&sw_frame, destname());
            if (ret < 0)
            {
                // unused frame
                av_frame_free(&frame);
                break;
            }

            // retrieve data from GPU to CPU
            ret = av_hwframe_transfer_data(sw_frame, frame, 0); // hwframe_copy_from_hw
            // Free unused frame
            av_frame_free(&frame);
            if (ret < 0)
            {
                Logging::error(filename(), "Error transferring the data to system memory (error '%1').", ffmpeg_geterror(ret).c_str());
                // unused frame
                av_frame_free(&sw_frame);
                break;
            }
            frame = sw_frame;
        }

        again = true;
        *decoded += pkt->size;
#endif

        // Sometimes only a few packets contain valid dts/pts/pos data, so we keep it
        if (pkt->dts != AV_NOPTS_VALUE)
        {
            int64_t pts = pkt->dts;
            if (pts > m_pts)
            {
                m_pts = pts;
            }
        }
        else if (pkt->pts != AV_NOPTS_VALUE)
        {
            int64_t pts = pkt->pts;
            if (pts > m_pts)
            {
                m_pts = pts;
            }
        }

        if (pkt->pos > -1)
        {
            m_pos = pkt->pos;
        }

        if (frame != nullptr)
        {
            if (data_present && !(frame->flags & AV_FRAME_FLAG_CORRUPT || frame->flags & AV_FRAME_FLAG_DISCARD))
            {
                frame = send_filters(frame, ret);
                if (ret)
                {
                    av_frame_free(&frame);
                    return ret;
                }

                if (m_sws_ctx != nullptr)
                {
                    AVCodecContext *output_codec_ctx = m_out.m_video.m_codec_ctx;

                    AVFrame * tmp_frame = alloc_picture(m_out.m_pix_fmt, output_codec_ctx->width, output_codec_ctx->height);
                    if (tmp_frame == nullptr)
                    {
                        av_frame_free(&frame);
                        return AVERROR(ENOMEM);
                    }

                    sws_scale(m_sws_ctx,
                              static_cast<const uint8_t * const *>(frame->data), frame->linesize,
                              0, frame->height,
                              tmp_frame->data, tmp_frame->linesize);

                    tmp_frame->pts = frame->pts;
                    tmp_frame->best_effort_timestamp = frame->best_effort_timestamp;

                    av_frame_free(&frame);

                    frame = tmp_frame;
                }

#if LAVF_DEP_AVSTREAM_CODEC
                int64_t best_effort_timestamp = frame->best_effort_timestamp;
#else
                int64_t best_effort_timestamp = av_frame_get_best_effort_timestamp(frame);
#endif

                if (best_effort_timestamp != AV_NOPTS_VALUE)
                {
                    frame->pts = best_effort_timestamp;
                }

                if (frame->pts == AV_NOPTS_VALUE)
                {
                    frame->pts = m_pts;
                }

                if (m_out.m_video.m_stream != nullptr && frame->pts != AV_NOPTS_VALUE)
                {
                    if (m_in.m_video.m_stream->time_base.den != m_out.m_video.m_stream->time_base.den || m_in.m_video.m_stream->time_base.num != m_out.m_video.m_stream->time_base.num)
                    {
                        frame->pts = av_rescale_q_rnd(frame->pts, m_in.m_video.m_stream->time_base, m_out.m_video.m_stream->time_base, static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                    }

                    // Fix for issue #46: bitrate too high.
                    // Solution found here https://stackoverflow.com/questions/11466184/setting-video-bit-rate-through-ffmpeg-api-is-ignored-for-libx264-codec
                    // This is permanently used in the current ffmpeg.c code (see commit: e3fb9af6f1353f30855eaa1cbd5befaf06e303b8 Date:Wed Jan 22 15:52:10 2020 +0100)
                    frame->pts = av_rescale_q(frame->pts, m_out.m_video.m_stream->time_base, m_out.m_video.m_codec_ctx->time_base);
                }

                frame->quality      = m_out.m_video.m_codec_ctx->global_quality;
                frame->pict_type    = AV_PICTURE_TYPE_NONE;	// other than AV_PICTURE_TYPE_NONE causes warnings

                m_video_frame_fifo.push(frame);
            }
            else
            {
                // unused frame
                av_frame_free(&frame);
            }
        }
    }

    return ret;
}

int FFmpeg_Transcoder::store_packet(AVPacket *pkt, AVMediaType mediatype)
{
    if (is_hls() && pkt->pts != AV_NOPTS_VALUE)
    {
        switch (mediatype)
        {
        case AVMEDIA_TYPE_AUDIO:
        {
            int64_t pos = av_rescale_q_rnd(pkt->pts - m_out.m_audio_start_time, m_out.m_audio.m_stream->time_base, av_get_time_base_q(), static_cast<AVRounding>(AV_ROUND_UP | AV_ROUND_PASS_MINMAX));
            if (pos < 0)
            {
                pos = 0;
            }
            uint32_t next_segment = static_cast<uint32_t>(pos / params.m_segment_duration + 1);

            //Logging::error(destname(), "AUDIO PACKET      next %1 pos %2 %3", next_segment, pos, format_duration(pos).c_str());

            if (next_segment == m_current_segment + 1)
            {
                if (!(m_inhibit_stream_msk & FFMPEGFS_AUDIO))
                {
                    m_inhibit_stream_msk |= FFMPEGFS_AUDIO;

                    Logging::debug(destname(), "AUDIO SKIP PACKET next %1 pos %2 %3", next_segment, pos, format_duration(pos).c_str());
                }

                m_hls_packet_fifo.push(av_packet_clone(pkt));
                return 0;
            }
            break;
        }
        case AVMEDIA_TYPE_VIDEO:
        {
            int64_t pos = av_rescale_q_rnd(pkt->pts - m_out.m_video_start_time, m_out.m_video.m_stream->time_base, av_get_time_base_q(), static_cast<AVRounding>(AV_ROUND_UP | AV_ROUND_PASS_MINMAX));
            if (pos < 0)
            {
                pos = 0;
            }
            uint32_t next_segment = static_cast<uint32_t>(pos / params.m_segment_duration + 1);

            //Logging::error(destname(), "VIDEO PACKET      next %1 pos %2 %3", next_segment, pos, format_duration(pos).c_str());

            if (next_segment == m_current_segment + 1)
            {
                if (!(m_inhibit_stream_msk & FFMPEGFS_VIDEO))
                {
                    m_inhibit_stream_msk |= FFMPEGFS_VIDEO;

                    Logging::debug(destname(), "VIDEO SKIP PACKET next %1 pos %2 %3", next_segment, pos, format_duration(pos).c_str());
				}
	            m_hls_packet_fifo.push(av_packet_clone(pkt));
	            return 0;
            }
            break;
        }
        default:
        {
            break;
        }
        }
    }

#ifdef USE_INTERLEAVED_WRITE
    int ret = av_interleaved_write_frame(m_out.m_format_ctx, pkt);
#else   // !USE_INTERLEAVED_WRITE
    int ret = av_write_frame(m_out.m_format_ctx, pkt);
#endif  // !USE_INTERLEAVED_WRITE

    if (ret < 0)
    {
        const char *type;

        if (mediatype != AVMEDIA_TYPE_ATTACHMENT)
        {
            type = av_get_media_type_string(mediatype);

            if (type == nullptr)
            {
                type = "unknown";
            }
        }
        else
        {
            type = "album art";
        }

        Logging::error(destname(), "Could not write %1 frame (error '%2').", type, ffmpeg_geterror(ret).c_str());
    }

    return ret;
}

int FFmpeg_Transcoder::decode_frame(AVPacket *pkt)
{
    int ret = 0;

    if (m_in.m_audio.m_stream != nullptr && pkt->stream_index == m_in.m_audio.m_stream_idx && m_out.m_audio.m_stream_idx != INVALID_STREAM)
    {
        if (m_reset_pts & FFMPEGFS_AUDIO && pkt->pts != AV_NOPTS_VALUE)
        {
            m_reset_pts &= ~FFMPEGFS_AUDIO; // Clear reset bit

            int64_t pts = av_rescale_q(pkt->pts, m_in.m_audio.m_stream->time_base, av_get_time_base_q());

            m_out.m_audio_pts = av_rescale_q(pts, av_get_time_base_q(), m_out.m_audio.m_stream->time_base);
			
            Logging::debug(destname(), "Reset PTS from audio packet to %1", format_duration(pts).c_str());
        }

        if (!m_copy_audio)
        {
            int decoded = 0;
            ret = decode_audio_frame(pkt, &decoded);
        }
        else
        {
            pkt->stream_index   = m_out.m_audio.m_stream_idx;
            if (pkt->pts != AV_NOPTS_VALUE)
            {
                pkt->pts            = av_rescale_q_rnd(pkt->pts, m_in.m_audio.m_stream->time_base, m_out.m_audio.m_stream->time_base, static_cast<AVRounding>(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
            }
            if (pkt->dts != AV_NOPTS_VALUE)
            {
                pkt->dts            = av_rescale_q_rnd(pkt->dts, m_in.m_audio.m_stream->time_base, m_out.m_audio.m_stream->time_base, static_cast<AVRounding>(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
            }
            if (pkt->duration)
            {
                pkt->duration       = static_cast<int>(av_rescale_q(pkt->duration, m_in.m_audio.m_stream->time_base, m_out.m_audio.m_stream->time_base));
            }
            pkt->pos            = -1;

            ret = store_packet(pkt, AVMEDIA_TYPE_AUDIO);
        }
    }
    else if (m_in.m_video.m_stream != nullptr && pkt->stream_index == m_in.m_video.m_stream_idx && (m_out.m_video.m_stream_idx != INVALID_STREAM || is_frameset()))
    {
        if (m_reset_pts & FFMPEGFS_VIDEO && pkt->pts != AV_NOPTS_VALUE)
        {
            m_reset_pts &= ~FFMPEGFS_VIDEO; // Clear reset bit

            int64_t pts = av_rescale_q(pkt->pts, m_in.m_video.m_stream->time_base, av_get_time_base_q());

            m_out.m_video_pts = av_rescale_q(pts, av_get_time_base_q(), m_out.m_video.m_stream->time_base);

            Logging::debug(destname(), "Reset PTS from video packet to %1", format_duration(pts).c_str());
        }

        if (!m_copy_video)
        {
            int decoded = 0;
            /**
              * @todo Calling decode_video_frame until all data has been used, but for
              * DVDs only. Can someone tell me why this seems required??? If this is not
              * done some videos become garbled. But only for DVDs... @n
              * @n
              * With fix: all DVDs OK, some Blurays (e.g. Phil Collins) not... @n
              * With fix: all DVDs shitty, but Blurays OK. @n
              * @n
              * Applying fix for DVDs only.
              */
#ifndef USE_LIBDVD
            ret = decode_video_frame(pkt, &decoded);
#else //USE_LIBDVD
            if (m_virtualfile->m_type != VIRTUALTYPE_DVD)
            {
                ret = decode_video_frame(pkt, &decoded);
            }
            else
            {
#if LAVC_NEW_PACKET_INTERFACE
                int lastret = 0;
#endif
                do
                {
                    // Decode one frame.
                    ret = decode_video_frame(pkt, &decoded);

#if LAVC_NEW_PACKET_INTERFACE
                    if ((ret == AVERROR(EAGAIN) && ret == lastret) || ret == AVERROR_EOF)
                    {
                        // If EAGAIN reported twice or stream at EOF
                        // quit loop, but this is not an error
                        // (must process all streams).
                        break;
                    }

                    if (ret < 0 && ret != AVERROR(EAGAIN))
                    {
                        Logging::error(filename(), "Could not decode frame (error '%1').", ffmpeg_geterror(ret).c_str());
                        return ret;
                    }

                    lastret = ret;
#else
                    if (ret < 0)
                    {
                        Logging::error(filename(), "Could not decode frame (error '%1').", ffmpeg_geterror(ret).c_str());
                        return ret;
                    }
#endif
                    pkt->data += decoded;
                    pkt->size -= decoded;
                }
#if LAVC_NEW_PACKET_INTERFACE
                while (pkt->size > 0 && (ret == 0 || ret == AVERROR(EAGAIN)));
#else
                while (pkt->size > 0);
#endif
                ret = 0;
            }
#endif // USE_LIBDVD
        }
        else
        {
            pkt->stream_index   = m_out.m_video.m_stream_idx;
            av_packet_rescale_ts(pkt, m_in.m_video.m_stream->time_base, m_out.m_video.m_stream->time_base);
            pkt->pos            = -1;

            ret = store_packet(pkt, AVMEDIA_TYPE_VIDEO);
        }
    }
    else
    {
        for (size_t n = 0; n < m_in.m_album_art.size(); n++)
        {
            AVStream *input_stream = m_in.m_album_art.at(n).m_stream;

            // AV_DISPOSITION_ATTACHED_PIC streams already processed in process_albumarts()
            if (pkt->stream_index == input_stream->index && !(input_stream->disposition & AV_DISPOSITION_ATTACHED_PIC))
            {
                AVStream *output_stream = m_out.m_album_art.at(n).m_stream;

                ret = add_albumart_frame(output_stream, pkt);
                break;
            }
        }
    }

    if (!params.m_decoding_errors && ret < 0 && ret != AVERROR(EAGAIN))
    {
        ret = 0;
    }

    return ret;
}

int FFmpeg_Transcoder::init_converted_samples(uint8_t ***converted_input_samples, int frame_size)
{
    int ret;

    // Allocate as many pointers as there are audio channels.
    // Each pointer will later point to the audio samples of the corresponding
    // channels (although it may be nullptr for interleaved formats).

    *converted_input_samples = static_cast<uint8_t **>(av_calloc(static_cast<size_t>(m_out.m_audio.m_codec_ctx->channels), sizeof(**converted_input_samples)));

    if (*converted_input_samples == nullptr)
    {
        Logging::error(destname(), "Could not allocate converted input sample pointers.");
        return AVERROR(ENOMEM);
    }

    // Allocate memory for the samples of all channels in one consecutive
    // block for convenience.
    ret = av_samples_alloc(*converted_input_samples, nullptr,
                           m_out.m_audio.m_codec_ctx->channels,
                           frame_size,
                           m_out.m_audio.m_codec_ctx->sample_fmt, 0);
    if (ret < 0)
    {
        Logging::error(destname(), "Could not allocate converted input samples (error '%1').", ffmpeg_geterror(ret).c_str());
        av_freep(&(*converted_input_samples)[0]);
        av_free(*converted_input_samples);
        return ret;
    }
    return 0;
}

#if LAVR_DEPRECATE
int FFmpeg_Transcoder::convert_samples(uint8_t **input_data, int in_samples, uint8_t **converted_data, int *out_samples)
{
    if (m_audio_resample_ctx != nullptr)
    {
        int ret;

        // Convert the samples using the resampler.
        ret = swr_convert(m_audio_resample_ctx, converted_data, *out_samples, const_cast<const uint8_t **>(input_data), in_samples);
        if (ret  < 0)
        {
            Logging::error(destname(), "Could not convert input samples (error '%1').", ffmpeg_geterror(ret).c_str());
            return ret;
        }

        *out_samples = ret;
    }
    else
    {
        // No resampling, just copy samples
        if (!av_sample_fmt_is_planar(m_in.m_audio.m_codec_ctx->sample_fmt))
        {
            memcpy(converted_data[0], input_data[0], static_cast<size_t>(in_samples * av_get_bytes_per_sample(m_out.m_audio.m_codec_ctx->sample_fmt) * m_in.m_audio.m_codec_ctx->channels));
        }
        else
        {
            size_t samples = static_cast<size_t>(in_samples * av_get_bytes_per_sample(m_out.m_audio.m_codec_ctx->sample_fmt));
            for (int n = 0; n < m_in.m_audio.m_codec_ctx->channels; n++)
            {
                memcpy(converted_data[n], input_data[n], samples);
            }
        }
    }
    return 0;
}
#else
int FFmpeg_Transcoder::convert_samples(uint8_t **input_data, const int in_samples, uint8_t **converted_data, int *out_samples)
{
    if (m_audio_resample_ctx != nullptr)
    {
        int ret;

        // Convert the samples using the resampler.
        ret = avresample_convert(m_audio_resample_ctx, converted_data, 0, *out_samples, input_data, 0, in_samples);
        if (ret < 0)
        {
            Logging::error(destname(), "Could not convert input samples (error '%1').", ffmpeg_geterror(ret).c_str());
            return ret;
        }

        *out_samples = ret;

        // Perform a sanity check so that the number of converted samples is
        // not greater than the number of samples to be converted.
        // If the sample rates differ, this case has to be handled differently

        if (avresample_available(m_audio_resample_ctx))
        {
            Logging::error(destname(), "Converted samples left over.");
            return AVERROR_EXIT;
        }
    }
    else
    {
        // No resampling, just copy samples
        if (!av_sample_fmt_is_planar(m_in.m_audio.m_codec_ctx->sample_fmt))
        {
            memcpy(converted_data[0], input_data[0], in_samples * av_get_bytes_per_sample(m_out.m_audio.m_codec_ctx->sample_fmt) * m_in.m_audio.m_codec_ctx->channels);
        }
        else
        {
            for (int n = 0; n < m_in.m_audio.m_codec_ctx->channels; n++)
            {
                memcpy(converted_data[n], input_data[n], in_samples * av_get_bytes_per_sample(m_out.m_audio.m_codec_ctx->sample_fmt));
            }
        }
    }
    return 0;
}
#endif

int FFmpeg_Transcoder::add_samples_to_fifo(uint8_t **converted_input_samples, int frame_size)
{
    int ret;

    // Make the FIFO as large as it needs to be to hold both,
    // the old and the new samples.

    ret = av_audio_fifo_realloc(m_audio_fifo, av_audio_fifo_size(m_audio_fifo) + frame_size);
    if (ret < 0)
    {
        Logging::error(destname(), "Could not reallocate FIFO.");
        return ret;
    }

    // Store the new samples in the FIFO buffer.
    ret = av_audio_fifo_write(m_audio_fifo, reinterpret_cast<void **>(converted_input_samples), frame_size);
    if (ret < frame_size)
    {
        if (ret < 0)
        {
            Logging::error(destname(), "Could not write data to FIFO (error '%1').", ffmpeg_geterror(ret).c_str());
        }
        else
        {
            Logging::error(destname(), "Could not write data to FIFO.");
            ret = AVERROR_EXIT;
        }
        return AVERROR_EXIT;
    }

    return 0;
}

int FFmpeg_Transcoder::flush_frames_all(bool use_flush_packet)
{
    int ret = 0;

    if (m_in.m_audio.m_codec_ctx != nullptr)
    {
        int ret2 = flush_frames_single(m_in.m_audio.m_stream_idx, use_flush_packet);
        if (ret2 < 0)
        {
            ret = ret2;
        }
    }

    if (m_in.m_video.m_codec_ctx != nullptr)
    {
        int ret2 = flush_frames_single(m_in.m_video.m_stream_idx, use_flush_packet);
        if (ret2 < 0)
        {
            ret = ret2;
        }
    }

    return ret;
}

int FFmpeg_Transcoder::flush_frames_single(int stream_index, bool use_flush_packet)
{
    int ret = 0;

    if (stream_index > INVALID_STREAM)
    {
        int (FFmpeg_Transcoder::*decode_frame_ptr)(AVPacket *pkt, int *decoded) = nullptr;

        if (!m_copy_audio && stream_index == m_in.m_audio.m_stream_idx && m_out.m_audio.m_stream_idx > -1)
        {
            decode_frame_ptr = &FFmpeg_Transcoder::decode_audio_frame;
        }
        else if (!m_copy_video && stream_index == m_in.m_video.m_stream_idx && (m_out.m_video.m_stream_idx != INVALID_STREAM || is_frameset()))
        {
            decode_frame_ptr = &FFmpeg_Transcoder::decode_video_frame;
        }

        if (decode_frame_ptr != nullptr)
        {
#if !LAVC_DEP_AV_INIT_PACKET
            AVPacket pkt;
#endif // !LAVC_DEP_AV_INIT_PACKET
            AVPacket *flush_packet = nullptr;
            int decoded = 0;

            if (use_flush_packet)
            {
#if LAVC_DEP_AV_INIT_PACKET
                flush_packet = av_packet_alloc();
#else // !LAVC_DEP_AV_INIT_PACKET
                flush_packet = &pkt;

                init_packet(flush_packet);
#endif // !LAVC_DEP_AV_INIT_PACKET

                flush_packet->data          = nullptr;
                flush_packet->size          = 0;
                flush_packet->stream_index  = stream_index;
            }

            do
            {
                ret = (this->*decode_frame_ptr)(flush_packet, &decoded);
                if (ret < 0 && ret != AVERROR(EAGAIN))
                {
                    break;
                }
            }
            while (decoded);

            av_packet_unref(flush_packet);
#if LAVC_DEP_AV_INIT_PACKET
            av_packet_free(&flush_packet);
#endif // LAVC_DEP_AV_INIT_PACKET
        }
    }

    return ret;
}

int FFmpeg_Transcoder::read_decode_convert_and_store(int *finished)
{
    // Packet used for temporary storage.
    AVPacket pkt;
    int ret = 0;

    try
    {
        // Read one frame from the input file into a temporary packet.
        ret = av_read_frame(m_in.m_format_ctx, &pkt);
        if (ret < 0)
        {
            if (ret == AVERROR_EOF)
            {
                // If we are the the end of the file, flush the decoder below.
                *finished = 1;
                Logging::trace(destname(), "Read to EOF.");
            }
            else
            {
                Logging::error(destname(), "Could not read frame (error '%1').", ffmpeg_geterror(ret).c_str());
                throw ret;
            }
        }

        if (m_virtualfile->m_flags & VIRTUALFLAG_CUESHEET)
        {
            // Check for end of cue sheet track
            ///<* @todo Cue sheet track: Must check video stream, too and end if both all video and audio packets arrived. Discard packets exceeding duration.
            if (pkt.stream_index == m_in.m_audio.m_stream_idx)
            {
                int64_t pts = av_rescale_q(pkt.pts, m_in.m_audio.m_stream->time_base, av_get_time_base_q());
                if (pts > m_virtualfile->m_cuesheet.m_start + m_virtualfile->m_cuesheet.m_duration)
                {
                    Logging::trace(destname(), "Read to end of track.");
                    *finished = 1;
                    ret = AVERROR_EOF;
                }
            }
        }

        if (!*finished)
        {
            // Decode one packet, at least with the old API (!LAV_NEW_PACKET_INTERFACE)
            // it seems a packet can contain more than one frame so loop around it
            // if necessary...
            ret = decode_frame(&pkt);

            if (ret < 0 && ret != AVERROR(EAGAIN))
            {
                throw ret;
            }
        }
        else
        {
            // Flush cached frames, ignoring any errors
            flush_frames_all(true);
        }

        ret = 0;    // Errors will be reported by exception
    }
    catch (int _ret)
    {
        ret = _ret;
    }

    av_packet_unref(&pkt);

    return ret;
}

int FFmpeg_Transcoder::init_audio_output_frame(AVFrame **frame, int frame_size)
{
    int ret;

    // Create a new frame to store the audio samples.
    ret = init_frame(frame, destname());
    if (ret < 0)
    {
        return AVERROR_EXIT;
    }

    //
    // Set the frame's parameters, especially its size and format.
    // av_frame_get_buffer needs this to allocate memory for the
    // audio samples of the frame.
    // Default channel layouts based on the number of channels
    // are assumed for simplicity.

    (*frame)->nb_samples        = frame_size;
    (*frame)->channel_layout    = m_out.m_audio.m_codec_ctx->channel_layout;
    (*frame)->format            = m_out.m_audio.m_codec_ctx->sample_fmt;
    (*frame)->sample_rate       = m_out.m_audio.m_codec_ctx->sample_rate;

    // Allocate the samples of the created frame. This call will make
    // sure that the audio frame can hold as many samples as specified.
    // 29.05.2021: Let API decide about alignment. Should be properly set for the current CPU.

    ret = av_frame_get_buffer(*frame, 0);
    if (ret < 0)
    {
        Logging::error(destname(), "Could allocate output frame samples (error '%1').", ffmpeg_geterror(ret).c_str());
        av_frame_free(frame);
        return ret;
    }

    return 0;
}

void FFmpeg_Transcoder::produce_audio_dts(AVPacket *pkt)
{
    if (pkt->pts == AV_NOPTS_VALUE && pkt->dts == AV_NOPTS_VALUE)
    {
        // Normally we have already added the PTS to the frame when it was created. Just in case
        // this failed, and there are no valid PTS/DTS valuesm we add it here.
        int64_t duration;

        // Some encoders to not produce dts/pts.
        // So we make some up.
        if (pkt->duration)
        {
            duration = pkt->duration;

            if (m_out.m_audio.m_codec_ctx->codec_id == AV_CODEC_ID_OPUS || m_current_format->filetype() == FILETYPE_TS || m_current_format->filetype() == FILETYPE_HLS)
            {
                /** @todo Is this a FFmpeg bug or am I too stupid? @n
                 * OPUS is a bit strange. Whatever we feed into the encoder, the result will always be floating point planar
                 * at 48 K sampling rate. @n
                 * For some reason the duration calculated by the FFMpeg API is wrong. We have to rescale it to the correct value.
                 * Same applies to mpegts, so let's rescale.
                 */
                if (duration > 0 && CODECPAR(m_out.m_audio.m_stream)->sample_rate > 0)
                {
                    pkt->duration = duration = static_cast<int>(av_rescale(duration, static_cast<int64_t>(m_out.m_audio.m_stream->time_base.den) * m_out.m_audio.m_codec_ctx->ticks_per_frame, CODECPAR(m_out.m_audio.m_stream)->sample_rate * static_cast<int64_t>(m_out.m_audio.m_stream->time_base.num)));
                }
            }
        }
        else
        {
            duration = 1;
        }

        pkt->dts = m_out.m_audio_pts - 1;
        pkt->pts = m_out.m_audio_pts;

        m_out.m_audio_pts += duration;
    }
}

int FFmpeg_Transcoder::encode_audio_frame(const AVFrame *frame, int *data_present)
{
    // Packet used for temporary storage.
#if !LAVC_DEP_AV_INIT_PACKET
    AVPacket tmp_pkt;
#endif // !LAVC_DEP_AV_INIT_PACKET
    AVPacket *pkt;
    int ret;

#if LAVC_DEP_AV_INIT_PACKET
    pkt = av_packet_alloc();
#else // !LAVC_DEP_AV_INIT_PACKET
    pkt = &tmp_pkt;
    init_packet(pkt);
#endif // !LAVC_DEP_AV_INIT_PACKET

    // Encode the audio frame and store it in the temporary packet.
    // The output audio stream encoder is used to do this.
#if !LAVC_NEW_PACKET_INTERFACE
    ret = avcodec_encode_audio2(m_out.m_audio.m_codec_ctx, pkt, frame, data_present);

    if (ret < 0)
    {
        Logging::error(destname(), "Could not encode audio frame (error '%1').", ffmpeg_geterror(ret).c_str());
        av_packet_unref(pkt);
        return ret;
    }

    {
#else
    *data_present = 0;

    // send the frame for encoding
    ret = avcodec_send_frame(m_out.m_audio.m_codec_ctx, frame);
    if (ret < 0 && ret != AVERROR_EOF)
    {
        Logging::error(destname(), "Could not encode audio frame at PTS=%1 (error %2').", av_rescale_q(frame->pts, m_in.m_audio.m_stream->time_base, av_get_time_base_q()), ffmpeg_geterror(ret).c_str());
        av_packet_unref(pkt);
        return ret;
    }

    // read all the available output packets (in general there may be any number of them)
    while (ret >= 0)
    {
        *data_present = 0;

        ret = avcodec_receive_packet(m_out.m_audio.m_codec_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            av_packet_unref(pkt);
            return ret;
        }
        else if (ret < 0)
        {
            Logging::error(destname(), "Could not encode audio frame (error '%1').", ffmpeg_geterror(ret).c_str());
            av_packet_unref(pkt);
            return ret;
        }

        *data_present = 1;
#endif
        // Write one audio frame from the temporary packet to the output buffer.
        if (*data_present)
        {
            pkt->stream_index = m_out.m_audio.m_stream_idx;

            produce_audio_dts(pkt);

            ret = store_packet(pkt, AVMEDIA_TYPE_AUDIO);
            if (ret < 0)
            {
                av_packet_unref(pkt);
                return ret;
            }
        }

        av_packet_unref(pkt);
    }

#if LAVC_DEP_AV_INIT_PACKET
    av_packet_free(&pkt);
#endif // LAVC_DEP_AV_INIT_PACKET

    return 0;
}

int FFmpeg_Transcoder::encode_image_frame(const AVFrame *frame, int *data_present)
{
    *data_present = 0;

    if (frame == nullptr || m_skip_next_frame)
    {
        // This called internally to flush frames. We do not have a cache to flush, so simply ignore that.
        // After seek oprations we need to skip the first frame.
        m_skip_next_frame = false;
        return 0;
    }

    if (m_current_format == nullptr)
    {
        Logging::error(destname(), "Internal - missing format.");
        return AVERROR(EINVAL);
    }

    if (m_buffer == nullptr)
    {
        Logging::error(destname(), "Internal - cache not open.");
        return AVERROR(EINVAL);
    }

#if !LAVC_DEP_AV_INIT_PACKET
    AVPacket tmp_pkt;
#endif // !LAVC_DEP_AV_INIT_PACKET
    AVPacket *pkt = nullptr;
    AVFrame *cloned_frame = av_frame_clone(frame);  // Clone frame. Does not copy data but references it, only the properties are copied. Not a big memory impact.
    int ret = 0;
    try
    {
#if LAVC_DEP_AV_INIT_PACKET
        pkt = av_packet_alloc();
#else // !LAVC_DEP_AV_INIT_PACKET
        pkt = &tmp_pkt;
        init_packet(pkt);
#endif // !LAVC_DEP_AV_INIT_PACKET

        uint32_t frame_no = pts_to_frame(m_in.m_video.m_stream, frame->pts);

        if (m_current_format->video_codec_id() == AV_CODEC_ID_MJPEG)
        {
            // The MJEPG codec requires monotonically growing PTS values so we fake some to avoid them going backwards after seeks
            cloned_frame->pts = frame_to_pts(m_in.m_video.m_stream, ++m_fake_frame_no);
        }

#if !LAVC_NEW_PACKET_INTERFACE
        ret = avcodec_encode_video2(m_out.m_video.m_codec_ctx, pkt, frame, data_present);
        if (ret < 0)
        {
            Logging::error(destname(), "Could not encode image frame (error '%1').", ffmpeg_geterror(ret).c_str());
            av_packet_unref(pkt);
            throw ret;
        }

        {
#else
        *data_present = 0;

        // send the frame for encoding
        ret = avcodec_send_frame(m_out.m_video.m_codec_ctx, cloned_frame);
        if (ret < 0 && ret != AVERROR_EOF)
        {
            Logging::error(destname(), "Could not encode image frame (error '%1').", ffmpeg_geterror(ret).c_str());
            throw ret;
        }

        // read all the available output packets (in general there may be any number of them
        while (ret >= 0)
        {
            *data_present = 0;

            ret = avcodec_receive_packet(m_out.m_video.m_codec_ctx, pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            {
                av_packet_unref(pkt);
                break;
            }
            else if (ret < 0)
            {
                Logging::error(destname(), "Could not encode image frame (error '%1').", ffmpeg_geterror(ret).c_str());
                throw ret;
            }

            *data_present = 1;
#endif
            // Write one video frame from the temporary packet to the output buffer.
            if (*data_present)
            {
                // Store current video PTS
                if (pkt->pts != AV_NOPTS_VALUE)
                {
                    m_out.m_video_pts = pkt->pts;
                }

                m_buffer->write_frame(pkt->data, static_cast<size_t>(pkt->size), frame_no);

                if (m_last_seek_frame_no == frame_no)    // Skip frames until seek pos
                {
                    m_last_seek_frame_no = 0;
                }
            }

            av_frame_free(&cloned_frame);
            av_packet_unref(pkt);
        }
    }
    catch (int _ret)
    {
        av_frame_free(&cloned_frame);
        av_packet_unref(pkt);
        ret = _ret;
    }

#if LAVC_DEP_AV_INIT_PACKET
    av_packet_free(&pkt);
#endif // LAVC_DEP_AV_INIT_PACKET

    return ret;
}

int FFmpeg_Transcoder::encode_video_frame(const AVFrame *frame, int *data_present)
{
    if (m_out.m_video.m_stream == nullptr)
    {
        return 0; // ignore, avoid crash
    }

    // Packet used for temporary storage.
    if (frame != nullptr)
    {
#if LAVF_DEP_AVSTREAM_CODEC
        if (frame->interlaced_frame)
        {
            if (m_out.m_video.m_codec_ctx->codec->id == AV_CODEC_ID_MJPEG)
            {
                m_out.m_video.m_stream->codecpar->field_order = frame->top_field_first ? AV_FIELD_TT:AV_FIELD_BB;
            }
            else
            {
                m_out.m_video.m_stream->codecpar->field_order = frame->top_field_first ? AV_FIELD_TB:AV_FIELD_BT;
            }
        }
        else
        {
            m_out.m_video.m_stream->codecpar->field_order = AV_FIELD_PROGRESSIVE;
        }
#endif
    }
#if !LAVC_DEP_AV_INIT_PACKET
    AVPacket tmp_pkt;
#endif // !LAVC_DEP_AV_INIT_PACKET
    AVPacket *pkt = nullptr;
    AVFrame *hw_frame = nullptr;
    int ret = 0;

    try
    {
        if (m_hwaccel_enable_enc_buffering && frame != nullptr)
        {
            // If encoding is done in hardware, the resulting frame data needs to be copied to hardware
            ret = hwframe_copy_to_hw(m_out.m_video.m_codec_ctx, &hw_frame, frame);
            if (ret < 0)
            {
                throw ret;
            }
            frame = hw_frame;
        }

#if LAVC_DEP_AV_INIT_PACKET
        pkt = av_packet_alloc();
#else // !LAVC_DEP_AV_INIT_PACKET
        pkt = &tmp_pkt;
        init_packet(pkt);
#endif // !LAVC_DEP_AV_INIT_PACKET

        // Encode the video frame and store it in the temporary packet.
        // The output video stream encoder is used to do this.
#if !LAVC_NEW_PACKET_INTERFACE
        ret = avcodec_encode_video2(m_out.m_video.m_codec_ctx, pkt, frame, data_present);

        if (ret < 0)
        {
            Logging::error(destname(), "Could not encode video frame (error '%1').", ffmpeg_geterror(ret).c_str());
            av_packet_unref(pkt);
            throw ret;
        }

        {
#else
        *data_present = 0;

        // send the frame for encoding
        ret = avcodec_send_frame(m_out.m_video.m_codec_ctx, frame);
        if (ret < 0 && ret != AVERROR_EOF)
        {
            Logging::error(destname(), "Could not encode video frame at PTS=%1 (error %2').", av_rescale_q(frame->pts, m_in.m_video.m_stream->time_base, av_get_time_base_q()), ffmpeg_geterror(ret).c_str());
            throw ret;
        }

        // read all the available output packets (in general there may be any number of them
        while (ret >= 0)
        {
            *data_present = 0;

            ret = avcodec_receive_packet(m_out.m_video.m_codec_ctx, pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            {
                av_packet_unref(pkt);
                break;
            }
            else if (ret < 0)
            {
                Logging::error(destname(), "Could not encode video frame (error '%1').", ffmpeg_geterror(ret).c_str());
                throw ret;
            }

            *data_present = 1;
#endif

            // Write one video frame from the temporary packet to the output buffer.
            if (*data_present)
            {
                // Fix for issue #46: bitrate too high.
                av_packet_rescale_ts(pkt, m_out.m_video.m_codec_ctx->time_base, m_out.m_video.m_stream->time_base);

                if (!(m_out.m_format_ctx->oformat->flags & AVFMT_NOTIMESTAMPS))
                {
                    if (pkt->dts != AV_NOPTS_VALUE &&
                            pkt->pts != AV_NOPTS_VALUE &&
                            pkt->dts > pkt->pts &&
                            m_out.m_last_mux_dts != AV_NOPTS_VALUE)
                    {

                        Logging::warning(destname(), "Invalid DTS: %1 PTS: %2 in video output, replacing by guess.", pkt->dts, pkt->pts);

                        pkt->pts =
                                pkt->dts = pkt->pts + pkt->dts + m_out.m_last_mux_dts + 1
                                - FFMIN3(pkt->pts, pkt->dts, m_out.m_last_mux_dts + 1)
                                - FFMAX3(pkt->pts, pkt->dts, m_out.m_last_mux_dts + 1);
                    }

                    if (pkt->dts != AV_NOPTS_VALUE && m_out.m_last_mux_dts != AV_NOPTS_VALUE)
                    {
                        int64_t max = m_out.m_last_mux_dts + !(m_out.m_format_ctx->oformat->flags & AVFMT_TS_NONSTRICT);
                        // AVRational avg_frame_rate = { m_out.m_video.m_stream->avg_frame_rate.den, m_out.m_video.m_stream->avg_frame_rate.num };
                        // int64_t max = m_out.m_last_mux_dts + av_rescale_q(1, avg_frame_rate, m_out.m_video.m_stream->time_base);

                        if (pkt->dts < max)
                        {
                            Logging::trace(destname(), "Non-monotonous DTS in video output stream; previous: %1, current: %2; changing to %3. This may result in incorrect timestamps in the output.", m_out.m_last_mux_dts, pkt->dts, max);

                            if (pkt->pts >= pkt->dts)
                            {
                                pkt->pts = FFMAX(pkt->pts, max);
                            }
                            pkt->dts = max;
                        }
                    }
                }

                if (pkt->pts != AV_NOPTS_VALUE)
                {
                    m_out.m_video_pts       = pkt->pts;
                    m_out.m_last_mux_dts    = (pkt->dts != AV_NOPTS_VALUE) ? pkt->dts : (pkt->pts - pkt->duration);
                }

                if (frame != nullptr && !pkt->duration)
                {
                    pkt->duration = frame->pkt_duration;
                }

                // Write packet to buffer
                ret = store_packet(pkt, AVMEDIA_TYPE_VIDEO);
                if (ret < 0)
                {
                    throw ret;
                }
            }

            av_packet_unref(pkt);
        }
    }
    catch (int _ret)
    {
        av_packet_unref(pkt);
        ret = _ret;
    }

    // If we copied the frame from RAM to hardware we need to free the hardware frame
    if (hw_frame != nullptr)
    {
        av_frame_free(&hw_frame);
    }

#if LAVC_DEP_AV_INIT_PACKET
    av_packet_free(&pkt);
#endif // LAVC_DEP_AV_INIT_PACKET

    return ret;
}

int FFmpeg_Transcoder::create_audio_frame(int frame_size)
{
    // Temporary storage of the output samples of the frame written to the file.
    AVFrame *output_frame;
    int ret = 0;

    // Use the maximum number of possible samples per frame.
    // If there is less than the maximum possible frame size in the FIFO
    // buffer use this number. Otherwise, use the maximum possible frame size

    frame_size = FFMIN(av_audio_fifo_size(m_audio_fifo), frame_size);

    // Initialise temporary storage for one output frame.
    ret = init_audio_output_frame(&output_frame, frame_size);
    if (ret < 0)
    {
        return ret;
    }

    // Read as many samples from the FIFO buffer as required to fill the frame.
    // The samples are stored in the frame temporarily.

    ret = av_audio_fifo_read(m_audio_fifo, reinterpret_cast<void **>(output_frame->data), frame_size);
    if (ret < frame_size)
    {
        if (ret < 0)
        {
            Logging::error(destname(), "Could not read data from FIFO (error '%1').", ffmpeg_geterror(ret).c_str());
        }
        else
        {
            Logging::error(destname(), "Could not read data from FIFO.");
            ret = AVERROR_EXIT;
        }
        av_frame_free(&output_frame);
        return ret;
    }

    // Build correct PTS
    if (output_frame->sample_rate)
    {
        /*
        * FFmpeg API docs say:
        *
        * best_effort_timestamp: frame timestamp estimated using various heuristics, in stream time base.
        * Unused for encoding, but we set it anyways.
        *
        * pts: Presentation timestamp, they say in ffmpeg time_base units, but nevertheless it seems to be
        * in stream time base. When not converted the audio comes out wobbly.
        */

#if LAVF_DEP_AVSTREAM_CODEC
        // Not sure if this is use anywhere, but let's set it anyway.
        output_frame->best_effort_timestamp = av_rescale_q(m_out.m_audio_pts, m_out.m_audio.m_stream->time_base, m_in.m_audio.m_stream->time_base);
#endif
        output_frame->pts = m_out.m_audio_pts;

        // duration = `a * b / c` = AV_TIME_BASE * output_frame->nb_samples / output_frame->sample_rate;
        int64_t duration = av_rescale(AV_TIME_BASE, output_frame->nb_samples, output_frame->sample_rate);

        duration = av_rescale_q(duration, av_get_time_base_q(), m_out.m_audio.m_stream->time_base);

        m_out.m_audio_pts += duration;
    }

    m_audio_frame_fifo.push(output_frame);

    return ret;
}

int FFmpeg_Transcoder::write_output_file_trailer()
{
    int ret;

    ret = av_write_trailer(m_out.m_format_ctx);
    if (ret < 0)
    {
        Logging::error(destname(), "Could not write output file trailer (error '%1').", ffmpeg_geterror(ret).c_str());
        return ret;
    }

    return 0;
}

time_t FFmpeg_Transcoder::mtime() const
{
    return m_mtime;
}

template <size_t size>
const char * FFmpeg_Transcoder::tagcpy(char (&out) [ size ], const std::string & in) const
{
    memset(out, ' ', size);
    memcpy(out, in.c_str(), std::min(size, in.size()));
    return out;
}

void FFmpeg_Transcoder::copy_metadata(AVDictionary **metadata_out, const AVDictionary *metadata_in, bool contentstream)
{
    AVDictionaryEntry *tag = nullptr;

    while ((tag = av_dict_get(metadata_in, "", tag, AV_DICT_IGNORE_SUFFIX)) != NULL)
    {
        std::string value(tag->value);

        if (contentstream && m_virtualfile != nullptr && m_virtualfile->m_flags & VIRTUALFLAG_CUESHEET)
        {
            // Replace tags with cue sheet values
            if (!strcasecmp(tag->key, "ARTIST"))
            {
                value = m_virtualfile->m_cuesheet.m_artist;
            }
            else if (!strcasecmp(tag->key, "TITLE"))
            {
                value = m_virtualfile->m_cuesheet.m_title;
            }
            else if (!strcasecmp(tag->key, "TRACK"))
            {
                char buf[20];
                sprintf(buf, "%i", m_virtualfile->m_cuesheet.m_trackno);
                value = buf;
            }
        }

        dict_set_with_check(metadata_out, tag->key, value.c_str(), 0, destname());

        if (contentstream && m_out.m_filetype == FILETYPE_MP3)
        {
            // For MP3 fill in ID3v1 structure
            if (!strcasecmp(tag->key, "ARTIST"))
            {
                tagcpy(m_out.m_id3v1.m_artist, value);
            }
            else if (!strcasecmp(tag->key, "TITLE"))
            {
                tagcpy(m_out.m_id3v1.m_title, value);
            }
            else if (!strcasecmp(tag->key, "ALBUM"))
            {
                tagcpy(m_out.m_id3v1.m_album, value);
            }
            else if (!strcasecmp(tag->key, "COMMENT"))
            {
                tagcpy(m_out.m_id3v1.m_comment, value);
            }
            else if (!strcasecmp(tag->key, "YEAR") || !strcasecmp(tag->key, "DATE"))
            {
                tagcpy(m_out.m_id3v1.m_year, value);
            }
            else if (!strcasecmp(tag->key, "TRACK"))
            {
                m_out.m_id3v1.m_title_no = static_cast<char>(atoi(value.c_str()));
            }
        }
    }
}

int FFmpeg_Transcoder::process_metadata()
{
    Logging::trace(destname(), "Processing metadata.");

    if (m_in.m_audio.m_stream != nullptr && CODECPAR(m_in.m_audio.m_stream)->codec_id == AV_CODEC_ID_VORBIS)
    {
        // For some formats (namely ogg) FFmpeg returns the tags, odd enough, with streams...
        copy_metadata(&m_out.m_format_ctx->metadata, m_in.m_audio.m_stream->metadata);
    }

    copy_metadata(&m_out.m_format_ctx->metadata, m_in.m_format_ctx->metadata);

    if (m_out.m_audio.m_stream != nullptr && m_in.m_audio.m_stream != nullptr)
    {
        // Copy audio stream meta data
        copy_metadata(&m_out.m_audio.m_stream->metadata, m_in.m_audio.m_stream->metadata);
    }

    if (m_out.m_video.m_stream != nullptr && m_in.m_video.m_stream != nullptr)
    {
        // Copy video stream meta data
        copy_metadata(&m_out.m_video.m_stream->metadata, m_in.m_video.m_stream->metadata);
    }

    // Also copy album art meta tags
    for (size_t n = 0; n < m_in.m_album_art.size(); n++)
    {
        AVStream *input_stream = m_in.m_album_art.at(n).m_stream;
        AVStream *output_stream = m_out.m_album_art.at(n).m_stream;

        copy_metadata(&output_stream->metadata, input_stream->metadata, input_stream->index == m_in.m_audio.m_stream_idx || input_stream->index == m_in.m_video.m_stream_idx);
    }

    if (m_virtualfile != nullptr && m_virtualfile->m_flags & VIRTUALFLAG_CUESHEET)
    {
        dict_set_with_check(&m_out.m_format_ctx->metadata, "TRACKTOTAL", m_virtualfile->m_cuesheet.m_tracktotal, 0, destname());
        dict_set_with_check(&m_out.m_format_ctx->metadata, "TRACK", m_virtualfile->m_cuesheet.m_trackno, 0, destname(), true);
        dict_set_with_check(&m_out.m_format_ctx->metadata, "ARTIST", m_virtualfile->m_cuesheet.m_artist.c_str(), 0, destname(), true);
        if (av_dict_get(m_out.m_format_ctx->metadata, "ALBUM_ARTIST", nullptr, 0) == nullptr)
        {
            // Issue #78: duplicate ARTIST tag to ALBUM_ARTIST, if target is empty.
            dict_set_with_check(&m_out.m_format_ctx->metadata, "ALBUM_ARTIST", m_virtualfile->m_cuesheet.m_artist.c_str(), 0, destname(), true);
        }
        dict_set_with_check(&m_out.m_format_ctx->metadata, "TITLE", m_virtualfile->m_cuesheet.m_title.c_str(), 0, destname(), true);
        dict_set_with_check(&m_out.m_format_ctx->metadata, "ALBUM", m_virtualfile->m_cuesheet.m_album.c_str(), 0, destname(), true);
        dict_set_with_check(&m_out.m_format_ctx->metadata, "GENRE", m_virtualfile->m_cuesheet.m_genre.c_str(), 0, destname(), true);
        dict_set_with_check(&m_out.m_format_ctx->metadata, "DATE", m_virtualfile->m_cuesheet.m_date.c_str(), 0, destname(), true);
    }

    return 0;
}

int FFmpeg_Transcoder::process_albumarts()
{
    int ret = 0;

    for (size_t n = 0; n < m_in.m_album_art.size(); n++)
    {
        AVStream *input_stream = m_in.m_album_art.at(n).m_stream;

        if (input_stream->disposition & AV_DISPOSITION_ATTACHED_PIC)
        {
            AVStream *output_stream = m_out.m_album_art.at(n).m_stream;

            ret = add_albumart_frame(output_stream, &input_stream->attached_pic);
            if (ret < 0)
            {
                break;
            }
        }
    }

    return ret;
}

void FFmpeg_Transcoder::flush_buffers()
{
    if (m_in.m_audio.m_codec_ctx != nullptr)
    {
        avcodec_flush_buffers(m_in.m_audio.m_codec_ctx);
    }
    if (m_in.m_video.m_codec_ctx != nullptr)
    {
        avcodec_flush_buffers(m_in.m_video.m_codec_ctx);
    }
}

int FFmpeg_Transcoder::do_seek_frame(uint32_t frame_no)
{
    m_have_seeked           = true;     // Note that we have seeked, thus skipped frames. We need to start transcoding over to fill any gaps.

    //m_skip_next_frame = true; /**< @todo Take deinterlace into account. If deinterlace is on the frame number is decreased by one. */

    if (m_skip_next_frame)
    {
        --frame_no;
    }

    int64_t pts = frame_to_pts(m_in.m_video.m_stream, frame_no);

    if (m_in.m_video.m_stream->start_time != AV_NOPTS_VALUE)
    {
        pts += m_in.m_video.m_stream->start_time;
    }

    return av_seek_frame(m_in.m_format_ctx, m_in.m_video.m_stream_idx, pts, AVSEEK_FLAG_BACKWARD);
}

int FFmpeg_Transcoder::skip_decoded_frames(uint32_t frame_no, bool forced_seek)
{
    int ret = 0;
    uint32_t next_frame_no = frame_no;
    // Seek next undecoded frame
    for (; m_buffer->have_frame(next_frame_no); next_frame_no++)
    {
        sleep(0);
    }

    if (next_frame_no > m_virtualfile->m_video_frame_count)
    {
        // Reached end of file
        // Set PTS to end of file
        m_out.m_video_pts = m_in.m_video.m_stream->duration;
        if (m_in.m_video.m_stream->start_time != AV_NOPTS_VALUE)
        {
            m_out.m_video_pts += m_in.m_video.m_stream->start_time;
        }
        // Seek to end of file to force AVERROR_EOF from next av_read_frame() call.
        ret = av_seek_frame(m_in.m_format_ctx, m_in.m_video.m_stream_idx, m_out.m_video_pts, AVSEEK_FLAG_ANY);
        return 0;
    }

    uint32_t last_frame_no = pts_to_frame(m_in.m_video.m_stream, m_out.m_video_pts);

    // Ignore seek if target is within the next FRAME_SEEK_THRESHOLD frames
    if (next_frame_no >= last_frame_no /*+ 1*/ && next_frame_no <= last_frame_no + FRAME_SEEK_THRESHOLD)
    {
        return 0;
    }

    if (forced_seek || (frame_no != next_frame_no && next_frame_no > 1))
    {
        // If frame changed, skip to it
        ret = do_seek_frame(next_frame_no);

        if (ret < 0)
        {
            Logging::error(destname(), "Could not encode audio frame: Seek to frame #%1 failed (error '%2').", next_frame_no, ffmpeg_geterror(ret).c_str());
        }
    }

    return ret;
}

int FFmpeg_Transcoder::flush_delayed_audio()
{
    int ret = 0;

    if (m_out.m_audio.m_codec_ctx != nullptr)
    {
        // Flush the encoder as it may have delayed frames.
        int data_written = 0;
        do
        {
            ret = encode_audio_frame(nullptr, &data_written);
#if LAVC_NEW_PACKET_INTERFACE
            if (ret == AVERROR_EOF)
            {
                // Not an error
                break;
            }

            if (ret < 0 && ret != AVERROR(EAGAIN))
            {
                Logging::error(destname(), "Could not encode audio frame (error '%1').", ffmpeg_geterror(ret).c_str());
                throw ret;
            }
#else
            if (ret < 0)
            {
                Logging::error(destname(), "Could not encode audio frame (error '%1').", ffmpeg_geterror(ret).c_str());
                throw ret;
            }
#endif
        }
        while (data_written);
    }

    return ret;
}

int FFmpeg_Transcoder::flush_delayed_video()
{
    int ret = 0;

    if (m_out.m_video.m_codec_ctx != nullptr)
    {
        // Flush the encoder as it may have delayed frames.
        int data_written = 0;
        do
        {
            if (!is_frameset())
            {
                // Encode regular frame
                ret = encode_video_frame(nullptr, &data_written);
            }
            else
            {
                // Encode seperate image frame
                ret = encode_image_frame(nullptr, &data_written);
            }
#if LAVC_NEW_PACKET_INTERFACE
            if (ret == AVERROR_EOF)
            {
                // Not an error
                break;
            }
            if (ret < 0 && ret != AVERROR(EAGAIN))
            {
                Logging::error(destname(), "Could not encode video frame (error '%1').", ffmpeg_geterror(ret).c_str());
                throw ret;
            }
#else
            if (ret < 0)
            {
                throw ret;
            }
#endif
        }
        while (data_written);
    }

    return ret;
}

int FFmpeg_Transcoder::process_single_fr(int &status)
{
    int finished = 0;
    int ret = 0;

    status = 0;

    try
    {
        if (m_in.m_video.m_stream != nullptr && is_frameset())
        {
            // Direct access handling for frame sets: seek to frame if requested.
            if (!m_last_seek_frame_no)
            {
                // No current seek frame, check if new seek frame was stacked.
                {
                    std::lock_guard<std::recursive_mutex> lck (m_seek_to_fifo_mutex);

                    while (!m_seek_to_fifo.empty())
                    {
                        uint32_t frame_no = m_seek_to_fifo.front();
                        m_seek_to_fifo.pop();

                        if (!m_buffer->have_frame(frame_no))
                        {
                            // Frame not yet decoded, so skip to it.
                            m_last_seek_frame_no = frame_no;
                            break;
                        }
                    }
                }

                if (m_last_seek_frame_no)
                {
                    // The first frame that FFmpeg API returns after av_seek_frame is wrong (the last frame before seek).
                    // We are unable to detect that because the pts seems correct (the one that we requested).
                    // So we position before the frame requested, and simply throw the first away.

                    //#define PRESCAN_FRAMES  3
#ifdef PRESCAN_FRAMES
                    int64_t seek_frame_no = m_last_seek_frame_noX;
                    if (seek_frame_no > PRESCAN_FRAMES)
                    {
                        seek_frame_no -= PRESCAN_FRAMES;
                        //m_skip_next_frame = true; /**< @todo Take deinterlace into account */
                    }
                    else
                    {
                        seek_frame_no = 1;
                    }

                    ret = skip_decoded_frames(seek_frame_no, true);
#else
                    ret = skip_decoded_frames(m_last_seek_frame_no, true);
#endif

                    if (ret == AVERROR_EOF)
                    {
                        status = 1;
                        return 0;
                    }

                    if (ret < 0)
                    {
                        throw ret;
                    }
                }
            }
        }

        if (!m_copy_audio && m_out.m_audio.m_stream_idx != INVALID_STREAM)
        {
            int output_frame_size;

            if (m_out.m_audio.m_codec_ctx->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE)
            {
                // Encode supports variable frame size, use an arbitrary value
                output_frame_size =  10000;
            }
            else
            {
                // Use the encoder's desired frame size for processing.
                output_frame_size = m_out.m_audio.m_codec_ctx->frame_size;
            }

            // Make sure that there is one frame worth of samples in the FIFO
            // buffer so that the encoder can do its work.
            // Since the decoder's and the encoder's frame size may differ, we
            // need to FIFO buffer to store as many frames worth of input samples
            // that they make up at least one frame worth of output samples.

            while (av_audio_fifo_size(m_audio_fifo) < output_frame_size)
            {
                // Decode one frame worth of audio samples, convert it to the
                // output sample format and put it into the FIFO buffer.

                ret = read_decode_convert_and_store(&finished);
                if (ret < 0)
                {
                    throw ret;
                }

                // If we are at the end of the input file, we continue
                // encoding the remaining audio samples to the output file.

                if (finished)
                {
                    break;
                }
            }

            // If we have enough samples for the encoder, we encode them.
            // At the end of the file, we pass the remaining samples to
            // the encoder.

            while (av_audio_fifo_size(m_audio_fifo) >= output_frame_size || (finished && av_audio_fifo_size(m_audio_fifo) > 0))
            {
                // Take one frame worth of audio samples from the FIFO buffer,
                // create a frame and store in audio frame FIFO.

                ret = create_audio_frame(output_frame_size);
                if (ret < 0)
                {
                    throw ret;
                }
            }

            // Read audio frame FIFO, encode and store
            while (!m_audio_frame_fifo.empty())
            {
                AVFrame *audio_frame = m_audio_frame_fifo.front();
                m_audio_frame_fifo.pop();

                int data_written;
                // Encode one frame worth of audio samples.
                ret = encode_audio_frame(audio_frame, &data_written);

                av_frame_free(&audio_frame);

#if !LAVC_NEW_PACKET_INTERFACE
                if (ret < 0)
#else
                if (ret < 0 && ret != AVERROR(EAGAIN))
#endif
                {
                    throw ret;
                }
            }

            // If we are at the end of the input file and have encoded
            // all remaining samples, we can exit this loop and finish.

            if (finished)
            {
                flush_delayed_audio();

                status = 1;
            }
        }
        else
        {
            // If we have no audio stream, we'll only get video data
            // or we simply copy audio and/or video frames into the packet queue
            ret = read_decode_convert_and_store(&finished);
            if (ret < 0)
            {
                throw ret;
            }

            if (finished)
            {
                status = 1;
            }
        }

        if (!m_copy_video)
        {
            while (!m_video_frame_fifo.empty())
            {
                AVFrame *video_frame = m_video_frame_fifo.front();
                m_video_frame_fifo.pop();

                // Encode one video frame.
                int data_written = 0;
                video_frame->key_frame = 0;    // Leave that decision to encoder
                video_frame->pict_type = AV_PICTURE_TYPE_NONE;

                if (!is_frameset())
                {
                    ret = encode_video_frame(video_frame, &data_written);
                }
                else
                {
                    ret = encode_image_frame(video_frame, &data_written);
                }

                av_frame_free(&video_frame);

#if !LAVC_NEW_PACKET_INTERFACE
                if (ret < 0)
#else
                if (ret < 0 && ret != AVERROR(EAGAIN))
#endif
                {
                    throw ret;
                }
            }

#if LAVC_NEW_PACKET_INTERFACE
            ret = 0;    // May be AVERROR(EAGAIN)
#endif

            // If we are at the end of the input file and have encoded
            // all remaining samples, we can exit this loop and finish.

            if (finished)
            {
                flush_delayed_video();

                status = 1;
            }
        }

        if (is_hls())
        {
            uint32_t next_segment;
            if (m_active_stream_msk == m_inhibit_stream_msk)
            {
                bool opened = false;

#ifdef USE_INTERLEAVED_WRITE
		    // Flush interleaved frame queue into old stream
		    ret = av_interleaved_write_frame(m_out.m_format_ctx, nullptr);

		    if (ret < 0)
		    {
		        Logging::error(destname(), "Could not flush frame queue (error '%2').", ffmpeg_geterror(ret).c_str());
		    }
#endif  // USE_INTERLEAVED_WRITE

                encode_finish();

                // Go to next requested segment...
                next_segment = m_current_segment + 1;

                // ...or process any stacked seek requests.
                while (!m_seek_to_fifo.empty())
                {
                    uint32_t segment_no = m_seek_to_fifo.front();
                    m_seek_to_fifo.pop();

                    if (!m_buffer->segment_exists(segment_no) || !m_buffer->tell(segment_no)) // NOT EXISTS or NO DATA YET
                    {
                        m_reset_pts    = FFMPEGFS_AUDIO | FFMPEGFS_VIDEO;   // Note that we have to reset audio/video pts to the new position
                        m_have_seeked   = true;                             // Note that we have seeked, thus skipped frames. We need to start transcoding over to fill any gaps.

                        Logging::info(destname(), "Performing seek request to HLS segment no. %1.", segment_no);

                        int64_t pos = (segment_no - 1) * params.m_segment_duration;

                        if (m_in.m_video.m_stream_idx && m_out.m_video.m_stream_idx != INVALID_STREAM)
                        {
                            int64_t pts = av_rescale_q(pos, av_get_time_base_q(), m_in.m_video.m_stream->time_base);

                            if (m_in.m_video.m_stream->start_time != AV_NOPTS_VALUE)
                            {
                                pts += m_in.m_video.m_stream->start_time;
                            }

                            ret = av_seek_frame(m_in.m_format_ctx, m_in.m_video.m_stream_idx, pts, AVSEEK_FLAG_BACKWARD);
                        }
                        else // if (m_out.m_audio.m_stream_idx != INVALID_STREAM)
                        {
                            int64_t pts = av_rescale_q(pos, av_get_time_base_q(), m_in.m_audio.m_stream->time_base);

                            if (m_in.m_audio.m_stream->start_time != AV_NOPTS_VALUE)
                            {
                                pts += m_in.m_audio.m_stream->start_time;
                            }

                            ret = av_seek_frame(m_in.m_format_ctx, m_in.m_audio.m_stream_idx, pts, AVSEEK_FLAG_BACKWARD);
                        }

                        if (ret < 0)
                        {
                            Logging::error(destname(), "Seek failed on input file (error '%1').", ffmpeg_geterror(ret).c_str());
                            throw ret;
                        }

                        flush_buffers();

                        close_output_file();

                        purge_hls_fifo();   // We do not need the packets for the next frame, we start a new one at another position!

                        ret = open_output(m_buffer);
                        if (ret < 0)
                        {
                            throw ret;
                        }

                        next_segment = segment_no;

                        opened = true;

                        break;
                    }

                    Logging::info(destname(), "Discarded seek request to HLS segment no. %1.", segment_no);
                }

                // Set current segment
                m_current_segment       = next_segment;

                m_inhibit_stream_msk = 0;

                Logging::info(destname(), "Starting HLS segment no. %1.", m_current_segment);

                if (!m_buffer->set_segment(m_current_segment))
                {
                    throw AVERROR(errno);
                }

                if (!opened)
                {
                    // Process output file, already done by open_output() if file has been newly opened.
                    ret = process_output();
                    if (ret)
                    {
                        throw ret;
                    }
                }

                if (is_hls())
                {
                    while (!m_hls_packet_fifo.empty())
                    {
                        AVPacket *pkt = m_hls_packet_fifo.front();
                        m_hls_packet_fifo.pop();

#ifdef USE_INTERLEAVED_WRITE
                        ret = av_interleaved_write_frame(m_out.m_format_ctx, pkt);
#else   // !USE_INTERLEAVED_WRITE
                        ret = av_write_frame(m_out.m_format_ctx, pkt);
#endif  // !USE_INTERLEAVED_WRITE

                        if (ret < 0)
                        {
                            Logging::error(destname(), "Could not write frame (error '%2').", ffmpeg_geterror(ret).c_str());
                        }

                        av_packet_unref(pkt);
                    }
                }
            }
        }
    }
    catch (int _ret)
    {
        status = (_ret != AVERROR_EOF ? -1 : 1);   // If _ret == AVERROR_EOF, simply signal EOF
        return _ret;
    }

    return 0;
}

BITRATE FFmpeg_Transcoder::get_prores_bitrate(int width, int height, const AVRational &framerate, int interleaved, int profile)
{
    unsigned int mindist;
    int match = -1;

    // Find best match resolution
    mindist = UINT_MAX;
    for (int i = 0; m_prores_bitrate[i].m_width; i++)
    {
        unsigned int x = static_cast<unsigned int>(width - m_prores_bitrate[i].m_width);
        unsigned int y = static_cast<unsigned int>(height - m_prores_bitrate[i].m_height);
        unsigned int dist = (x * x) + (y * y);

        if (dist < mindist)
        {
            mindist = dist;
            match = i;
        }

        if (!dist)
        {
            // Exact match, won't find a better one.
            break;
        }
    }

    width   = m_prores_bitrate[match].m_width;
    height  = m_prores_bitrate[match].m_height;

    // Find best match framerate
    double framerateX = av_q2d(framerate);
    mindist = UINT_MAX;
    for (int i = match; width == m_prores_bitrate[i].m_width && height == m_prores_bitrate[i].m_height; i++)
    {
        unsigned int dist = UINT_MAX;
        for (int j = 0; j < MAX_PRORES_FRAMERATE && m_prores_bitrate[i].m_framerate[j].m_framerate; j++)
        {
            unsigned int x = static_cast<unsigned int>(framerateX - m_prores_bitrate[i].m_framerate[j].m_framerate);
            unsigned int y = static_cast<unsigned int>(interleaved - m_prores_bitrate[i].m_framerate[j].m_interleaved);

            dist = (x * x) + (y * y);

            if (dist < mindist)
            {
                mindist = dist;
                match = i;
            }

            if (!dist)
            {
                // Exact match, won't find a better one.
                break;
            }
        }

        if (!dist)
        {
            // Exact match, won't find a better one.
            break;
        }
    }

    if (match < 0)
    {
        return 0;
    }

    return m_prores_bitrate[match].m_bitrate[profile] * (1000 * 1000);
}

bool FFmpeg_Transcoder::audio_size(size_t *filesize, AVCodecID codec_id, BITRATE bit_rate, int64_t duration, int channels, int sample_rate)
{
    BITRATE output_audio_bit_rate;
    int output_sample_rate;
    bool success = true;

    get_output_bit_rate(bit_rate, params.m_audiobitrate, &output_audio_bit_rate);
    get_output_sample_rate(sample_rate, params.m_audiosamplerate, &output_sample_rate);

    switch (codec_id)
    {
    case AV_CODEC_ID_AAC:
    {
        // Try to predict the size of the AAC stream (this is fairly accurate, sometimes a bit larger, sometimes a bit too small
        *filesize += static_cast<size_t>(duration * output_audio_bit_rate / (8LL * AV_TIME_BASE));
        *filesize = static_cast<size_t>(1025 * (*filesize) / 1000); // add overhead
        break;
    }
    case AV_CODEC_ID_MP3:
    {
        // Kbps = bits per second / 8 = Bytes per second x 60 seconds = Bytes per minute x 60 minutes = Bytes per hour
        // This is the sum of the size of
        // ID3v2, ID3v1, and raw MP3 data. This is theoretically only approximate
        // but in practice gives excellent answers, usually exactly correct.
        // Cast to 64-bit int to avoid overflow.

        *filesize += static_cast<size_t>(duration * output_audio_bit_rate / (8LL * AV_TIME_BASE)) + ID3V1_TAG_LENGTH;
        break;
    }
    case AV_CODEC_ID_PCM_S16LE:
    case AV_CODEC_ID_PCM_S16BE:
    {
        // bits_per_sample = av_get_bits_per_sample(ctx->codec_id);
        // bit_rate = bits_per_sample ? ctx->sample_rate * (int64_t)ctx->channels * bits_per_sample : ctx->bit_rate;

        int bytes_per_sample    = av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);

        // File size:
        // file duration * sample rate (HZ) * channels * bytes per sample
        // + WAV_HEADER + DATA_HEADER + (with FFMpeg always) LIST_HEADER
        // The real size of the list header is unkown as we don't know the contents (meta tags)
        *filesize += static_cast<size_t>(duration * sample_rate * (channels >= 2 ? 2 : 1) * bytes_per_sample / AV_TIME_BASE) + sizeof(WAV_HEADER) + sizeof(WAV_LIST_HEADER) + sizeof(WAV_DATA_HEADER);
        break;
    }
    case AV_CODEC_ID_VORBIS:
    {
        // Kbps = bits per second / 8 = Bytes per second x 60 seconds = Bytes per minute x 60 minutes = Bytes per hour
        *filesize += static_cast<size_t>(duration * output_audio_bit_rate / (8LL * AV_TIME_BASE));
        *filesize = static_cast<size_t>(1025 * (*filesize) / 1000); // add overhead
        break;
    }
    case AV_CODEC_ID_OPUS:
    {
        // Kbps = bits per second / 8 = Bytes per second x 60 seconds = Bytes per minute x 60 minutes = Bytes per hour
        *filesize += static_cast<size_t>(duration * output_audio_bit_rate / (8LL * AV_TIME_BASE));
        *filesize = static_cast<size_t>(1150 * (*filesize) / 1000); // add overhead
        break;
    }
    case AV_CODEC_ID_ALAC:
    {
        int bytes_per_sample    = av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);

        // File size:
        // Apple Lossless Audio Coding promises a compression rate of 60-70%. We estimate 65 % of the original WAV size.
        *filesize += static_cast<size_t>(duration * sample_rate * (channels > 2 ? 2 : 1) * bytes_per_sample / AV_TIME_BASE) * 100 / 65;
        break;
    }
    case AV_CODEC_ID_AC3:
    {
        // Kbps = bits per second / 8 = Bytes per second x 60 seconds = Bytes per minute x 60 minutes = Bytes per hour
        *filesize += static_cast<size_t>(duration * output_audio_bit_rate / (8LL * AV_TIME_BASE));
        *filesize = static_cast<size_t>(1150 * (*filesize) / 1000); // add overhead
        break;
    }
    case AV_CODEC_ID_NONE:
    {
        break;
    }
    default:
    {
        success = false;
        break;
    }
    }
    return success;
}

bool FFmpeg_Transcoder::video_size(size_t *filesize, AVCodecID codec_id, BITRATE bit_rate, int64_t duration, int width, int height, int interleaved, const AVRational &framerate)
{
    BITRATE out_video_bit_rate;
    bool success = true;

    get_output_bit_rate(bit_rate, params.m_videobitrate, &out_video_bit_rate);

    switch (codec_id)
    {
    case AV_CODEC_ID_H264:
    {
        *filesize += static_cast<size_t>(duration * out_video_bit_rate / (8LL * AV_TIME_BASE));
        *filesize = static_cast<size_t>(1100 * (*filesize) / 1000); // add overhead
        break;
    }
    case AV_CODEC_ID_THEORA:
    {
        *filesize += static_cast<size_t>(duration * out_video_bit_rate / (8LL * AV_TIME_BASE));
        *filesize = static_cast<size_t>(1025 * (*filesize) / 1000); // add overhead
        break;
    }
    case AV_CODEC_ID_VP9:
    {
        *filesize += static_cast<size_t>(duration * out_video_bit_rate / (8LL * AV_TIME_BASE));
        *filesize = static_cast<size_t>(1150 * (*filesize) / 1000); // add overhead
        break;
    }
    case AV_CODEC_ID_PRORES:
    {
        *filesize += static_cast<size_t>(duration * get_prores_bitrate(width, height, framerate, interleaved, params.m_level) / (8LL * AV_TIME_BASE));
        break;
    }
    case AV_CODEC_ID_PNG:
    case AV_CODEC_ID_BMP:
    case AV_CODEC_ID_MJPEG:
    {
        *filesize += static_cast<size_t>(width * height * 24 / 8);   // Get the max. size
        break;
    }
    case AV_CODEC_ID_NONE:
    {
        break;
    }
    default:
    {
        success = false;
        break;
    }
    }
    return success;
}

size_t FFmpeg_Transcoder::calculate_predicted_filesize() const
{
    if (m_in.m_format_ctx == nullptr)
    {
        return 0;
    }

    if (m_current_format == nullptr)
    {
        // Should ever happen, but better check this to avoid crashes.
        return 0;
    }

    size_t filesize = 0;

    int64_t duration = m_in.m_format_ctx->duration != AV_NOPTS_VALUE ? m_in.m_format_ctx->duration : 0;
    BITRATE input_audio_bit_rate = 0;
    int input_sample_rate = 0;
    BITRATE input_video_bit_rate = 0;

    if (m_fileio->duration() != AV_NOPTS_VALUE)
    {
        duration = m_fileio->duration();
    }

    if (m_in.m_audio.m_stream_idx != INVALID_STREAM)
    {
        input_sample_rate = CODECPAR(m_in.m_audio.m_stream)->sample_rate;
        input_audio_bit_rate = (CODECPAR(m_in.m_audio.m_stream)->bit_rate != 0) ? CODECPAR(m_in.m_audio.m_stream)->bit_rate : m_in.m_format_ctx->bit_rate;
    }

    if (m_in.m_video.m_stream_idx != INVALID_STREAM)
    {
        input_video_bit_rate = (CODECPAR(m_in.m_video.m_stream)->bit_rate != 0) ? CODECPAR(m_in.m_video.m_stream)->bit_rate : m_in.m_format_ctx->bit_rate;
    }

    if (input_audio_bit_rate)
    {
        int channels = m_in.m_audio.m_codec_ctx->channels;

        if (!audio_size(&filesize, m_current_format->audio_codec_id(), input_audio_bit_rate, duration, channels, input_sample_rate))
        {
            Logging::warning(filename(), "Unsupported audio codec '%1' for format %2.", get_codec_name(m_current_format->audio_codec_id(), 0), m_current_format->desttype().c_str());
        }
    }

    if (input_video_bit_rate)
    {
        if (m_is_video)
        {
            int width = CODECPAR(m_in.m_video.m_stream)->width;
            int height = CODECPAR(m_in.m_video.m_stream)->height;
            int interleaved = params.m_deinterlace ? 0 : (CODECPAR(m_in.m_video.m_stream)->field_order != AV_FIELD_PROGRESSIVE);    // Deinterlace only if source is interlaced
#if LAVF_DEP_AVSTREAM_CODEC
            AVRational framerate = m_in.m_video.m_stream->avg_frame_rate;
#else
            AVRational framerate = m_in.m_video.m_stream->codec->framerate;
#endif
            if (!video_size(&filesize, m_current_format->video_codec_id(), input_video_bit_rate, duration, width, height, interleaved, framerate))
            {
                Logging::warning(filename(), "Unsupported video codec '%1' for format %2.", get_codec_name(m_current_format->video_codec_id(), 0), m_current_format->desttype().c_str());
            }
        }
        // else      /** @todo Feature #2260: Add picture size */
        // {
        // }
    }

    /*
    // Support #2654: Test Code
    // add total overhead
    switch (m_current_format->filetype())
    {
    case FILETYPE_MP3:
    case FILETYPE_MP4:
    case FILETYPE_WAV:
    case FILETYPE_OGG:
    case FILETYPE_WEBM:
    case FILETYPE_MOV:
    case FILETYPE_AIFF:
    case FILETYPE_OPUS:
    case FILETYPE_PRORES:
    case FILETYPE_ALAC:
    case FILETYPE_PNG:
    case FILETYPE_JPG:
    case FILETYPE_BMP:
    {
        break;
    }
    case FILETYPE_TS:
    case FILETYPE_HLS:
    {
        filesize = static_cast<size_t>(1280 * (filesize) / 1000);
        break;
    }
    case FILETYPE_UNKNOWN:
    {
        break;
    }
    }
*/

    return filesize;
}

int64_t FFmpeg_Transcoder::duration()
{
    if (m_virtualfile != nullptr)
    {
        return m_virtualfile->m_duration;
    }
    else
    {
        return 0;
    }
}

size_t FFmpeg_Transcoder::predicted_filesize()
{
    if (m_virtualfile != nullptr)
    {
        return m_virtualfile->m_predicted_size;
    }
    else
    {
        return 0;
    }
}

uint32_t FFmpeg_Transcoder::video_frame_count() const
{
    if (m_virtualfile != nullptr)
    {
        return m_virtualfile->m_video_frame_count;
    }
    else
    {
        return 0;
    }
}

uint32_t FFmpeg_Transcoder::segment_count() const
{
    if (m_virtualfile != nullptr)
    {
        return m_virtualfile->get_segment_count();
    }
    else
    {
        return 0;
    }
}

int FFmpeg_Transcoder::encode_finish()
{
    int ret = 0;

    if (!is_frameset())
    {
        // If not a frame set, write trailer

        // Write the trailer of the output file container.
        ret = write_output_file_trailer();
    }

    if (is_hls())
    {
        m_buffer->finished_segment();

        // Get segment VIRTUALFILE object
        std::string filename(m_buffer->virtualfile()->m_destfile + "/" + make_filename(m_current_segment, params.current_format(m_buffer->virtualfile())->fileext()));
        LPVIRTUALFILE virtualfile = find_file(filename.c_str());

        if (virtualfile != nullptr)
        {
            virtualfile->m_predicted_size   = m_buffer->buffer_watermark(m_current_segment);

            stat_set_size(&virtualfile->m_st, virtualfile->m_predicted_size);
        }
    }
    else //if (m_virtualfile->m_flags & VIRTUALFLAG_CUESHEET)
    {
        // Save actual result size of the file
        stat_set_size(&m_virtualfile->m_st, m_buffer->buffer_watermark());
    }

    return ret;
}

const ID3v1 * FFmpeg_Transcoder::id3v1tag() const
{
    return &m_out.m_id3v1;
}

int FFmpeg_Transcoder::input_read(void * opaque, unsigned char * data, int size)
{
    FileIO * io = static_cast<FileIO *>(opaque);

    if (io == nullptr)
    {
        Logging::error(nullptr, "input_read(): Internal error: FileIO is NULL!");
        return AVERROR(EINVAL);
    }

    if (io->eof())
    {
        // At EOF
        return AVERROR_EOF;
    }

    int read = static_cast<int>(io->read(reinterpret_cast<char *>(data), static_cast<size_t>(size)));

    if (read != size && io->error())
    {
        // Read failed
        return AVERROR(io->error());
    }

    return read;
}

int FFmpeg_Transcoder::output_write(void * opaque, unsigned char * data, int size)
{
    Buffer * buffer = static_cast<Buffer *>(opaque);

    if (buffer == nullptr)
    {
        Logging::error(nullptr, "input_write(): Internal error: FileIO is NULL!");
        return AVERROR(EINVAL);
    }

    int written = static_cast<int>(buffer->write(static_cast<const uint8_t*>(data), static_cast<size_t>(size)));
    if (written != size)
    {
        // Write error
        return (AVERROR(errno));
    }
    return written;
}

int64_t FFmpeg_Transcoder::seek(void * opaque, int64_t offset, int whence)
{
    FileIO * io = static_cast<FileIO *>(opaque);
    int64_t res_offset = 0;

    if (io == nullptr)
    {
        Logging::error(nullptr, "seek(): Internal error: FileIO is NULL!");
        return AVERROR(EINVAL);
    }

    if (whence & AVSEEK_SIZE)
    {
        // Return file size
        res_offset = static_cast<int64_t>(io->size());
    }
    else
    {
        whence &= ~(AVSEEK_SIZE | AVSEEK_FORCE);

        if (!io->seek(offset, whence))
        {
            // OK: Return position
            res_offset = offset;
        }
        else
        {
            // Error
            res_offset = AVERROR(errno);
        }
    }

    return res_offset;
}

bool FFmpeg_Transcoder::close_resample()
{
    if (m_audio_resample_ctx)
    {
#if LAVR_DEPRECATE
        swr_free(&m_audio_resample_ctx);
#else
        avresample_close(m_audio_resample_ctx);
        avresample_free(&m_audio_resample_ctx);
#endif
        m_audio_resample_ctx = nullptr;
        return true;
    }

    return false;
}

int FFmpeg_Transcoder::purge_audio_fifo()
{
    int audio_samples_left = 0;

    if (m_audio_fifo != nullptr)
    {
        audio_samples_left = av_audio_fifo_size(m_audio_fifo);
        av_audio_fifo_free(m_audio_fifo);
        m_audio_fifo = nullptr;
    }

    return audio_samples_left;
}

size_t FFmpeg_Transcoder::purge_audio_frame_fifo()
{
    size_t audio_frames_left  = m_audio_frame_fifo.size();

    while (m_audio_frame_fifo.size())
    {
        AVFrame *audio_frame = m_audio_frame_fifo.front();
        m_audio_frame_fifo.pop();

        av_frame_free(&audio_frame);
    }

    return audio_frames_left;
}

size_t FFmpeg_Transcoder::purge_video_frame_fifo()
{
    size_t video_frames_left = m_video_frame_fifo.size();

    while (m_video_frame_fifo.size())
    {
        AVFrame *video_frame = m_video_frame_fifo.front();
        m_video_frame_fifo.pop();

        av_frame_free(&video_frame);
    }

    return video_frames_left;
}

size_t FFmpeg_Transcoder::purge_hls_fifo()
{
    size_t hls_packets_left = m_hls_packet_fifo.size();

    while (!m_hls_packet_fifo.empty())
    {
        AVPacket *pkt = m_hls_packet_fifo.front();
        m_hls_packet_fifo.pop();

        av_packet_unref(pkt);
    }

    return hls_packets_left;
}

void FFmpeg_Transcoder::purge_fifos()
{
    std::string outfile;
    int audio_samples_left      = purge_audio_fifo();
    size_t audio_frames_left    = purge_audio_frame_fifo();
    size_t video_frames_left    = purge_video_frame_fifo();
    size_t hls_packets_left     = purge_hls_fifo();

    if (m_out.m_format_ctx != nullptr)
    {
#if LAVF_DEP_FILENAME
        if (m_out.m_format_ctx->url != nullptr)
        {
            outfile = m_out.m_format_ctx->url;
        }
#else
        // lavf 58.7.100 - avformat.h - deprecated
        outfile = m_out.m_format_ctx->filename;
#endif
    }

    const char *p = outfile.empty() ? nullptr : outfile.c_str();
    if (audio_samples_left)
    {
        Logging::warning(p, "%1 audio samples left in buffer and not written to target file!", audio_samples_left);
    }

    if (audio_frames_left)
    {
        Logging::warning(p, "%1 audio frames left in buffer and not written to target file!", audio_frames_left);
    }

    if (video_frames_left)
    {
        Logging::warning(p, "%1 video frames left in buffer and not written to target file!", video_frames_left);
    }

    if (hls_packets_left)
    {
        Logging::warning(p, "%1 hls packets left in buffer and not written to target file!", hls_packets_left);
    }
}

bool FFmpeg_Transcoder::close_output_file()
{
    bool closed = false;

    purge_fifos();

    if (close_resample())
    {
        closed = true;
    }

    if (m_sws_ctx != nullptr)
    {
        sws_freeContext(m_sws_ctx);
        m_sws_ctx = nullptr;
        closed = true;
    }

    // Close output file
#if !LAVF_DEP_AVSTREAM_CODEC
    if (m_out.m_audio.m_codec_ctx != nullptr)
    {
        avcodec_close(m_out.m_audio.m_codec_ctx);
        m_out.m_audio.m_codec_ctx = nullptr;
        closed = true;
    }

    if (m_out.m_video.m_codec_ctx != nullptr)
    {
        avcodec_close(m_out.m_video.m_codec_ctx);
        m_out.m_video.m_codec_ctx = nullptr;
        closed = true;
    }
#else
    if (m_out.m_audio.m_codec_ctx != nullptr)
    {
        avcodec_free_context(&m_out.m_audio.m_codec_ctx);
        m_out.m_audio.m_codec_ctx = nullptr;
        closed = true;
    }

    if (m_out.m_video.m_codec_ctx != nullptr)
    {
        avcodec_free_context(&m_out.m_video.m_codec_ctx);
        m_out.m_video.m_codec_ctx = nullptr;
        closed = true;
    }
#endif

    while (m_out.m_album_art.size())
    {
        AVCodecContext *codec_ctx = m_out.m_album_art.back().m_codec_ctx;
        m_out.m_album_art.pop_back();
        if (codec_ctx != nullptr)
        {
#if !LAVF_DEP_AVSTREAM_CODEC
            avcodec_close(codec_ctx);
#else
            avcodec_free_context(&codec_ctx);
#endif
            closed = true;
        }
    }

    if (m_out.m_format_ctx != nullptr)
    {
        if (m_out.m_format_ctx->pb != nullptr)
        {
            // 2017-09-01 - xxxxxxx - lavf 57.80.100 / 57.11.0 - avio.h
            //  Add avio_context_free(). From now on it must be used for freeing AVIOContext.
#if (LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(57, 80, 0))
            av_freep(&m_out.m_format_ctx->pb->buffer);
            avio_context_free(&m_out.m_format_ctx->pb);
#else
            av_freep(m_out.m_format_ctx->pb);
#endif
            m_out.m_format_ctx->pb = nullptr;
        }

        avformat_free_context(m_out.m_format_ctx);

        m_out.m_format_ctx = nullptr;
        closed = true;
    }

    return closed;
}

bool FFmpeg_Transcoder::close_input_file()
{
    bool closed = false;

#if !LAVF_DEP_AVSTREAM_CODEC
    if (m_in.m_audio.m_codec_ctx != nullptr)
    {
        avcodec_close(m_in.m_audio.m_codec_ctx);
        m_in.m_audio.m_codec_ctx = nullptr;
        closed = true;
    }

    if (m_in.m_video.m_codec_ctx != nullptr)
    {
        avcodec_close(m_in.m_video.m_codec_ctx);
        m_in.m_video.m_codec_ctx = nullptr;
        closed = true;
    }
#else
    if (m_in.m_audio.m_codec_ctx != nullptr)
    {
        avcodec_free_context(&m_in.m_audio.m_codec_ctx);
        m_in.m_audio.m_codec_ctx = nullptr;
        closed = true;
    }

    if (m_in.m_video.m_codec_ctx != nullptr)
    {
        avcodec_free_context(&m_in.m_video.m_codec_ctx);
        m_in.m_video.m_codec_ctx = nullptr;
        closed = true;
    }
#endif

    while (m_in.m_album_art.size())
    {
        AVCodecContext *codec_ctx = m_in.m_album_art.back().m_codec_ctx;
        m_in.m_album_art.pop_back();
        if (codec_ctx != nullptr)
        {
#if !LAVF_DEP_AVSTREAM_CODEC
            avcodec_close(codec_ctx);
#else
            avcodec_free_context(&codec_ctx);
#endif
            closed = true;
        }
    }

    if (m_in.m_format_ctx != nullptr)
    {
        //if (!(m_in.m_format_ctx->oformat->flags & AVFMT_NOFILE))
        {
            if (m_close_fileio && m_fileio != nullptr)
            {
                m_fileio->close();
                delete m_fileio;
                m_fileio = nullptr;
            }

            if (m_in.m_format_ctx->pb != nullptr)
            {
                // 2017-09-01 - xxxxxxx - lavf 57.80.100 / 57.11.0 - avio.h
                //  Add avio_context_free(). From now on it must be used for freeing AVIOContext.
#if (LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(57, 80, 0))
                avio_context_free(&m_in.m_format_ctx->pb);
#else
                av_freep(m_in.m_format_ctx->pb);
#endif
                m_in.m_format_ctx->pb = nullptr;
            }
        }

        avformat_close_input(&m_in.m_format_ctx);
        m_in.m_format_ctx = nullptr;
        closed = true;
    }

    free_filters();

    return closed;
}

void FFmpeg_Transcoder::close()
{
    bool closed = false;

    // Close input file
    closed |= close_input_file();

    // Close output file
    closed |= close_output_file();

    // Free hardware device contexts if open
    hwdevice_ctx_free(&m_hwaccel_dec_device_ctx);
    hwdevice_ctx_free(&m_hwaccel_enc_device_ctx);

    if (closed)
    {
        // Closed anything (anything had been open to be closed in the first place)...
        Logging::trace(nullptr, "FFmpeg transcoder closed.");
    }
}

const char *FFmpeg_Transcoder::filename() const
{
    return m_in.m_filename.c_str();
}

const char *FFmpeg_Transcoder::destname() const
{
    return m_out.m_filename.c_str();
}

// create
int FFmpeg_Transcoder::init_deinterlace_filters(AVCodecContext *codec_context, AVPixelFormat pix_fmt, const AVRational & avg_frame_rate, const AVRational & time_base)
{
    const char * filters;
    char args[1024];
    const AVFilter * buffer_src     = avfilter_get_by_name("buffer");
    const AVFilter * buffer_sink    = avfilter_get_by_name("buffersink");
    AVFilterInOut * outputs         = avfilter_inout_alloc();
    AVFilterInOut * inputs          = avfilter_inout_alloc();
    //enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE };
    int ret = 0;

    m_buffer_sink_context = nullptr;
    m_buffer_source_context = nullptr;
    m_filter_graph = nullptr;

    try
    {
        if (!avg_frame_rate.den && !avg_frame_rate.num)
        {
            // No framerate, so this video "stream" has only one picture
            throw static_cast<int>(AVERROR(EINVAL));
        }

        m_filter_graph = avfilter_graph_alloc();

        AVBufferSinkParams buffer_sink_params;
        enum AVPixelFormat pixel_fmts[3];

        if (outputs == nullptr || inputs == nullptr || m_filter_graph == nullptr)
        {
            throw static_cast<int>(AVERROR(ENOMEM));
        }

        // buffer video source: the decoded frames from the decoder will be inserted here.
        snprintf(args, sizeof(args), "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
                 codec_context->width, codec_context->height, pix_fmt,
                 time_base.num, time_base.den,
                 codec_context->sample_aspect_ratio.num, FFMAX(codec_context->sample_aspect_ratio.den, 1));

        //AVRational fr = av_guess_frame_rate(m_m_out.m_format_ctx, m_pVideoStream, nullptr);
        //if (fr.num && fr.den)
        //{
        //    av_strlcatf(buffersrc_args, sizeof(buffersrc_args), ":framerate=%d/%d", fr.num, fr.den);
        //}
        //
        //args.snprintf("%d:%d:%d:%d:%d", m_pCodecContext->width, m_pCodecContext->height, m_pCodecContext->format, 0, 0); //  0, 0 ok?

        ret = avfilter_graph_create_filter(&m_buffer_source_context, buffer_src, "in", args, nullptr, m_filter_graph);
        if (ret < 0)
        {
            Logging::error(destname(), "Cannot create buffer source (error '%1').", ffmpeg_geterror(ret).c_str());
            throw  ret;
        }

        //av_opt_set(m_pBufferSourceContext, "thread_type", "slice", AV_OPT_SEARCH_CHILDREN);
        //av_opt_set_int(m_pBufferSourceContext, "threads", FFMAX(1, av_cpu_count()), AV_OPT_SEARCH_CHILDREN);
        //av_opt_set_int(m_pBufferSourceContext, "threads", 16, AV_OPT_SEARCH_CHILDREN);

        // buffer video sink: to terminate the filter chain.

        pixel_fmts[0] = pix_fmt;
        pixel_fmts[1] = AV_PIX_FMT_NONE;

        buffer_sink_params.pixel_fmts = pixel_fmts;

        ret = avfilter_graph_create_filter(&m_buffer_sink_context, buffer_sink, "out", nullptr, &buffer_sink_params, m_filter_graph);

        if (ret < 0)
        {
            Logging::error(destname(), "Cannot create buffer sink (error '%1').", ffmpeg_geterror(ret).c_str());
            throw  ret;
        }

        // Cannot change FFmpeg's API, so we hide this warning
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
        ret = av_opt_set_int_list(m_buffer_sink_context, "pix_fmts", pixel_fmts, AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
#pragma GCC diagnostic pop

        if (ret < 0)
        {
            Logging::error(nullptr, "Cannot set output pixel format (error '%1').", ffmpeg_geterror(ret).c_str());
            throw  ret;
        }

        // Endpoints for the filter graph.
        outputs->name          = av_strdup("in");
        outputs->filter_ctx    = m_buffer_source_context;
        outputs->pad_idx       = 0;
        outputs->next          = nullptr;
        inputs->name           = av_strdup("out");
        inputs->filter_ctx     = m_buffer_sink_context;
        inputs->pad_idx        = 0;
        inputs->next           = nullptr;

        // args "null"      passthrough (dummy) filter for video
        // args "anull"     passthrough (dummy) filter for audio

        // https://stackoverflow.com/questions/31163120/c-applying-filter-in-ffmpeg
        //filters = "yadif=mode=send_frame:parity=auto:deint=interlaced";
        filters = "yadif=mode=send_frame:parity=auto:deint=all";
        //filters = "yadif=0:-1:0";
        //filters = "bwdif=mode=send_frame:parity=auto:deint=all";
        //filters = "kerndeint=thresh=10:map=0:order=0:sharp=1:twoway=1";
        //filters = "zoompan=z='min(max(zoom,pzoom)+0.0015,1.5)':d=1:x='iw/2-(iw/zoom/2)':y='ih/2-(ih/zoom/2)'";

        // vaapi_deinterlace=rate=field
        // format=nv12,hwupload,deinterlace_vaapi,hwdownload,format=nv12
        // deinterlace_vaapi,scale_vaapi=w=1280:h=720,hwdownload,format=nv12

        ret = avfilter_graph_parse_ptr(m_filter_graph, filters, &inputs, &outputs, nullptr);
        if (ret < 0)
        {
            Logging::error(destname(), "avfilter_graph_parse_ptr failed (error '%1').", ffmpeg_geterror(ret).c_str());
            throw  ret;
        }

        ret = avfilter_graph_config(m_filter_graph, nullptr);
        if (ret < 0)
        {
            Logging::error(destname(), "avfilter_graph_config failed (error '%1').", ffmpeg_geterror(ret).c_str());
            throw  ret;
        }

        Logging::debug(destname(), "Deinterlacing initialised with filters '%1'.", filters);
    }
    catch (int _ret)
    {
        ret = _ret;
    }

    if (inputs != nullptr)
    {
        avfilter_inout_free(&inputs);
    }
    if (outputs != nullptr)
    {
        avfilter_inout_free(&outputs);
    }

    return ret;
}

AVFrame *FFmpeg_Transcoder::send_filters(AVFrame * srcframe, int & ret)
{
    AVFrame *tgtframe = srcframe;

    ret = 0;

    if (m_buffer_source_context != nullptr /*&& srcframe->interlaced_frame*/)
    {
        try
        {
            AVFrame * filterframe   = nullptr;

            //pFrame->pts = av_frame_get_best_effort_timestamp(pFrame);
            // push the decoded frame into the filtergraph

            if ((ret = av_buffersrc_add_frame_flags(m_buffer_source_context, srcframe, AV_BUFFERSRC_FLAG_KEEP_REF)) < 0)
            {
                Logging::warning(destname(), "Error while feeding the frame to filtergraph (error '%1').", ffmpeg_geterror(ret).c_str());
                throw ret;
            }

            ret = init_frame(&filterframe, destname());
            if (ret < 0)
            {
                Logging::error(destname(), "Unable to allocate filter frame (error '%1').", ffmpeg_geterror(ret).c_str());
                throw ret;
            }

            // pull filtered frames from the filtergraph
            ret = av_buffersink_get_frame(m_buffer_sink_context, filterframe);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            {
                // Not an error, go on
                av_frame_free(&filterframe);
                ret = 0;
            }
            else if (ret < 0)
            {
                Logging::error(destname(), "Error while getting frame from filtergraph (error '%1').", ffmpeg_geterror(ret).c_str());
                av_frame_free(&filterframe);
                throw ret;
            }
            else
            {
                // All OK; copy filtered frame and unref original
                tgtframe = filterframe;

                tgtframe->pts = srcframe->pts;
#if LAVF_DEP_AVSTREAM_CODEC
                tgtframe->best_effort_timestamp = srcframe->best_effort_timestamp;
#else
                tgtframe->best_effort_timestamp = av_frame_get_best_effort_timestamp(srcframe);
#endif
                av_frame_free(&srcframe);
            }
        }
        catch (int _ret)
        {
            ret = _ret;
        }
    }

    return tgtframe;
}

// free

void FFmpeg_Transcoder::free_filters()
{
    if (m_buffer_sink_context != nullptr)
    {
        avfilter_free(m_buffer_sink_context);
        m_buffer_sink_context = nullptr;
    }

    if (m_buffer_source_context != nullptr)
    {
        avfilter_free(m_buffer_source_context);
        m_buffer_source_context = nullptr;
    }

    if (m_filter_graph != nullptr)
    {
        avfilter_graph_free(&m_filter_graph);
        m_filter_graph = nullptr;
    }
}

int FFmpeg_Transcoder::stack_seek_frame(uint32_t frame_no)
{
    if (frame_no > 0 && frame_no <= video_frame_count())
    {
        std::lock_guard<std::recursive_mutex> lck (m_seek_to_fifo_mutex);
        m_seek_to_fifo.push(frame_no);  // Seek to this frame next decoding operation
        return 0;
    }
    else
    {
        errno = EINVAL;
        Logging::error(destname(), "stack_seek_frame() failed: Frame %1 was requested, but is out of range (1...%2)", frame_no, video_frame_count() + 1);
        return AVERROR(EINVAL);
    }
}

int FFmpeg_Transcoder::stack_seek_segment(uint32_t segment_no)
{
    if (segment_no > 0 && segment_no <= segment_count())
    {
        std::lock_guard<std::recursive_mutex> lck (m_seek_to_fifo_mutex);
        m_seek_to_fifo.push(segment_no);  // Seek to this segment next decoding operation
        return 0;
    }
    else
    {
        errno = EINVAL;
        Logging::error(destname(), "stack_seek() failed: Segment %1 was requested, but is out of range (1...%2)", segment_no, video_frame_count() + 1);
        return AVERROR(EINVAL);
    }
}

bool FFmpeg_Transcoder::is_multiformat() const
{
    if (m_current_format == nullptr)
    {
        return false;
    }
    else
    {
        return m_current_format->is_multiformat();
    }
}


bool FFmpeg_Transcoder::is_frameset() const
{
    if (m_current_format == nullptr)
    {
        return false;
    }
    else
    {
        return m_current_format->is_frameset();
    }
}

bool FFmpeg_Transcoder::is_hls() const
{
    if (m_current_format == nullptr)
    {
        return false;
    }
    else
    {
        return m_current_format->is_hls();
    }
}

bool FFmpeg_Transcoder::have_seeked() const
{
    return m_have_seeked;
}

enum AVPixelFormat FFmpeg_Transcoder::get_format_static(AVCodecContext *input_codec_ctx, const enum AVPixelFormat *pix_fmts)
{
    FFmpeg_Transcoder * pThis = static_cast<FFmpeg_Transcoder *>(input_codec_ctx->opaque);
    return pThis->get_format(input_codec_ctx, pix_fmts);
}

enum AVPixelFormat FFmpeg_Transcoder::get_format(__attribute__((unused)) AVCodecContext *input_codec_ctx, const enum AVPixelFormat *pix_fmts)
{
    if (params.m_hwaccel_dec_device_type == AV_HWDEVICE_TYPE_NONE)
    {
        // We should never happen to end up here...
        Logging::error(filename(), "Unable to decode this file using hardware acceleration: Internal error! No hardware device tyoe set.");
        return AV_PIX_FMT_NONE;
    }

    AVPixelFormat pix_fmt_expected = m_dec_hw_pix_fmt;

    for (const AVPixelFormat *p = pix_fmts; *p != AV_PIX_FMT_NONE; p++)
    {
        if (*p == pix_fmt_expected)
        {
            return pix_fmt_expected;
        }
    }

    Logging::error(filename(), "Unable to decode this file using hardware acceleration. Expected format '%1' not supported.", get_pix_fmt_name(pix_fmt_expected).c_str());

    return AV_PIX_FMT_NONE;
}

int FFmpeg_Transcoder::hwdevice_ctx_create(AVBufferRef ** hwaccel_enc_device_ctx, AVHWDeviceType dev_type, const std::string & device) const
{
    std::string active_device(device);
    int ret;

    if (active_device == "AUTO" && dev_type == AV_HWDEVICE_TYPE_VAAPI)
    {
        active_device = "/dev/dri/renderD128";	//** @todo HWACCEL - Try to autodetect rendering device
    }

    ret = av_hwdevice_ctx_create(hwaccel_enc_device_ctx, dev_type, !active_device.empty() ? active_device.c_str() : nullptr, nullptr, 0);
    if (ret < 0)
    {
        Logging::error(destname(), "Failed to create a %1 device (error '%2').", hwdevice_get_type_name(dev_type), ffmpeg_geterror(ret).c_str());
        return ret;
    }
    return 0;
}

int FFmpeg_Transcoder::hwdevice_ctx_add_ref(AVCodecContext *input_codec_ctx)
{
    assert(m_hwaccel_dec_device_ctx != nullptr);
    input_codec_ctx->hw_device_ctx = av_buffer_ref(m_hwaccel_dec_device_ctx);
    if (input_codec_ctx->hw_device_ctx == nullptr)
    {
        int ret = AVERROR(ENOMEM);
        Logging::error(destname(), "A hardware device reference create failed (error '%1').", ffmpeg_geterror(ret).c_str());
        return ret;
    }

    input_codec_ctx->opaque     = static_cast<void*>(this);
    input_codec_ctx->get_format = &FFmpeg_Transcoder::get_format_static;

    return 0;
}

void FFmpeg_Transcoder::hwdevice_ctx_free(AVBufferRef **hwaccel_device_ctx)
{
    if (*hwaccel_device_ctx != nullptr)
    {
        av_buffer_unref(hwaccel_device_ctx);
        *hwaccel_device_ctx = nullptr;
    }
}

int FFmpeg_Transcoder::hwframe_ctx_set(AVCodecContext *output_codec_ctx, AVCodecContext *input_codec_ctx, AVBufferRef *hw_device_ctx) const
{
    AVBufferRef *hw_new_frames_ref;
    AVHWFramesContext *frames_ctx = nullptr;
    int ret = 0;

    hw_new_frames_ref = av_hwframe_ctx_alloc(hw_device_ctx);
    if (hw_new_frames_ref == nullptr)
    {
        ret = AVERROR(ENOMEM);
        Logging::error(destname(), "hwframe_ctx_set(): Failed to create hwframe context (error '%1').", ffmpeg_geterror(ret).c_str());
        return ret;
    }

    frames_ctx = reinterpret_cast<AVHWFramesContext *>(hw_new_frames_ref->data);
    frames_ctx->format    = m_enc_hw_pix_fmt;
    frames_ctx->sw_format = /*input_codec_ctx->sw_pix_fmt; */find_sw_fmt_by_hw_type(params.m_hwaccel_enc_device_type);
    frames_ctx->width     = input_codec_ctx->width;
    frames_ctx->height    = input_codec_ctx->height;

    frames_ctx->initial_pool_size = 20;	// Driver default seems to be 17

    ret = av_hwframe_ctx_init(hw_new_frames_ref);
    if (ret < 0)
    {
        Logging::error(destname(), "hwframe_ctx_set(): Failed to initialise hwframe context (error '%1').", ffmpeg_geterror(ret).c_str());
        av_buffer_unref(&hw_new_frames_ref);
        return ret;
    }

    output_codec_ctx->hw_frames_ctx = av_buffer_ref(hw_new_frames_ref);
    if (!output_codec_ctx->hw_frames_ctx)
    {
        Logging::error(destname(), "hwframe_ctx_set(): A hardware frame reference create failed (error '%1').", ffmpeg_geterror(AVERROR(ENOMEM)).c_str());
        ret = AVERROR(ENOMEM);
    }

    av_buffer_unref(&hw_new_frames_ref);

    return 0;
}

//int FFmpeg_Transcoder::hwframe_ctx_set(AVCodecContext *output_codec_ctx, AVCodecContext *input_codec_ctx, AVBufferRef *hw_device_ctx)
//{
//    // If the decoder runs in hardware, we should use the decoder's frames context. This will save us from
//    // having to transfer frames from hardware to software and vice versa.
//    // If the decoder runs in software, create a new frames context.
//    if (input_codec_ctx->hw_frames_ctx != nullptr)
//    {
//        Logging::debug(destname(), "Hardware encoder init: Hardware decoder active, using decoder hw_frames_ctx for encoder.");

//        /* we need to ref hw_frames_ctx of decoder to initialize encoder's codec.
//       Only after we get a decoded frame, can we obtain its hw_frames_ctx */
//        output_codec_ctx->hw_frames_ctx = av_buffer_ref(input_codec_ctx->hw_frames_ctx);
//        if (!output_codec_ctx->hw_frames_ctx)
//        {
//            int ret = AVERROR(ENOMEM);
//            Logging::error(destname(), "A hardware frame reference create failed (error '%1').", ffmpeg_geterror(ret).c_str());
//            return ret;
//        }

//        m_hwaccel_dec = true;   /* Doing decoding in hardware */
//    }
//    else {
//        Logging::debug(destname(), "Hardware encoder init: Software decoder active, creating new hw_frames_ctx for encoder.");

//        AVBufferRef *hw_new_frames_ref;
//        AVHWFramesContext *frames_ctx = nullptr;
//        int ret = 0;

//        if (!(hw_new_frames_ref = av_hwframe_ctx_alloc(hw_device_ctx)))
//        {
//            ret = AVERROR(ENOMEM);
//            Logging::error(destname(), "Failed to create hwframe context (error '%1').", ffmpeg_geterror(ret).c_str());
//            return ret;
//        }
//        frames_ctx = (AVHWFramesContext *)(hw_new_frames_ref->data);
//        frames_ctx->format    = m_hw_pix_fmt;
//        frames_ctx->sw_format = find_sw_fmt_by_hw_type(params.m_hwaccel_enc_device_type);
//        frames_ctx->width     = input_codec_ctx->width;
//        frames_ctx->height    = input_codec_ctx->height;

//        frames_ctx->initial_pool_size = 20;	// Driver default: 17
//        if ((ret = av_hwframe_ctx_init(hw_new_frames_ref)) < 0)
//        {
//            Logging::error(destname(), "Failed to initialise hwframe context (error '%1').", ffmpeg_geterror(ret).c_str());
//            av_buffer_unref(&hw_new_frames_ref);
//            return ret;
//        }

//        output_codec_ctx->hw_frames_ctx = av_buffer_ref(hw_new_frames_ref);
//        if (!output_codec_ctx->hw_frames_ctx)
//        {
//            Logging::error(destname(), "A hardware frame reference create failed (error '%1').", ffmpeg_geterror(AVERROR(ENOMEM)));
//            ret = AVERROR(ENOMEM);
//        }

//        av_buffer_unref(&hw_new_frames_ref);

//        m_hwaccel_dec = false;   /* Doing decoding in software */
//    }

//    return 0;
//}

int FFmpeg_Transcoder::hwframe_copy_from_hw(AVCodecContext * /*ctx*/, AVFrame ** sw_frame, const AVFrame * hw_frame) const
{
    int ret;

    ret = init_frame(sw_frame, destname());
    if (ret < 0)
    {
        return ret;
    }

    ret = av_frame_copy_props(*sw_frame, hw_frame);
    if (ret < 0)
    {
        Logging::error(destname(), "hwframe_copy_from_hw(): Failed to copy frame properties (error '%1').", ffmpeg_geterror(ret).c_str());
        return ret;
    }

    ret = av_hwframe_transfer_data(*sw_frame, hw_frame, 0);
    if (ret < 0)
    {
        Logging::error(destname(), "hwframe_copy_from_hw(): Error while transferring frame data to surface (error '%1').", ffmpeg_geterror(ret).c_str());
        return ret;
    }

    return 0;
}

int FFmpeg_Transcoder::hwframe_copy_to_hw(AVCodecContext *ctx, AVFrame ** hw_frame, const AVFrame * sw_frame) const
{
    int ret;

    ret = init_frame(hw_frame, destname());
    if (ret < 0)
    {
        return ret;
    }

    ret = av_frame_copy_props(*hw_frame, sw_frame);
    if (ret < 0)
    {
        Logging::error(destname(), "hwframe_copy_to_hw(): Failed to copy frame properties (error '%1').", ffmpeg_geterror(ret).c_str());
        return ret;
    }

    ret = av_hwframe_get_buffer(ctx->hw_frames_ctx, *hw_frame, 0);
    if (ret < 0)
    {
        Logging::error(destname(), "hwframe_copy_to_hw(): Failed to copy frame buffers to hardware memory (error '%1').", ffmpeg_geterror(ret).c_str());
        return ret;
    }

    if ((*hw_frame)->hw_frames_ctx == nullptr)
    {
        ret = AVERROR(ENOMEM);
        Logging::error(destname(), "hwframe_copy_to_hw(): Failed to copy frame buffers to hardware memory (error '%1').", ffmpeg_geterror(ret).c_str());
        return ret;
    }

    ret = av_hwframe_transfer_data(*hw_frame, sw_frame, 0);
    if (ret < 0)
    {
        Logging::error(destname(), "hwframe_copy_to_hw(): Error while transferring frame data to surface (error '%1').", ffmpeg_geterror(ret).c_str());
        return ret;
    }

    return 0;
}

/**
 * @todo HWACCEL - Supported formats
 *
 * Tested and working: VA-API, MMAL and OMX.
 *
 * Some VA-API formats do not yet work, see "fixit"
 *
 * V4LM2M: implemented, but untested
 * NIVIDA/CUDA: implemented, but untested
 *
 */
int FFmpeg_Transcoder::get_hw_decoder_name(AVCodecID codec_id, std::string *codec_name) const
{
    std::string codec_name_buf;
    int ret = 0;

    switch (params.m_hwaccel_dec_API)
    {
    case HWACCELAPI_VAAPI:
    {
        ret = get_hw_vaapi_codec_name(codec_id, &codec_name_buf);
        break;
    }
    case HWACCELAPI_MMAL:
    {
        ret = get_hw_mmal_decoder_name(codec_id, &codec_name_buf);
        break;
    }
        //case HWACCELAPI_V4L2M2M:
        //{
        //    ret = get_hw_v4l2m2m_decoder_name(codec_id, &codec_name_buf);
        //    break;
        //}
    case HWACCELAPI_NONE:
    default:
    {
        ret = AVERROR_DECODER_NOT_FOUND;
        break;
    }
    }

    if (codec_name != nullptr)
    {
        if (!ret)
        {
            *codec_name = codec_name_buf;
        }
        else
        {
            codec_name->clear();
        }
    }

    return ret;
}

int FFmpeg_Transcoder::get_hw_encoder_name(AVCodecID codec_id, std::string *codec_name) const
{
    std::string codec_name_buf;
    int ret = 0;

    switch (params.m_hwaccel_enc_API)
    {
    case HWACCELAPI_VAAPI:
    {
        ret = get_hw_vaapi_codec_name(codec_id, &codec_name_buf);
        break;
    }
    case HWACCELAPI_OMX:
    {
        ret = get_hw_omx_encoder_name(codec_id, &codec_name_buf);
        break;
    }
        //case HWACCELAPI_V4L2M2M:
        //{
        //    ret = get_hw_v4l2m2m_encoder_name(codec_id, &codec_name_buf);
        //    break;
        //}
    case HWACCELAPI_NONE:
    default:
    {
        ret = AVERROR_DECODER_NOT_FOUND;
        break;
    }
    }

    if (codec_name != nullptr)
    {
        if (!ret)
        {
            *codec_name = codec_name_buf;
        }
        else
        {
            codec_name->clear();
        }
    }

    return ret;
}

int FFmpeg_Transcoder::get_hw_vaapi_codec_name(AVCodecID codec_id, std::string *codec_name) const
{
    int ret = 0;
    /**
     * *** Intel VAAPI de/encoder ***
     *
     * h264_vaapi           H.264/AVC (VAAPI) (codec h264)
     * hevc_vaapi           H.265/HEVC (VAAPI) (codec hevc)
     * mjpeg_vaapi          MJPEG (VAAPI) (codec mjpeg)
     * mpeg2_vaapi          MPEG-2 (VAAPI) (codec mpeg2video)
     * vp1_vaapi            VC1 (VAAPI) (codec vc1) seems to be possible on my hardware
     * vp8_vaapi            VP8 (VAAPI) (codec vp8)
     * vp9_vaapi            VP9 (VAAPI) (codec vp9)
     *
     */
    switch (codec_id)
    {
    case AV_CODEC_ID_H264:
    {
        *codec_name = "h264_vaapi";
        break;
    }
        /**
          * @todo fixit, MPEG-1 decoding does not work...
          *
          * Program terminated with signal SIGSEGV, Segmentation fault.
          * #0  __memmove_avx_unaligned_erms () at ../sysdeps/x86_64/multiarch/memmove-vec-unaligned-erms.S:383
          * 383     ../sysdeps/x86_64/multiarch/memmove-vec-unaligned-erms.S: Datei oder Verzeichnis nicht gefunden.
          * [Current thread is 1 (Thread 0x7f95a24d4700 (LWP 16179))]
          * (gdb) bt
          * #0  __memmove_avx_unaligned_erms () at ../sysdeps/x86_64/multiarch/memmove-vec-unaligned-erms.S:383
          * #1  0x00007f95903c4e26 in ?? () from /usr/lib/x86_64-linux-gnu/dri/iHD_drv_video.so
          * #2  0x00007f95aaae80b8 in vaGetImage () from /lib/x86_64-linux-gnu/libva.so.2
          * #3  0x00007f95af524bb5 in ?? () from /lib/x86_64-linux-gnu/libavutil.so.56
          * #4  0x00007f95af5250fb in ?? () from /lib/x86_64-linux-gnu/libavutil.so.56
          * #5  0x00007f95af51c37f in av_hwframe_transfer_data () from /lib/x86_64-linux-gnu/libavutil.so.56
          * #6  0x00007f95af51c406 in av_hwframe_transfer_data () from /lib/x86_64-linux-gnu/libavutil.so.56
          * #7  0x0000555da4fde146 in FFmpeg_Transcoder::decode_video_frame (this=0x7f9598002e90, pkt=0x7f95a24d2f90, decoded=0x7f95a24d2ec4) at ffmpeg_transcoder.cc:2655
          * #8  0x0000555da4fde5cd in FFmpeg_Transcoder::decode_frame (this=0x7f9598002e90, pkt=0x7f95a24d2f90) at ffmpeg_transcoder.cc:2852
          * #9  0x0000555da4fdea4b in FFmpeg_Transcoder::read_decode_convert_and_store (this=0x7f9598002e90, finished=0x7f95a24d3030) at ffmpeg_transcoder.cc:3189
          * #10 0x0000555da4fdfa73 in FFmpeg_Transcoder::process_single_fr (this=this\@entry=0x7f9598002e90, status=@0x7f95a24d3134: 0) at ffmpeg_transcoder.cc:3987
          * #11 0x0000555da4f8c997 in transcoder_thread (arg=optimized out) at transcode.cc:874
          * #12 0x0000555da4fc54ef in thread_pool::loop_function (this=0x7f959c002b40) at thread_pool.cc:78
          * #13 0x00007f95aeaf4c10 in ?? () from /lib/x86_64-linux-gnu/libstdc++.so.6
          * #14 0x00007f95ae9f0ea7 in start_thread (arg=optimized out) at pthread_create.c:477
          * #15 0x00007f95ae920d4f in clone () at ../sysdeps/unix/sysv/linux/x86_64/clone.S:95
          *
          */
        //case AV_CODEC_ID_MJPEG:
        //{
        //    *codec_name = "mjpeg_vaapi";
        //    break;
        //}
    case AV_CODEC_ID_MPEG2VIDEO:
    {
        *codec_name = "mpeg2_vaapi";
        break;
    }
    case AV_CODEC_ID_HEVC:
    {
        *codec_name = "hevc_vaapi";
        break;
    }
    case AV_CODEC_ID_VC1:
    {
        *codec_name = "vc1_vaapi";
        break;
    }
    case AV_CODEC_ID_VP8:
    {
        *codec_name = "vp9_vaapi";
        break;
    }
    case AV_CODEC_ID_VP9:
    {
        *codec_name = "vp9_vaapi";
        break;
    }
    default:
    {
        ret = AVERROR_DECODER_NOT_FOUND;
        break;
    }
    }

    return ret;
}

int FFmpeg_Transcoder::get_hw_mmal_decoder_name(AVCodecID codec_id, std::string *codec_name) const
{
    int ret = 0;
    /**
     * *** MMAL decoder ***
     *
     * h264_mmal            h264 (mmal) (codec h264)
     * mpeg2_mmal           mpeg2 (mmal) (codec mpeg2video)
     * mpeg4_mmal           mpeg4 (mmal) (codec mpeg4)
     * vc1_mmal             vc1 (mmal) (codec vc1)
     *
     */
    switch (codec_id)
    {
    case AV_CODEC_ID_H264:
    {
        *codec_name = "h264_mmal";
        break;
    }
        /**
          * @todo mmal MPEG1 hardware acceleration not working. Probably because I have not bought a key... @n
          * @n
          * INFO   : [/root/test/in/En Vogue - Don-t Let Go (Love) (Official Music Video) (mpeg2).mpeg] Transcoding to ts. @n
          * INFO   : [/root/test/in/En Vogue - Don-t Let Go (Love) (Official Music Video) (mpeg2).mpeg] Hardware decoder acceleration active using codec 'mpeg2_mmal'. @n
          * INFO   : [/root/test/in/En Vogue - Don-t Let Go (Love) (Official Music Video) (mpeg2).mpeg] Hardware decoder acceleration enabled. Codec 'mpeg2_mmal'. @n
          * mmal: mmal_vc_port_info_set: failed to set port info (2:0): EINVAL @n
          * mmal: mmal_vc_port_set_format: mmal_vc_port_info_set failed 0x6b985440 (EINVAL) @n
          * mmal: mmal_port_disable: port vc.ril.video_decode:in:0(MP2V)(0x6b985440) is not enabled @n
          * mmal: mmal_port_disable: port vc.ril.video_decode:out:0(0x6b985890) is not enabled @n
          * mmal: mmal_port_disable: port vc.ril.video_decode:ctr:0(0x6b94db90) is not enabled @n
          * ERROR  : [/root/test/in/En Vogue - Don-t Let Go (Love) (Official Music Video) (mpeg2).mpeg] Failed to open video input codec for stream #video (error '0'). @n
          * ERROR  : [/root/test/in/En Vogue - Don-t Let Go (Love) (Official Music Video) (mpeg2).mpeg] Failed to open video codec (error 'Unknown error occurred'). @n
          * ERROR  : [/root/test/out/En Vogue - Don-t Let Go (Love) (Official Music Video) (mpeg2).ts] Transcoding exited with error. @n
          * ERROR  : [/root/test/out/En Vogue - Don-t Let Go (Love) (Official Music Video) (mpeg2).ts] System error: (5) Input/output error @n
          * ERROR  : [/root/test/out/En Vogue - Don-t Let Go (Love) (Official Music Video) (mpeg2).ts] FFMpeg error: (-1313558101) Unknown error occurred
          */
    case AV_CODEC_ID_MPEG2VIDEO:
    {
        *codec_name = "mpeg2_mmal";
        break;
    }
    case AV_CODEC_ID_MPEG4:
    {
        *codec_name = "mpeg4_mmal";
        break;
    }
        /**
            * @todo mmal VC1 hardware acceleration not working. Probably because I have not bought a key... @n
            * @n
            * INFO   : [/root/test/in/Test_1440x576_WVC1_6Mbps.wmv] Hardware decoder acceleration active using codec 'vc1_mmal'. @n
            * INFO   : [/root/test/in/Test_1440x576_WVC1_6Mbps.wmv] Hardware decoder acceleration enabled. Codec 'vc1_mmal'. @n
            * mmal: mmal_vc_port_info_set: failed to set port info (2:0): EINVAL @n
            * mmal: mmal_vc_port_set_format: mmal_vc_port_info_set failed 0x6e54c560 (EINVAL) @n
            * mmal: mmal_port_disable: port vc.ril.video_decode:in:0(WVC1)(0x6e54c560) is not enabled @n
            * mmal: mmal_port_disable: port vc.ril.video_decode:out:0(0x6e546660) is not enabled @n
            * mmal: mmal_port_disable: port vc.ril.video_decode:ctr:0(0x6e54c240) is not enabled @n
            * ERROR  : [/root/test/in/Test_1440x576_WVC1_6Mbps.wmv] Failed to open video input codec for stream #video (error '0'). @n
            * ERROR  : [/root/test/in/Test_1440x576_WVC1_6Mbps.wmv] Failed to open video codec (error 'Unknown error occurred').
            */
    case AV_CODEC_ID_VC1:
    {
        *codec_name = "vc1_mmal";
        break;
    }
    default:
    {
        ret = AVERROR_DECODER_NOT_FOUND;
        break;
    }
    }

    return ret;
}

//int FFmpeg_Transcoder::get_hw_v4l2m2m_decoder_name(AVCodecID codec_id, std::string *codec_name) const
//{
//    int ret = 0;
//    /**
//     * *** v4l2m2m (Video2linux) decoder ***
//     *
//     * h263_v4l2m2m         V4L2 mem2mem H.263 decoder wrapper (codec h263)
//     * h264_v4l2m2m         V4L2 mem2mem H.264 decoder wrapper (codec h264)
//     * hevc_v4l2m2m         V4L2 mem2mem HEVC decoder wrapper (codec hevc)
//     * mpeg1_v4l2m2m        V4L2 mem2mem MPEG1 decoder wrapper (codec mpeg1video)
//     * mpeg2_v4l2m2m        V4L2 mem2mem MPEG2 decoder wrapper (codec mpeg2video)
//     * mpeg4_v4l2m2m        V4L2 mem2mem MPEG4 decoder wrapper (codec mpeg4)
//     * vc1_v4l2m2m          V4L2 mem2mem VC1 decoder wrapper (codec vc1)
//     * vp8_v4l2m2m          V4L2 mem2mem VP8 decoder wrapper (codec vp8)
//     * vp9_v4l2m2m          V4L2 mem2mem VP9 decoder wrapper (codec vp9)
//     */
//    switch (codec_id)
//    {
//    case AV_CODEC_ID_H263:
//    {
//        *codec_name = "h263_v4l2m2m";
//        break;
//    }
//    case AV_CODEC_ID_H264:
//    {
//        *codec_name = "h264_v4l2m2m";
//        break;
//    }
//    case AV_CODEC_ID_H265:
//    {
//        *codec_name = "hevc_v4l2m2m";
//        break;
//    }
//    case AV_CODEC_ID_MPEG1VIDEO:
//    {
//        *codec_name = "mpeg1_v4l2m2m";
//        break;
//    }
//    case AV_CODEC_ID_MPEG2VIDEO:
//    {
//        *codec_name = "mpeg2_v4l2m2m";
//        break;
//    }
//    case AV_CODEC_ID_MPEG4:
//    {
//        *codec_name = "mpeg4_v4l2m2m";
//        break;
//    }
//    case AV_CODEC_ID_VC1:
//    {
//        *codec_name = "vc1_v4l2m2m";
//        break;
//    }
//    case AV_CODEC_ID_VP8:
//    {
//        *codec_name = "vp8_v4l2m2m";
//        break;
//    }
//    case AV_CODEC_ID_VP9:
//    {
//        *codec_name = "vp9_v4l2m2m";
//        break;
//    }
//    default:
//    {
//        ret = AVERROR_DECODER_NOT_FOUND;
//        break;
//    }
//    }

//    return ret;
//}

int FFmpeg_Transcoder::get_hw_omx_encoder_name(AVCodecID codec_id, std::string *codec_name) const
{
    int ret = 0;
    /**
     * *** Openmax encoder ***
     *
     * h264_omx             OpenMAX IL H.264 video encoder (codec h264)
     */
    switch (codec_id)
    {
    case AV_CODEC_ID_H264:
    {
        *codec_name = "h264_omx";
        break;
    }
    default:
    {
        ret = AVERROR_DECODER_NOT_FOUND;
        break;
    }
    }

    return ret;
}

int FFmpeg_Transcoder::get_hw_v4l2m2m_encoder_name(AVCodecID codec_id, std::string *codec_name) const
{
    int ret = 0;
    /**
     *  * *** v4l2m2m (Video2linux) encoder ***
     *
     * h263_v4l2m2m         V4L2 mem2mem H.263 encoder wrapper (codec h263)
     * h264_v4l2m2m         V4L2 mem2mem H.264 encoder wrapper (codec h264)
     * hevc_v4l2m2m         V4L2 mem2mem HEVC encoder wrapper (codec hevc)
     * mpeg4_v4l2m2m        V4L2 mem2mem MPEG4 encoder wrapper (codec mpeg4)
     * vp8_v4l2m2m          V4L2 mem2mem VP8 encoder wrapper (codec vp8)
     */
    switch (codec_id)
    {
    case AV_CODEC_ID_H263:
    {
        *codec_name = "h263_v4l2m2m";
        break;
    }
    case AV_CODEC_ID_H264:
    {
        *codec_name = "h264_v4l2m2m";
        break;
    }
    case AV_CODEC_ID_H265:
    {
        *codec_name = "hevc_v4l2m2m";
        break;
    }
    case AV_CODEC_ID_MPEG4:
    {
        *codec_name = "mpeg4_v4l2m2m";
        break;
    }
    case AV_CODEC_ID_VP8:
    {
        *codec_name = "vp8_v4l2m2m";
        break;
    }
    default:
    {
        ret = AVERROR_DECODER_NOT_FOUND;
        break;
    }
    }

    return ret;
}

AVPixelFormat FFmpeg_Transcoder::find_sw_fmt_by_hw_type(AVHWDeviceType type)
{
    DEVICETYPE_MAP::const_iterator it = m_devicetype_map.find(type);

    if (it == m_devicetype_map.cend())
    {
        return AV_PIX_FMT_NONE;
    }

    return it->second;
}

void FFmpeg_Transcoder::get_pix_formats(AVPixelFormat *in_pix_fmt, AVPixelFormat *out_pix_fmt, AVCodecContext* output_codec_ctx) const
{
#if LAVF_DEP_AVSTREAM_CODEC
    *in_pix_fmt = static_cast<AVPixelFormat>(m_in.m_video.m_stream->codecpar->format);
#else
    *in_pix_fmt = static_cast<AVPixelFormat>(m_in.m_video.m_stream->codec->pix_fmt);
#endif

    if (m_hwaccel_enable_dec_buffering)
    {
        *in_pix_fmt = find_sw_fmt_by_hw_type(params.m_hwaccel_dec_device_type);
    }

    if (output_codec_ctx == nullptr)
    {
        output_codec_ctx = m_out.m_video.m_codec_ctx;
    }

    // Fail safe: If output_codec_ctx is NULL, set to something common (AV_PIX_FMT_YUV420P is widely used)
    *out_pix_fmt = (output_codec_ctx != nullptr) ? output_codec_ctx->pix_fmt : AV_PIX_FMT_YUV420P;

    if (*in_pix_fmt == AV_PIX_FMT_NONE)
    {
        // If input's stream pixel format is unknown, use same as output (may not work but at least will not crash FFmpeg)
        *in_pix_fmt = *out_pix_fmt;
    }

    // If hardware acceleration is enabled, e.g., output_codec_ctx->pix_fmt is AV_PIX_FMT_VAAPI
    // but the format actually is AV_PIX_FMT_NV12 so we use the correct value from sw_format in
    // the hardware frames context.
    if (m_hwaccel_enable_enc_buffering &&
            output_codec_ctx != nullptr &&
            output_codec_ctx->hw_frames_ctx != nullptr &&
            output_codec_ctx->hw_frames_ctx->data != nullptr)
    {
        *out_pix_fmt = reinterpret_cast<AVHWFramesContext*>(output_codec_ctx->hw_frames_ctx->data)->sw_format;
    }
}
