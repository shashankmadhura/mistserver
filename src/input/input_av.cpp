#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mist/defines.h>
#include <mist/stream.h>
#include <string>

#include "input_av.h"

namespace Mist{
  inputAV::inputAV(Util::Config *cfg) : Input(cfg){
    pFormatCtx = 0;
    capa["name"] = "AV";
    capa["desc"] =
        "This input uses libavformat to read any type of file. Unfortunately this input cannot be "
        "redistributed, but it is a great tool for testing the other file-based inputs against.";
    capa["source_match"] = "/*";
    capa["source_file"] = "$source";
    capa["priority"] = 1;
    capa["codecs"][0u][0u].null();
    capa["codecs"][0u][1u].null();
    capa["codecs"][0u][2u].null();
    av_register_all();
    AVCodec *cInfo = 0;
    while ((cInfo = av_codec_next(cInfo)) != 0){
      if (cInfo->type == AVMEDIA_TYPE_VIDEO){capa["codecs"][0u][0u].append(cInfo->name);}
      if (cInfo->type == AVMEDIA_TYPE_AUDIO){capa["codecs"][0u][1u].append(cInfo->name);}
      if (cInfo->type == AVMEDIA_TYPE_SUBTITLE){capa["codecs"][0u][3u].append(cInfo->name);}
    }
  }

  inputAV::~inputAV(){
    if (pFormatCtx){avformat_close_input(&pFormatCtx);}
  }

  bool inputAV::checkArguments(){
    if (config->getString("input") == "-"){
      std::cerr << "Input from stdin not yet supported" << std::endl;
      return false;
    }
    if (!config->getString("streamname").size()){
      if (config->getString("output") == "-"){
        std::cerr << "Output to stdout not yet supported" << std::endl;
        return false;
      }
    }else{
      if (config->getString("output") != "-"){
        std::cerr << "File output in player mode not supported" << std::endl;
        return false;
      }
    }
    return true;
  }

  bool inputAV::preRun(){
    // make sure all av inputs are registered properly, just in case
    // the constructor already does this, but under windows it doesn't remember that it has.
    // Very sad, that. We may need to get windows some medication for it.
    av_register_all();

    // close any already open files
    if (pFormatCtx){
      avformat_close_input(&pFormatCtx);
      pFormatCtx = 0;
    }

    // Open video file
    int ret = avformat_open_input(&pFormatCtx, config->getString("input").c_str(), NULL, NULL);
    if (ret != 0){
      char errstr[300];
      av_strerror(ret, errstr, 300);
      FAIL_MSG("Could not open file: %s", errstr);
      return false; // Couldn't open file
    }

    // Retrieve stream information
    ret = avformat_find_stream_info(pFormatCtx, NULL);
    if (ret < 0){
      char errstr[300];
      av_strerror(ret, errstr, 300);
      FAIL_MSG("Could not find stream info: %s", errstr);
      return false;
    }
    return true;
  }

  bool inputAV::readHeader(){
    for (unsigned int i = 0; i < pFormatCtx->nb_streams;){
      AVStream *strm = pFormatCtx->streams[i++];
      size_t idx = meta.addTrack();
      meta.setID(idx, i);
      switch (strm->codecpar->codec_id){
      case AV_CODEC_ID_HEVC: meta.setCodec(idx, "HEVC"); break;
      case AV_CODEC_ID_MPEG1VIDEO:
      case AV_CODEC_ID_MPEG2VIDEO: meta.setCodec(idx, "MPEG2"); break;
      case AV_CODEC_ID_MP2: meta.setCodec(idx, "MP2"); break;
      case AV_CODEC_ID_H264: meta.setCodec(idx, "H264"); break;
      case AV_CODEC_ID_THEORA: meta.setCodec(idx, "theora"); break;
      case AV_CODEC_ID_VORBIS: meta.setCodec(idx, "vorbis"); break;
      case AV_CODEC_ID_OPUS: meta.setCodec(idx, "opus"); break;
      case AV_CODEC_ID_VP8: meta.setCodec(idx, "VP8"); break;
      case AV_CODEC_ID_VP9: meta.setCodec(idx, "VP9"); break;
      case AV_CODEC_ID_AAC: meta.setCodec(idx, "AAC"); break;
      case AV_CODEC_ID_MP3: meta.setCodec(idx, "MP3"); break;
      case AV_CODEC_ID_AC3:
      case AV_CODEC_ID_EAC3: meta.setCodec(idx, "AC3"); break;
      default:
        const AVCodecDescriptor *desc = avcodec_descriptor_get(strm->codecpar->codec_id);
        if (desc && desc->name){
          meta.setCodec(idx, desc->name);
        }else{
          meta.setCodec(idx, "?");
        }
        break;
      }
      if (strm->codecpar->extradata_size){
        meta.setInit(idx, std::string((char *)strm->codecpar->extradata, strm->codecpar->extradata_size));
      }
      if (strm->codecpar->codec_type == AVMEDIA_TYPE_VIDEO){
        meta.setType(idx, "video");
        if (strm->avg_frame_rate.den && strm->avg_frame_rate.num){
          meta.setFpks(idx, (strm->avg_frame_rate.num * 1000) / strm->avg_frame_rate.den);
        }else{
          meta.setFpks(idx, 0);
        }
        meta.setWidth(idx, strm->codecpar->width);
        meta.setHeight(idx, strm->codecpar->height);
      }
      if (strm->codecpar->codec_type == AVMEDIA_TYPE_AUDIO){
        meta.setType(idx, "audio");
        meta.setRate(idx, strm->codecpar->sample_rate);
        meta.setSize(idx, strm->codecpar->frame_size);
        meta.setChannels(idx, strm->codecpar->channels);
      }
    }

    AVPacket packet;
    while (av_read_frame(pFormatCtx, &packet) >= 0){
      AVStream *strm = pFormatCtx->streams[packet.stream_index];
      long long packTime = (packet.dts * 1000 * strm->time_base.num / strm->time_base.den);
      long long packOffset = 0;
      bool isKey = false;
      if (packTime < 0){packTime = 0;}
      size_t idx = meta.trackIDToIndex(packet.stream_index);
      if (packet.flags & AV_PKT_FLAG_KEY && M.getType(idx) != "audio"){
        isKey = true;
      }
      if (packet.pts != AV_NOPTS_VALUE && packet.pts != packet.dts){
        packOffset = ((packet.pts - packet.dts) * 1000 * strm->time_base.num / strm->time_base.den);
      }
      meta.update(packTime, packOffset, idx, packet.size, packet.pos, isKey);
      av_packet_unref(&packet);
    }
    return true;
  }

  void inputAV::getNext(size_t wantIdx){
    AVPacket packet;
    while (av_read_frame(pFormatCtx, &packet) >= 0){
      // filter tracks we don't care about
      size_t idx = meta.trackIDToIndex(packet.stream_index);
      if (idx == INVALID_TRACK_ID){continue;}
      if (wantIdx != INVALID_TRACK_ID && idx != wantIdx){continue;}
      if (!userSelect.count(idx)){
        HIGH_MSG("Track %u not selected", packet.stream_index);
        continue;
      }
      AVStream *strm = pFormatCtx->streams[packet.stream_index];
      long long packTime = (packet.dts * 1000 * strm->time_base.num / strm->time_base.den);
      long long packOffset = 0;
      bool isKey = false;
      if (packTime < 0){packTime = 0;}
      if (packet.flags & AV_PKT_FLAG_KEY && M.getType(idx) != "audio"){
        isKey = true;
      }
      if (packet.pts != AV_NOPTS_VALUE && packet.pts != packet.dts){
        packOffset = ((packet.pts - packet.dts) * 1000 * strm->time_base.num / strm->time_base.den);
      }
      thisTime = packTime;
      thisIdx = idx;
      thisPacket.genericFill(packTime, packOffset, thisIdx,
                             (const char *)packet.data, packet.size, 0, isKey);
      av_packet_unref(&packet);
      return; // success!
    }
    thisPacket.null();
    preRun();
    // failure :-(
    FAIL_MSG("getNext failed");
  }

  void inputAV::seek(uint64_t seekTime, size_t idx){
    int stream_index = av_find_default_stream_index(pFormatCtx);
    // Convert ts to frame
    unsigned long long reseekTime =
        av_rescale(seekTime, pFormatCtx->streams[stream_index]->time_base.den,
                   pFormatCtx->streams[stream_index]->time_base.num);
    reseekTime /= 1000;
    unsigned long long seekStreamDuration = pFormatCtx->streams[stream_index]->duration;
    int flags = AVSEEK_FLAG_BACKWARD;
    if (reseekTime > 0 && reseekTime < seekStreamDuration){
      flags |= AVSEEK_FLAG_ANY; // H.264 I frames don't always register as "key frames" in FFmpeg
    }
    int ret = av_seek_frame(pFormatCtx, stream_index, reseekTime, flags);
    if (ret < 0){ret = av_seek_frame(pFormatCtx, stream_index, reseekTime, AVSEEK_FLAG_ANY);}
  }

}// namespace Mist
