/***************************************************************************
            \file              ADM_ffmpeg_dxva2.cpp
            \brief Decoder using half ffmpeg/half dxva2
            \author mean (c) 2016

 Very similar to dxva2


 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#include "ADM_default.h"
#include "BVector.h"

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavutil/pixfmt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/hwcontext_dxva2.h"
#define CONFIG_DXVA2 1
#include "libavcodec/dxva2_internal.h"
#include "libavcodec/dxva2.h"
}

#include "ADM_codec.h"
#include "ADM_ffmp43.h"
#include "DIA_coreToolkit.h"
#include "ADM_dynamicLoading.h"
#include "ADM_render/GUI_render.h"
#include "prefs.h"
#include "ADM_coreDxva2.h"
#include "../private_inc/ADM_codecDxva2.h"
#include "ADM_threads.h"
#include "ADM_vidMisc.h"
#include "prefs.h"

static bool         dxva2Working=false;
static int  ADM_DXVA2getBuffer(AVCodecContext *avctx, AVFrame *pic,int flags);
static void ADM_DXVA2releaseBuffer(void *s, uint8_t *d);
static admMutex     imageMutex;
#if 1
#define aprintf(...) {}
#else
#define aprintf printf
#endif
/**
    \fn dxva2Usable
    \brief Return true if  dxva2 can be used...
*/
bool dxva2Usable(void)
{
    bool v=true;
    if(!dxva2Working) return false;
    if(!prefs->get(FEATURES_DXVA2,&v)) v=false;
    return v;
}
/**
    \fn dxva2Probe
    \brief Try loading d3d and dxva2...
*/
bool dxva2Probe(void)
{
    GUI_WindowInfo xinfo;
    void *draw;
    draw=UI_getDrawWidget();
    UI_getWindowInfo(draw,&xinfo );
    if(false==admD3D::init(&xinfo))
    {
	ADM_warning("D3D init failed\n");
 	return false;
    }
    if( admCoreCodecSupports(ADM_CORE_CODEC_FEATURE_DXVA2)==false)
    {
        GUI_Error_HIG(QT_TRANSLATE_NOOP("adm","Error"),QT_TRANSLATE_NOOP("adm","Core has been compiled without DXVA2 support, but the application has been compiled with it.\nInstallation mismatch"));
        dxva2Working=false;
        return false;
    }

    if(false==admDxva2::init(&xinfo)) return false;
    dxva2Working=true;
    return true;
}
//--




#if 1
#define aprintf(...) {}
#else
#define aprintf ADM_info
#endif

/**
  \fn dxvaBitDepthFromContext
*/
static int dxvaBitDepthFromContext(AVCodecContext *avctx)
{

  if(avctx->sw_pix_fmt==AV_PIX_FMT_YUV420P10) // not sure
      return 10;
  return 8;
}

/**
 *
 * @param instance
 * @param cookie
 * @return
 */
static  bool dxvaMarkSurfaceUsed(void *instance,void *cookie)
{
    decoderFFDXVA2 *inst=(decoderFFDXVA2 *)instance;
    admDx2Surface *s=(admDx2Surface *)cookie ;
    inst->markSurfaceUsed(s);
    return true;
}
/**
 *
 * @param instance
 * @param cookie
 * @return
 */
static  bool dxvaMarkSurfaceUnused(void *instance,void *cookie)
{
    decoderFFDXVA2 *inst=(decoderFFDXVA2 *)instance ;
    admDx2Surface *s=(admDx2Surface *)cookie ;
    inst->markSurfaceUnused(s);
    return true;
}

/**
    \fn vdpauRefDownload
    \brief Convert a VASurface image to a regular image
*/
static bool dxvaRefDownload(ADMImage *image, void *instance, void *cookie)
{
    decoderFFDXVA2 *inst=(decoderFFDXVA2 *)instance ;
    admDx2Surface *s=(admDx2Surface *) cookie;
    bool r=s->surfaceToAdmImage(image);
    inst->markSurfaceUnused(s);
    image->refType=ADM_HW_NONE;
    aprintf("Got frame\n");
    return r;
}

/**
    \fn ADM_DXVA2getBuffer
    \brief trampoline to get a DXVA2 surface
*/
int ADM_DXVA2getBuffer(AVCodecContext *avctx, AVFrame *pic,int flags)
{

    decoderFF *ff=(decoderFF *)avctx->opaque;
    decoderFFDXVA2 *dec=(decoderFFDXVA2 *)ff->getHwDecoder();
    aprintf("Get Buffer: FF avctx=%p parent=%p instance=%p\n",avctx,ff,dec);
    ADM_assert(dec);
    return dec->getBuffer(avctx,pic);
}

/**
 * \fn ADM_XVBAreleaseBuffer
 * @param avctx
 * @param pic
 */
void ADM_DXVA2releaseBuffer(void *opaque, uint8_t *data)
{
  admDx2Surface *w=(admDx2Surface *)opaque;
  aprintf("Release surface %p\n",w);
  if(!w)
  {
    ADM_warning("Cannot find image in buffers\n");
    return;
  }
  decoderFFDXVA2 *instance=(decoderFFDXVA2 *)w->parent;
  aprintf("Decoder=%p, surface internal =%p\n",w->decoder,w->surface);
  instance->releaseBuffer(w);
}

/**
    \fn dxva2Cleanup
*/
bool dxva2Cleanup(void)
{
  // return admLibVA::cleanup();
    return false;
}






extern "C"
{

static enum AVPixelFormat ADM_DXVA2_getFormat(struct AVCodecContext *avctx,  const enum AVPixelFormat *fmt)
{
    int i;
    ADM_info("[DXVA2]: GetFormat\n");
    AVCodecID id=AV_CODEC_ID_NONE;
    AVPixelFormat c;
    AVPixelFormat outPix;
    for(i=0;fmt[i]!=AV_PIX_FMT_NONE;i++)
    {
        c=fmt[i];
        char name[300]={0};
        av_get_pix_fmt_string(name,sizeof(name),c);
        ADM_info("[DXVA2]: Evaluating PIX_FMT %d,%s\n",c,name);
        av_get_codec_tag_string(name,sizeof(name),avctx->codec_id);
        ADM_info("\t  Evaluating codec %d,%s\n",avctx->codec_id,name);

        if(c!=AV_PIX_FMT_DXVA2_VLD) continue;
#define FMT_V_CHECK(x,y)      case AV_CODEC_ID_##x:   outPix=AV_PIX_FMT_DXVA2_VLD;id=avctx->codec_id;break;


        switch(avctx->codec_id)  //AV_CODEC_ID_H265
        {
            FMT_V_CHECK(H264,H264)
            FMT_V_CHECK(H265,H265)
//            FMT_V_CHECK(VC1,VC1)
//            FMT_V_CHECK(VP9,VP9)
            default:
                ADM_info("DXVA2 No hw support for format %d\n",avctx->codec_id);
                continue;
                break;
        }
        break;
    }
    if(id==AV_CODEC_ID_NONE)
    {
        return AV_PIX_FMT_NONE;
    }
    // Finish intialization of DXVA2 decoder
    const AVHWAccel *accel=ADM_acceleratedDecoderFF::parseHwAccel(outPix,id,AV_PIX_FMT_DXVA2_VLD);
    if(accel)
    {
        ADM_info("Found matching hw accelerator : %s\n",accel->name);
        ADM_info("Successfully setup hw accel\n");
        return AV_PIX_FMT_DXVA2_VLD;
    }
    return AV_PIX_FMT_NONE;
}
}

/**
 *
 * @param w
 * @param h
 * @param fcc
 * @param extraDataLen
 * @param extraData
 * @param bpp
 */
decoderFFDXVA2::decoderFFDXVA2(AVCodecContext *avctx,decoderFF *parent)
: ADM_acceleratedDecoderFF(avctx,parent)
{

    ADM_info("Setting up Dxva2 context (instance=%p)\n",this);
    alive=false;
    _context->slice_flags     = SLICE_FLAG_CODED_ORDER|SLICE_FLAG_ALLOW_FIELD;
    _context->thread_count    = 1;
    //
    for(int i=0;i<ADM_MAX_SURFACE;i++)
    {
        surfaces[i]=NULL;
        surface_infos[i].used = 0;
        surface_infos[i].age  = 0;
    }
    surface_age=1;

    // create decoder
    struct dxva_context *dx_context=new struct dxva_context;
    memset(dx_context,0,sizeof(*dx_context)); // dangerous...

    // Allocate temp buffer
    num_surfaces=4;

    switch(avctx->codec_id)
    {
        case AV_CODEC_ID_H265:
                align=128;
                num_surfaces+=16;
                break;
        case AV_CODEC_ID_H264:
                align=16;
                num_surfaces+=16;
                break;
        case AV_CODEC_ID_VP9:
                align=16;
                num_surfaces+=8;
                break;
        default:
                align=16;
                num_surfaces+=2;
                break;
    }

    // Allocate surfaces..
    int bits=dxvaBitDepthFromContext(avctx);
    if(!admDxva2::allocateD3D9Surface(num_surfaces,avctx->coded_width,avctx->coded_height,surfaces,align,bits))
    {
        ADM_warning("Cannot allocate surfaces \n");
        return ;
    }
    dx_context->decoder=admDxva2::createDecoder(avctx->codec_id,avctx->coded_width, avctx->coded_height,num_surfaces,surfaces,align,bits);
    aprintf("Decoder=%p\n",dx_context->decoder);
    if(!dx_context->decoder)
    {
        ADM_warning("Cannot create decoder\n");
        return ;
    }
    ADM_info("DXVA2 decoder created\n");
    // Populare queue with them
    for (int i=0;i<num_surfaces;i++)
    {
        admDx2Surface *w=new admDx2Surface(this,align);
        w->surface=surfaces[i];
        w->decoder = dx_context->decoder;
        dxvaPool.freeSurfaceQueue.append(w);
        dxvaPool.allSurfaceQueue.append(w);
        aprintf("Appending surface %p->internal %p\n",w,w->surface);
    }
    //
    _context->hwaccel_context = dx_context;
    _context->get_buffer2     = ADM_DXVA2getBuffer;
    _context->draw_horiz_band = NULL;

    dx_context->surface         = surfaces;
    dx_context->surface_count   = num_surfaces;
    dx_context->cfg             = admDxva2::getDecoderConfig(avctx->codec_id,bits);

    ADM_info("Ctor Successfully setup DXVA2 hw accel (%d surface created, ffdxva=%p,parent=%p,context=%p)\n",num_surfaces,this,parent,avctx);
    alive=true;
}

/**
 * \fn dtor
 */
decoderFFDXVA2::~decoderFFDXVA2()
{
    if(alive)
    {
        admDxva2::destroyD3DSurface(num_surfaces,surfaces);
        // TODO : flush pool
    }
    if(_context->hwaccel_context)
    {
        aprintf("Destroying context\n");
        struct dxva_context *dx_context=(struct dxva_context *)_context->hwaccel_context;
        _context->hwaccel_context=NULL;
        aprintf("Destroying decoder\n");
        admDxva2::destroyDecoder(dx_context->decoder);
        dx_context->decoder=NULL;
        aprintf("Context cleaned up\n");
        delete dx_context;
    }
}

/**
    \fn markSurfaceUsed
    \brief mark the surfave as used. Can be called multiple time.
*/
bool decoderFFDXVA2::markSurfaceUsed(admDx2Surface *s)
{
    imageMutex.lock();
    s->refCount++;
    imageMutex.unlock();
    return true;

}
/**
    \fn markSurfaceUnused
    \brief mark the surfave as unused by the caller. Can be called multiple time.
*/
bool decoderFFDXVA2::markSurfaceUnused(admDx2Surface *surface)
{

   imageMutex.lock();
   surface->refCount--;
   aprintf("Surface %x, Ref count is now %d\n",surface->surface,surface->refCount);
   if(!surface->refCount)
   {
        surface->removeRef();
        admDxva2::decoderRemoveRef(surface->decoder);
        dxvaPool.freeSurfaceQueue.append(surface);
   }
   imageMutex.unlock();
   return true;
}


/**
 * \fn uncompress
 * \brief
 * @param in
 * @param out
 * @return
 */
bool decoderFFDXVA2::uncompress (ADMCompressedImage * in, ADMImage * out)
{
    aprintf("==> uncompress %s\n",_context->codec->long_name);
    if(out->refType==ADM_HW_DXVA)
    {
            admDx2Surface *img=(admDx2Surface *)out->refDescriptor.refHwImage;
            markSurfaceUnused(img);
            out->refType=ADM_HW_NONE;
    }

    if (!in->dataLength )	// Null frame, silently skipped
    {
        out->_noPicture = 1;
        out->Pts=ADM_COMPRESSED_NO_PTS;
        out->refType=ADM_HW_NONE;
        ADM_info("[LibVa] Nothing to decode -> no Picture\n");
        return false;
    }


    if (!in->dataLength )	// Null frame, silently skipped
    {
        out->_noPicture = 1;
        out->Pts=ADM_COMPRESSED_NO_PTS;
        out->refType=ADM_HW_NONE;
        ADM_info("[dxva] Nothing to decode -> no Picture\n");
        return false;
    }

   // Put a safe value....
    out->Pts=in->demuxerPts;
    _context->reordered_opaque=in->demuxerPts;
    int got_picture;
    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.data=in->data;
    pkt.size=in->dataLength;
    if(in->flags&AVI_KEY_FRAME)
        pkt.flags=AV_PKT_FLAG_KEY;
    else
        pkt.flags=0;

    AVFrame *frame=_parent->getFramePointer();
    ADM_assert(frame);
    int ret = avcodec_decode_video2 (_context, frame, &got_picture, &pkt);

    if(ret<0)
    {
        char er[2048]={0};
        av_make_error_string(er, sizeof(er)-1, ret);
        ADM_warning("DXVA: Error %d in lavcodec (%s)\n",ret,er);
        out->refType=ADM_HW_NONE;
        return false;
    }
    if(frame->pict_type==AV_PICTURE_TYPE_NONE)
    {
        out->_noPicture=true;
        out->refType=ADM_HW_NONE;
        out->Pts= (uint64_t)(frame->reordered_opaque);
        ADM_info("[DXVA] --No picture \n");
        return false;
    }
   return readBackBuffer(frame,in,out);

}
/**
    \fn
 */
bool     decoderFFDXVA2::readBackBuffer(AVFrame *decodedFrame, ADMCompressedImage * in, ADMImage * out)
{
    aprintf("Reading Back Buffer\n");
    uint64_t pts_opaque=(uint64_t)(decodedFrame->reordered_opaque);
    out->Pts= (uint64_t)(pts_opaque);
    out->flags=admFrameTypeFromLav(decodedFrame);
    out->refType=ADM_HW_DXVA;
    out->refDescriptor.refCodec=this;
    admDx2Surface *img=(admDx2Surface *)(decodedFrame->data[0]);
    out->refDescriptor.refHwImage=img; // the ADM_vaImage in disguise
    markSurfaceUsed(img); // one ref for us too, it will be free when the image is cycled
    aprintf("ReadBack: Got image=%x surfaceId=%x\n",(int)(uintptr_t)decodedFrame->data[0],(int)img->surface);
    out->refDescriptor.refMarkUsed  =dxvaMarkSurfaceUsed;
    out->refDescriptor.refMarkUnused=dxvaMarkSurfaceUnused;
    out->refDescriptor.refDownload  =dxvaRefDownload;
    return true;
}
//---

/**
 */
int decoderFFDXVA2::getBuffer(AVCodecContext *avctx, AVFrame *pic)
{

    aprintf("-> Get buffer (%d,%p)\n",num_surfaces,this);
    ADM_assert(pic->format == AV_PIX_FMT_DXVA2_VLD);

    //--
    imageMutex.lock();
    if(dxvaPool.freeSurfaceQueue.empty())
    {
        ADM_warning("DXVA : No surface available!\n");
        imageMutex.unlock();
        return -1;
    }

    admDx2Surface *s= dxvaPool.freeSurfaceQueue[0];
    dxvaPool.freeSurfaceQueue.popFront();
    imageMutex.unlock();
    s->refCount=0;
    markSurfaceUsed(s); // 1 ref taken by lavcodec

    //
    pic->buf[0]=av_buffer_create((uint8_t *)&(s->surface),  // Maybe a memleak here...
                                     sizeof(s->surface),
                                     ADM_DXVA2releaseBuffer,
                                     (void *)s,
                                     AV_BUFFER_FLAG_READONLY);

    aprintf("Alloc Buffer : 0x%llx, surfaceid=%x\n",s,(int)s->surface);
    pic->data[0]=(uint8_t *)s;
    pic->data[3]=(uint8_t *)(uintptr_t)s->surface;
    pic->reordered_opaque= avctx->reordered_opaque;

    s->addRef();
    admDxva2::decoderAddRef(s->decoder);


    return 0;

}
/**
  \fn releaseBuffer LPDIRECT3DSURFACE9
*/
admDx2Surface        *decoderFFDXVA2::findBuffer(LPDIRECT3DSURFACE9 surface)
{
    for(int i=0;i<num_surfaces;i++)
    {
        admDx2Surface *s=dxvaPool.allSurfaceQueue[i];
        if(s->surface==surface)
        {
            return s;
        }
    }
    ADM_warning("Cannot find surface to be released\n");
    return NULL;
}


/**
 * \fn releaseBuffer
 */
bool decoderFFDXVA2::releaseBuffer(admDx2Surface *surface)
{
    markSurfaceUnused(surface);
    return true ;
}
/**
 */
class ADM_hwAccelEntryDxva2 : public ADM_hwAccelEntry
{
public:
                            ADM_hwAccelEntryDxva2();
     virtual bool           canSupportThis(struct AVCodecContext *avctx,  const enum AVPixelFormat *fmt,enum AVPixelFormat &outputFormat);
     virtual                ADM_acceleratedDecoderFF *spawn( struct AVCodecContext *avctx,  const enum AVPixelFormat *fmt );
     virtual                ~ADM_hwAccelEntryDxva2() {};
};
/**
 *
 */
ADM_hwAccelEntryDxva2::ADM_hwAccelEntryDxva2()
{
    name="DXVA2";
}
/**
 *
 * @param avctx
 * @param fmt
 * @param outputFormat
 * @return
 */
bool           ADM_hwAccelEntryDxva2::canSupportThis(struct AVCodecContext *avctx,  const enum AVPixelFormat *fmt,enum AVPixelFormat &outputFormat)
{
    bool enabled=false;
    prefs->get(FEATURES_DXVA2,&enabled);
    if(!enabled)
    {
        ADM_info("DXVA2 not enabled\n");
        return false;
    }
    enum AVPixelFormat ofmt=ADM_DXVA2_getFormat(avctx,fmt);
    if(ofmt==AV_PIX_FMT_NONE)
        return false;

    outputFormat=ofmt;
    ADM_info("This is maybe supported by DXVA2...\n");
    int bits=dxvaBitDepthFromContext(avctx);
    if(!admDxva2::supported(avctx->codec_id,bits)) // not sure either
    {
        ADM_warning("Not supported by DXVA2\n");
        return false;
    }
    return true;
}


/**
 *
 * @param avctx
 * @param fmt
 * @return
 */
ADM_acceleratedDecoderFF *ADM_hwAccelEntryDxva2::spawn( struct AVCodecContext *avctx,  const enum AVPixelFormat *fmt )
{
    decoderFF *ff=(decoderFF *)avctx->opaque;
    decoderFFDXVA2 *instance= new decoderFFDXVA2(avctx,ff);
    if(!instance->isAlive())
    {
                ADM_warning("DXVA2 decoder is not alive, killing it\n");
                delete instance;
                instance=NULL;
    }else
    {
        ADM_info("Spawned DXVA2 decoder with %d surfaces, avcontext=%p, ff=%p, instance=%p\n",instance->getNumSurfaces(),
                                avctx,ff,instance);
    }
    return instance;
}
static ADM_hwAccelEntryDxva2 dxva2Entry;
/**
 *
 * @return
 */
bool initDXVA2Decoder(void)
{
    ADM_info("Registering DXVA2 hw decocer\n");
    ADM_hwAccelManager::registerDecoder(&dxva2Entry);
    return true;
}

// EOF
