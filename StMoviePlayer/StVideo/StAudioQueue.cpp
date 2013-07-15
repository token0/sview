/**
 * This source is a part of sView program.
 *
 * Copyright © Kirill Gavrilov, 2009-2013
 */

#include "StAudioQueue.h"

#include <StGL/StGLVec.h>
#include <StThreads/StThread.h>

/**
 * Check OpenAL state.
 */
#ifdef __ST_DEBUG__
void stalCheckErrors(const StString& theProcedure) {
#else
void stalCheckErrors(const StString& ) {
#endif
    ALenum error = alGetError();
    switch(error) {
        case AL_NO_ERROR: return; // alright
        case AL_INVALID_NAME:      ST_DEBUG_LOG(theProcedure + ": AL_INVALID_NAME");      return;
        case AL_INVALID_ENUM:      ST_DEBUG_LOG(theProcedure + ": AL_INVALID_ENUM");      return;
        case AL_INVALID_VALUE:     ST_DEBUG_LOG(theProcedure + ": AL_INVALID_VALUE");     return;
        case AL_INVALID_OPERATION: ST_DEBUG_LOG(theProcedure + ": AL_INVALID_OPERATION"); return;
        case AL_OUT_OF_MEMORY:     ST_DEBUG_LOG(theProcedure + ": AL_OUT_OF_MEMORY");     return;
        default:                   ST_DEBUG_LOG(theProcedure + ": OpenAL unknown error"); return;
    }
}

static const StGLVec3 POSITION_CENTER       ( 0.0f, 0.0f,  0.0f);
static const StGLVec3 POSITION_FRONT_LEFT   (-1.0f, 0.0f, -1.0f);
static const StGLVec3 POSITION_FRONT_CENTER ( 0.0f, 0.0f, -1.0f);
static const StGLVec3 POSITION_FRONT_RIGHT  ( 1.0f, 0.0f, -1.0f);
static const StGLVec3 POSITION_LFE          ( 0.0f, 0.0f,  0.0f);
static const StGLVec3 POSITION_REAR_LEFT    (-1.0f, 0.0f,  1.0f);
static const StGLVec3 POSITION_REAR_RIGHT   ( 1.0f, 0.0f,  1.0f);

/**
 * Check if dynamically linked version of FFmpeg libraries
 * is old or not.
 */
inline bool isReoderingNeededInit() {
    stLibAV::Version aVersion = stLibAV::Version::libavcodec();
    // I have no idea which version introduce this feature for ac3 and ogg streams...
    // but experimentally detect that FFmpeg 0.5.1 is old and FFmpeg 0.6 include this
    // We check libavcodec version here and hopes this will true for most packages
    bool isUpToDate = ( aVersion.myMajor  > 52) ||
                      ((aVersion.myMajor == 52) && (aVersion.myMinor >= 72));
    if(!isUpToDate) {
        ST_DEBUG_LOG("Used old FFmpeg, enabled sView channel reorder for multichannel AC3 and OGG Vorbis streams!");
    }
    return !isUpToDate;
}

inline bool isReoderingNeeded() {
    static bool isNeeded = isReoderingNeededInit();
    return isNeeded;
}

void StAudioQueue::stalConfigureSources1() {
    alSourcefv(myAlSources[0], AL_POSITION, POSITION_CENTER);
    stalCheckErrors("alSource*0");
}

void StAudioQueue::stalConfigureSources4_0() {
    alSourcefv(myAlSources[0], AL_POSITION, POSITION_FRONT_LEFT);
    alSourcefv(myAlSources[1], AL_POSITION, POSITION_FRONT_RIGHT);
    alSourcefv(myAlSources[2], AL_POSITION, POSITION_REAR_LEFT);
    alSourcefv(myAlSources[3], AL_POSITION, POSITION_REAR_RIGHT);
    stalCheckErrors("alSource*0123");
}

void StAudioQueue::stalConfigureSources5_1() {
    alSourcefv(myAlSources[0], AL_POSITION, POSITION_FRONT_LEFT);
    alSourcefv(myAlSources[1], AL_POSITION, POSITION_FRONT_RIGHT);
    alSourcefv(myAlSources[2], AL_POSITION, POSITION_FRONT_CENTER);
    alSourcefv(myAlSources[3], AL_POSITION, POSITION_LFE);
    alSourcefv(myAlSources[4], AL_POSITION, POSITION_REAR_LEFT);
    alSourcefv(myAlSources[5], AL_POSITION, POSITION_REAR_RIGHT);
    stalCheckErrors("alSource*012345");
}

bool StAudioQueue::stalInit() {
    StHandle<StString> aDevName = myAlDeviceName;
    if(!myAlCtx.create(*aDevName)) {
        if(!myAlCtx.create()) {
            // retry with default device
            return false;
        }
    }
    myAlCtx.makeCurrent();

    alGetError(); // clear error code

    // generate the buffers
    for(size_t srcId = 0; srcId < NUM_AL_SOURCES; ++srcId) {
        alGenBuffers(NUM_AL_BUFFERS, &myAlBuffers[srcId][0]);
        stalCheckErrors(StString("alGenBuffers") + srcId);
    }

    // generate the sources
    alGenSources(NUM_AL_SOURCES, myAlSources);
    stalCheckErrors("alGenSources");

    // configure sources
    const StGLVec3 aZeroVec(0.0f);
    for(size_t srcId = 0; srcId < NUM_AL_SOURCES; ++srcId) {
        alSourcefv(myAlSources[srcId], AL_POSITION,        aZeroVec);
        alSourcefv(myAlSources[srcId], AL_VELOCITY,        aZeroVec);
        alSourcefv(myAlSources[srcId], AL_DIRECTION,       aZeroVec);
        alSourcef (myAlSources[srcId], AL_ROLLOFF_FACTOR,  0.0f);
        alSourcei (myAlSources[srcId], AL_SOURCE_RELATIVE, AL_TRUE);
        alSourcef (myAlSources[srcId], AL_GAIN,            1.0f);
        stalCheckErrors(StString("alSource*") + srcId);
    }

    // configure listener
    const StGLVec3 aListenerOri[2] = { -StGLVec3::DZ(),    // forward
                                        StGLVec3::DY()  }; // up
    alListenerfv(AL_POSITION,    aZeroVec);
    alListenerfv(AL_VELOCITY,    aZeroVec);
    alListenerfv(AL_ORIENTATION, (const ALfloat* )aListenerOri);
    alListenerf (AL_GAIN,        myAlGain); // apply gain to all sources at-once
    return true;
}

void StAudioQueue::stalDeinit() {
    // clear buffers
    stalEmpty();
    alSourceStopv(NUM_AL_SOURCES, myAlSources);

    alDeleteSources(NUM_AL_SOURCES, myAlSources);
    stalCheckErrors("alDeleteSources");

    for(size_t srcId = 0; srcId < NUM_AL_SOURCES; ++srcId) {
        alDeleteBuffers(NUM_AL_BUFFERS, &myAlBuffers[srcId][0]);
        stalCheckErrors(StString("alDeleteBuffers") + srcId);
    }

    // close device
    myAlCtx.destroy();
}

void StAudioQueue::stalEmpty() {
    alSourceStopv(NUM_AL_SOURCES, myAlSources);

    ALint aBufQueued = 0;
    ALuint alBuffIdToUnqueue = 0;
    for(size_t aSrcId = 0; aSrcId < NUM_AL_SOURCES; ++aSrcId) {
        alGetSourcei(myAlSources[aSrcId], AL_BUFFERS_QUEUED, &aBufQueued);
        for(ALint aBufIter = 0; aBufIter < aBufQueued; ++aBufIter) {
            alSourceUnqueueBuffers(myAlSources[aSrcId], 1, &alBuffIdToUnqueue);
            stalCheckErrors(StString("alSourceUnqueueBuffers") + aSrcId);
        }
        alSourcei(myAlSources[aSrcId], AL_BUFFER, 0);
    }
    ///alSourceRewindv(NUM_AL_SOURCES, myAlSources);
}

ALenum StAudioQueue::stalGetSourceState() {
    ALenum aState;
    alGetSourcei(myAlSources[0], AL_SOURCE_STATE, &aState);
    if(myDbgPrevSrcState != aState) {
        switch(aState) {
            case AL_INITIAL: ST_DEBUG_LOG("OpenAL source state: INITIAL"); break;
            case AL_PLAYING: ST_DEBUG_LOG("OpenAL source state: PLAYING"); break;
            case AL_PAUSED:  ST_DEBUG_LOG("OpenAL source state: PAUSED") ; break;
            case AL_STOPPED: ST_DEBUG_LOG("OpenAL source state: STOPPED"); break;
            default:         ST_DEBUG_LOG("OpenAL source state: UNKNOWN"); break;
        }
    }
    myDbgPrevSrcState = aState;
    return aState;
}

/**
 * Simple thread function which just call decodeLoop().
 */
static SV_THREAD_FUNCTION threadFunction(void* audioQueue) {
    StAudioQueue* stAudioQueue = (StAudioQueue* )audioQueue;
    stAudioQueue->decodeLoop();
    return SV_THREAD_RETURN 0;
}

/**
 * 1 second of 48khz 32bit audio (old AVCODEC_MAX_AUDIO_FRAME_SIZE).
 */
#define ST_MAX_AUDIO_FRAME_SIZE 192000

StAudioQueue::StAudioQueue(const StString& theAlDeviceName)
: StAVPacketQueue(512),
  myAlDataLoop(),
  myPlaybackTimer(false),
  myDowntimeEvent(true),
  myBufferSrc(PCM16_SIGNED, ST_MAX_AUDIO_FRAME_SIZE),
  myBufferOut(PCM16_SIGNED, ST_MAX_AUDIO_FRAME_SIZE),
  myIsAlValid(ST_AL_INIT_NA),
  myToSwitchDev(false),
  myIsDisconnected(false),
  myAlDeviceName(new StString(theAlDeviceName)),
  myAlCtx(),
  myAlFormat(AL_FORMAT_STEREO16),
  myPrevFormat(AL_FORMAT_STEREO16),
  myPrevFrequency(0),
  myAlGain(1.0f),
  myAlGainPrev(1.0f),
  myDbgPrevQueued(-1),
  myDbgPrevSrcState(-1) {
    stMemSet(myAlSources, 0, sizeof(myAlSources));

    // launch thread parse incoming packets from queue
    myThread = new StThread(threadFunction, (void* )this);
}

StAudioQueue::~StAudioQueue() {
    myToQuit = true;
    pushQuit();

    myThread->wait();
    myThread.nullify();

    deinit();
}

bool StAudioQueue::init(AVFormatContext*   theFormatCtx,
                        const unsigned int theStreamId) {
    while(myIsAlValid == ST_AL_INIT_NA) {
        StThread::sleep(10);
        continue;
    }

    if(myIsAlValid != ST_AL_INIT_OK) {
        signals.onError(stCString("OpenAL: no playback device available"));
        deinit();
        return false;
    }

    if(!StAVPacketQueue::init(theFormatCtx, theStreamId)
    || myCodecCtx->codec_type != AVMEDIA_TYPE_AUDIO) {
        signals.onError(stCString("FFmpeg: invalid stream"));
        deinit();
        return false;
    }

    // get AUDIO codec
    myCodec = avcodec_find_decoder(myCodecCtx->codec_id);
    if(myCodec == NULL) {
        signals.onError(stCString("FFmpeg: audio codec not found"));
        deinit();
        return false;
    }
#if(LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(53, 8, 0))
    if(avcodec_open2(myCodecCtx, myCodec, NULL) < 0) {
#else
    if(avcodec_open(myCodecCtx, myCodec) < 0) {
#endif
        signals.onError(stCString("FFmpeg: could not open audio codec"));
        deinit();
        return false;
    }

    // setup source sample format (bitness)
    if(myCodecCtx->sample_fmt == stLibAV::audio::SAMPLE_FMT::U8) {
        myBufferSrc.setFormat(PCM8_UNSIGNED);
    } else if(myCodecCtx->sample_fmt == stLibAV::audio::SAMPLE_FMT::S16) {
        myBufferSrc.setFormat(PCM16_SIGNED);
    } else if(myCodecCtx->sample_fmt == stLibAV::audio::SAMPLE_FMT::S32) {
        myBufferSrc.setFormat(PCM32_SIGNED);
    } else if(myCodecCtx->sample_fmt == stLibAV::audio::SAMPLE_FMT::FLT) {
        myBufferSrc.setFormat(PCM32FLOAT);
    } else if(myCodecCtx->sample_fmt == stLibAV::audio::SAMPLE_FMT::DBL) {
        myBufferSrc.setFormat(PCM64FLOAT);
    } else {
        signals.onError(StString("Audio sample format '") + stLibAV::audio::getSampleFormatString(myCodecCtx)
                      + "' not supported");
        deinit();
        return false;
    }

    // setup frequency
    myBufferSrc.setFreq(myCodecCtx->sample_rate);
    myBufferOut.setFreq(myCodecCtx->sample_rate);

    // setup channel order
    if(myCodecCtx->channels == 1) {
        if(myBufferSrc.getFormat() == PCM8_UNSIGNED) {
            // uint8_t is supported by OpenAL core
            myAlFormat = AL_FORMAT_MONO8;
            myBufferOut.setFormat(PCM8_UNSIGNED);
        } else if(myBufferSrc.getFormat() == PCM64FLOAT && myAlCtx.hasExtFloat64) {
            // use float64 extension
            myAlFormat = alGetEnumValue("AL_FORMAT_MONO_DOUBLE_EXT");
            myBufferOut.setFormat(PCM64FLOAT);
        } else if((myBufferSrc.getFormat() == PCM32_SIGNED ||
                   myBufferSrc.getFormat() == PCM32FLOAT ||
                   myBufferSrc.getFormat() == PCM64FLOAT)
               && myAlCtx.hasExtFloat32) {
            // use float32 extension to reduce quality degradation
            myAlFormat = alGetEnumValue("AL_FORMAT_MONO_FLOAT32");
            myBufferOut.setFormat(PCM32FLOAT);
        } else {
            // default - int16_t
            myAlFormat = AL_FORMAT_MONO16;
            myBufferOut.setFormat(PCM16_SIGNED);
        }

        myBufferSrc.setupChannels(StChannelMap::CH10, StChannelMap::PCM, 1);
        myBufferOut.setupChannels(StChannelMap::CH10, StChannelMap::PCM, 1);
        stalConfigureSources1();
    } else if(myCodecCtx->channels == 2) {
        if(myBufferSrc.getFormat() == PCM8_UNSIGNED) {
            // uint8_t is supported by OpenAL core
            myAlFormat = AL_FORMAT_STEREO8;
            myBufferOut.setFormat(PCM8_UNSIGNED);
        } else if(myBufferSrc.getFormat() == PCM64FLOAT && myAlCtx.hasExtFloat64) {
            // use float64 extension
            myAlFormat = alGetEnumValue("AL_FORMAT_STEREO_DOUBLE_EXT");
            myBufferOut.setFormat(PCM64FLOAT);
        } else if((myBufferSrc.getFormat() == PCM32_SIGNED ||
                   myBufferSrc.getFormat() == PCM32FLOAT ||
                   myBufferSrc.getFormat() == PCM64FLOAT)
               && myAlCtx.hasExtFloat32) {
            // use float32 extension to reduce quality degradation
            myAlFormat = alGetEnumValue("AL_FORMAT_STEREO_FLOAT32");
            myBufferOut.setFormat(PCM32FLOAT);
        } else {
            // default - int16_t
            myAlFormat = AL_FORMAT_STEREO16;
            myBufferOut.setFormat(PCM16_SIGNED);
        }

        myBufferSrc.setupChannels(StChannelMap::CH20, StChannelMap::PCM, 1);
        myBufferOut.setupChannels(StChannelMap::CH20, StChannelMap::PCM, 1);
        stalConfigureSources1();
    } else if(myCodecCtx->channels == 4) {
        if(myAlCtx.hasExtMultiChannel) {
            if(myBufferSrc.getFormat() == PCM8_UNSIGNED) {
                myAlFormat = alGetEnumValue("AL_FORMAT_QUAD8");
                myBufferOut.setFormat(PCM8_UNSIGNED);
            } else if(myBufferSrc.getFormat() == PCM32_SIGNED ||
                      myBufferSrc.getFormat() == PCM32FLOAT ||
                      myBufferSrc.getFormat() == PCM64FLOAT) {
                // use float32 format to reduce quality degradation
                myAlFormat = alGetEnumValue("AL_FORMAT_QUAD32");
                myBufferOut.setFormat(PCM32FLOAT);
            } else {
                // default - int16_t
                myAlFormat = alGetEnumValue("AL_FORMAT_QUAD16");
                myBufferOut.setFormat(PCM16_SIGNED);
            }

            myBufferSrc.setupChannels(StChannelMap::CH40, StChannelMap::PCM, 1);
            myBufferOut.setupChannels(StChannelMap::CH40, StChannelMap::PCM, 1);
            stalConfigureSources1();
        } else {
            signals.onError(stCString("OpenAL: multichannel extension (AL_FORMAT_QUAD16) not available"));
            deinit();
            return false;
        }
    } else if(myCodecCtx->channels == 6) {
        if(myAlCtx.hasExtMultiChannel) {
            if(myBufferSrc.getFormat() == PCM8_UNSIGNED) {
                myAlFormat = alGetEnumValue("AL_FORMAT_51CHN8");
                myBufferOut.setFormat(PCM8_UNSIGNED);
            } else if(myBufferSrc.getFormat() == PCM32_SIGNED ||
                      myBufferSrc.getFormat() == PCM32FLOAT ||
                      myBufferSrc.getFormat() == PCM64FLOAT) {
                // use float32 format to reduce quality degradation
                myAlFormat = alGetEnumValue("AL_FORMAT_51CHN32");
                myBufferOut.setFormat(PCM32FLOAT);
            } else {
                // default - int16_t
                myAlFormat = alGetEnumValue("AL_FORMAT_51CHN16");
                myBufferOut.setFormat(PCM16_SIGNED);
            }

            myBufferOut.setupChannels(StChannelMap::CH51, StChannelMap::PCM, 1);
            if(isReoderingNeeded()) {
                // workaround for old FFmpeg
                if(myCodec->id == CODEC_ID_AC3) {
                    myBufferSrc.setupChannels(StChannelMap::CH51, StChannelMap::AC3, 1);
                } else if(myCodec->id == CODEC_ID_VORBIS) {
                    myBufferSrc.setupChannels(StChannelMap::CH51, StChannelMap::OGG, 1);
                } else {
                    myBufferSrc.setupChannels(StChannelMap::CH51, StChannelMap::PCM, 1);
                }
            } else {
                myBufferSrc.setupChannels(StChannelMap::CH51, StChannelMap::PCM, 1);
            }
            stalConfigureSources1();
        } else {
            if(myBufferSrc.getFormat() == PCM8_UNSIGNED) {
                myAlFormat = AL_FORMAT_MONO8;
                myBufferOut.setFormat(PCM8_UNSIGNED);
            } else if(myBufferSrc.getFormat() == PCM64FLOAT && myAlCtx.hasExtFloat64) {
                // use float64 extension
                myAlFormat = alGetEnumValue("AL_FORMAT_MONO_DOUBLE_EXT");
                myBufferOut.setFormat(PCM64FLOAT);
            } else if((myBufferSrc.getFormat() == PCM32_SIGNED ||
                       myBufferSrc.getFormat() == PCM32FLOAT ||
                       myBufferSrc.getFormat() == PCM64FLOAT)
                   && myAlCtx.hasExtFloat32) {
                // use float32 extension to reduce quality degradation
                myAlFormat = alGetEnumValue("AL_FORMAT_MONO_FLOAT32");
                myBufferOut.setFormat(PCM32FLOAT);
            } else {
                // default - int16_t
                myAlFormat = AL_FORMAT_MONO16;
                myBufferOut.setFormat(PCM16_SIGNED);
            }

            myBufferOut.setupChannels(StChannelMap::CH51, StChannelMap::PCM, 6);
            if(isReoderingNeeded()) {
                // workaround for old FFmpeg
                if(myCodec->id == CODEC_ID_AC3) {
                    myBufferSrc.setupChannels(StChannelMap::CH51, StChannelMap::AC3, 1);
                } else if(myCodec->id == CODEC_ID_VORBIS) {
                    myBufferSrc.setupChannels(StChannelMap::CH51, StChannelMap::OGG, 1);
                } else {
                    myBufferSrc.setupChannels(StChannelMap::CH51, StChannelMap::PCM, 1);
                }
            } else {
                myBufferSrc.setupChannels(StChannelMap::CH51, StChannelMap::PCM, 1);
            }
            stalConfigureSources5_1();
            ST_DEBUG_LOG("OpenAL: multichannel extension (AL_FORMAT_51CHN16) not available");
        }
    }
    return true;
}

void StAudioQueue::deinit() {
    myBufferSrc.clear();
    myBufferOut.clear();
    StAVPacketQueue::deinit();
}

bool StAudioQueue::parseEvents() {
    double aPtsSeek = 0.0;

    if(myToSwitchDev) {
        stalDeinit(); // release OpenAL context
        myIsAlValid = (stalInit() ? ST_AL_INIT_OK : ST_AL_INIT_KO);

        myIsDisconnected = false;
        myToSwitchDev    = false;
        return true;
    }

    if(!stAreEqual(myAlGain, myAlGainPrev, 1.e-7f)) {
        ST_DEBUG_LOG("Audio volume changed from " + myAlGainPrev + " to " + myAlGain);
        myAlGainPrev = myAlGain;
        alListenerf(AL_GAIN, myAlGain); // apply gain to all sources at-once
    }

    switch(popPlayEvent(aPtsSeek)) {
        case ST_PLAYEVENT_PLAY: {
            stalEmpty();
            playTimerStart(0.0);
            playTimerPause();
            return false;
        }
        case ST_PLAYEVENT_STOP: {
            playTimerPause();
            stalEmpty();
            return false;
        }
        case ST_PLAYEVENT_PAUSE: {
            playTimerPause();
            alSourcePausev(NUM_AL_SOURCES, myAlSources);
            return false;
        }
        case ST_PLAYEVENT_RESUME: {
            playTimerResume();
            alSourcePlayv(NUM_AL_SOURCES, myAlSources);
            return false;
        }
        case ST_PLAYEVENT_SEEK: {
            stalEmpty();
            playTimerStart(aPtsSeek);
            playTimerPause();
            myBufferSrc.setDataSize(0);
            myBufferOut.setDataSize(0);
            // return special flag to skip "resume playback from" in loop
            return true;
        }
        case ST_PLAYEVENT_NONE:
        default:
            return false;
    }
}

bool StAudioQueue::stalQueue(const double thePts) {
    ALint aQueued = 0;
    ALint aProcessed = 0;
    ALenum aState = stalGetSourceState();
    alGetSourcei(myAlSources[0], AL_BUFFERS_PROCESSED, &aProcessed);
    alGetSourcei(myAlSources[0], AL_BUFFERS_QUEUED,    &aQueued);

#if defined(__ST_DEBUG__)
    if(myDbgPrevQueued != aQueued) {
        ST_DEBUG_LOG("OpenAL buffers: " + aQueued + " queued + "
            + aProcessed + " processed from " + NUM_AL_BUFFERS
        );
    }
    myDbgPrevQueued = aQueued;
#endif

    if((aState == AL_PLAYING
     || aState == AL_PAUSED)
    && (myPrevFormat    != myAlFormat
     || myPrevFrequency != myBufferOut.getFreq()))
    {
        return false; // wait until tail of previous stream played
    }

    if(myPrevFormat    != myAlFormat
    || myPrevFrequency != myBufferOut.getFreq()
    || (aState  == AL_STOPPED
     && aQueued == NUM_AL_BUFFERS)) {
        ST_DEBUG_LOG("AL, reinitialize buffers per source , size= " + myBufferOut.getDataSize(0)
                            + "; freq= " + myBufferOut.getFreq());
        stalEmpty();
        stalCheckErrors("reset state");
        aProcessed = 0;
        aQueued = 0;
    }

    bool toTryToPlay = false;
    bool isQueued = false;
    if(aProcessed == 0 && aQueued < NUM_AL_BUFFERS) {
        stalCheckErrors("reset state");
        ///ST_DEBUG_LOG("AL, queue more buffers " + aQueued + " / " + NUM_AL_BUFFERS);
        myPrevFormat    = myAlFormat;
        myPrevFrequency = myBufferOut.getFreq();
        for(size_t aSrcId = 0; aSrcId < myBufferOut.getSourcesCount(); ++aSrcId) {
            alBufferData(myAlBuffers[aSrcId][aQueued], myAlFormat,
                         myBufferOut.getData(aSrcId), (ALsizei )myBufferOut.getDataSize(aSrcId),
                         myBufferOut.getFreq());
            stalCheckErrors("alBufferData");
            alSourceQueueBuffers(myAlSources[aSrcId], 1, &myAlBuffers[aSrcId][aQueued]);
            stalCheckErrors("alSourceQueueBuffers");
        }
        toTryToPlay = ((aQueued + 1) == NUM_AL_BUFFERS);
        isQueued = true;
    } else if(aProcessed != 0
           && (aState == AL_PLAYING
            || aState == AL_PAUSED)) {
        ALuint alBuffIdToFill = 0;
        ///ST_DEBUG_LOG("queue buffer " + thePts + "; state= " + stalGetSourceState());
        if(myBufferOut.getDataSize(0) == 0) {
            ST_DEBUG_LOG(" EMPTY BUFFER ");
            return true;
        }

        myPrevFormat    = myAlFormat;
        myPrevFrequency = myBufferOut.getFreq();
        for(size_t aSrcId = 0; aSrcId < myBufferOut.getSourcesCount(); ++aSrcId) {

            // wait other sources for processed buffers
            if(aSrcId != 0) {
                myLimitTimer.restart();
                for(;;) {
                    alGetSourcei(myAlSources[aSrcId], AL_BUFFERS_PROCESSED, &aProcessed);
                    if(aProcessed != 0) {
                        break;
                    }
                    if(myLimitTimer.getElapsedTimeInSec() > 2.0) {
                        // just avoid dead loop - should never happens
                        return false;
                    }
                    StThread::sleep(10);
                }
            }

            alSourceUnqueueBuffers(myAlSources[aSrcId], 1, &alBuffIdToFill);
            stalCheckErrors("alSourceUnqueueBuffers");
            if(alBuffIdToFill != 0) {
                alBufferData(alBuffIdToFill, myAlFormat,
                             myBufferOut.getData(aSrcId), (ALsizei )myBufferOut.getDataSize(aSrcId),
                             myBufferOut.getFreq());
                stalCheckErrors("alBufferData");
                alSourceQueueBuffers(myAlSources[aSrcId], 1, &alBuffIdToFill);
                stalCheckErrors("alSourceQueueBuffers");
            } else {
                ST_DEBUG_LOG("OpenAL, unqueue FAILED");
            }
        }
        toTryToPlay = true;
        isQueued = true;
    }

    if(aState == AL_STOPPED
    && toTryToPlay) {
        double diffSecs = double(myAlDataLoop.summ() + myBufferOut.getDataSizeWhole()) / double(myBufferOut.getSecondSize());
        if((thePts - diffSecs) < 100000.0) {
            playTimerStart(thePts - diffSecs);
        } else {
            playTimerStart(0.0);
        }
        alSourcePlayv(NUM_AL_SOURCES, myAlSources);
        if(stalCheckConnected()) {
            ST_DEBUG_LOG("!!! OpenAL was in stopped state, now resume playback from " + (thePts - diffSecs));
        }

        // pause playback if not in playing state
        myEventMutex.lock();
        const bool toPause = !myIsPlaying;
        myEventMutex.unlock();
        if(toPause) {
            alSourcePausev(NUM_AL_SOURCES, myAlSources);
        }
    }

    return isQueued;
}

bool StAudioQueue::stalCheckConnected() {
    if(!myIsDisconnected && myAlCtx.isConnected()) {
        return true;
    }

    myAlDeviceName = new StString();
    stalDeinit(); // release OpenAL context
    myIsAlValid = (stalInit() ? ST_AL_INIT_OK : ST_AL_INIT_KO);
    myIsDisconnected = true;
    ST_DEBUG_LOG("!!! OpenAL device was disconnected !!!");
    return false;
}

void StAudioQueue::stalFillBuffers(const double thePts,
                                   const bool   toIgnoreEvents) {
    if(!toIgnoreEvents) {
        parseEvents();
    }

    bool toSkipPlaybackFrom = false;
    while(!stalQueue(thePts)) {
        // AL queue is full
        if(!toIgnoreEvents) {
            toSkipPlaybackFrom = parseEvents();
        }
        if(myToQuit) {
            return;
        }

        if(!toSkipPlaybackFrom && !stalIsAudioPlaying() && isPlaying()) {
            // this position means:
            // 1) buffers were empty and playback was stopped
            //    now we have all buffers full and could play them
            double diffSecs = double(myAlDataLoop.summ() + myBufferOut.getDataSizeWhole()) / double(myBufferOut.getSecondSize());
            if((thePts - diffSecs) < 100000.0) {
                playTimerStart(thePts - diffSecs);
            } else {
                playTimerStart(0.0);
            }
            alSourcePlayv(NUM_AL_SOURCES, myAlSources);
            if(stalCheckConnected()) {
                ST_DEBUG_LOG("!!! OpenAL was in stopped state, now resume playback from " + (thePts - diffSecs));
            }
        } else {
            // TODO (Kirill Gavrilov#3#) often updates may prevent normal video playback
            // on files with broken audio/video PTS
            ALfloat aPos = 0.0f;
            alGetSourcef(myAlSources[0], AL_SEC_OFFSET, &aPos);
            double diffSecs = double(myAlDataLoop.summ() + myBufferOut.getDataSizeWhole()) / double(myBufferOut.getSecondSize());
            diffSecs -= aPos;
            if((thePts - diffSecs) < 100000.0) {
                 static double oldPts = 0.0;
                 if(thePts != oldPts) {
                    /**ST_DEBUG_LOG("set AAApts " + (thePts - diffSecs)
                        + " from " + getPts()
                        + "(" + thePts + ", " + diffSecs + ")"
                    );*/
                    playTimerStart(thePts - diffSecs);
                    oldPts = thePts;
                }
                ///playTimerStart(thePts - diffSecs);
            }
        }
        StThread::sleep(1);
    }
}

void StAudioQueue::decodePacket(const StHandle<StAVPacket>& thePacket, double& thePts) {

    const uint8_t* audio_pkt_data = thePacket->getData();
    int audio_pkt_size = thePacket->getSize();
    int len1 = 0;
    int dataSize = 0;
    bool checkMoreFrames = false;
    double aNewPts = 0.0;
    // packet could store multiple frames
    for(;;) {
        while(audio_pkt_size > 0) {
            dataSize = (int )myBufferSrc.getBufferSizeWhole();

        #if(LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(52, 23, 0))
            StAVPacket anAvPkt;
            anAvPkt.getAVpkt()->data = (uint8_t* )audio_pkt_data;
            anAvPkt.getAVpkt()->size = audio_pkt_size;
            len1 = avcodec_decode_audio3(myCodecCtx, (int16_t* )myBufferSrc.getData(), &dataSize, anAvPkt.getAVpkt());
        #else
            len1 = avcodec_decode_audio2(myCodecCtx,
                                         (int16_t* )myBufferSrc.getData(), &dataSize,
                                         audio_pkt_data, audio_pkt_size);
        #endif
            if(len1 < 0) {
                // if error, we skip the frame
                audio_pkt_size = 0;
                break;
            }

            audio_pkt_data += len1;
            audio_pkt_size -= len1;
            if(dataSize <= 0) {
                continue;
            }

            checkMoreFrames = true;
            myBufferSrc.setDataSize(dataSize);

            if(myBufferOut.addData(myBufferSrc)) {
                // 'big buffer' still not full
                break;
            }

            if(thePacket->getPts() != stLibAV::NOPTS_VALUE) {
                aNewPts = unitsToSeconds(thePacket->getPts()) - myPtsStartBase;
                if(aNewPts <= thePts) {
                    ST_DEBUG_LOG("Got the AUDIO packet with pts in past; "
                        + "new PTS= "   + aNewPts
                        + "; old PTS= " + thePts
                    );
                }
                thePts = aNewPts;
            }

            // now fill OpenAL buffers
            stalFillBuffers(thePts, false);
            if(myToQuit) {
                return;
            }

            // save the history for filled AL buffers sizes
            myAlDataLoop.push(myBufferOut.getDataSizeWhole());

            // clear 'big' buffer
            myBufferOut.setDataSize(0);
            myBufferOut.addData(myBufferSrc);
            break;
        }

        if(checkMoreFrames) {
            checkMoreFrames = false;
            continue;
        }
        break;
    }

}

void StAudioQueue::decodeLoop() {
    myIsAlValid = (stalInit() ? ST_AL_INIT_OK : ST_AL_INIT_KO);

    double aPts = 0.0;
    StHandle<StAVPacket> aPacket;
    for(;;) {
        // wait for upcoming packets
        if(isEmpty()) {
            myDowntimeEvent.set();
            parseEvents();
            StThread::sleep(10);
            ///ST_DEBUG_LOG_AT("AQ is empty");
            continue;
        }
        myDowntimeEvent.reset();

        aPacket = pop();
        if(aPacket.isNull()) {
            continue;
        }
        switch(aPacket->getType()) {
            case StAVPacket::FLUSH_PACKET: {
                // got the special FLUSH packet - flush FFmpeg codec buffers
                if(myCodecCtx != NULL && myCodec != NULL) {
                    avcodec_flush_buffers(myCodecCtx);
                }
                // at this moment we clear current data from our buffers too
                myBufferOut.setDataSize(0);
                myBufferSrc.setDataSize(0);
                stalEmpty();
                continue;
            }
            case StAVPacket::START_PACKET: {
                playTimerStart(myPtsStartStream - myPtsStartBase);
                aPts = 0.0;
                continue;
            }
            case StAVPacket::END_PACKET: {
                pushPlayEvent(ST_PLAYEVENT_NONE);
                // TODO (Kirill Gavrilov#3#) improve file-by-file playback
                if(myBufferOut.getDataSize(0) != 0) {
                    stalFillBuffers(aPts, true);
                }
                myBufferOut.setDataSize(0);
                myBufferSrc.setDataSize(0);
                if(myToQuit) {
                    stalDeinit(); // release OpenAL context
                    return;
                }
                continue;
            }
            case StAVPacket::QUIT_PACKET: {
                stalDeinit(); // release OpenAL context
                return;
            }
        }

        // we got the data packet, so decode it
        decodePacket(aPacket, aPts);
        aPacket.nullify();
    }
}

void StAudioQueue::pushPlayEvent(const StPlayEvent_t theEventId,
                                 const double        theSeekParam) {
    myEventMutex.lock();
    StAVPacketQueue::pushPlayEvent(theEventId, theSeekParam);
    if(theEventId == ST_PLAYEVENT_SEEK) {
        myPlaybackTimer.restart(theSeekParam * 1000000.0);
    }
    myEventMutex.unlock();
}
