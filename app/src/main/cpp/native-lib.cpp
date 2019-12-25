#include <jni.h>
#include <string>
#include <android/log.h>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <pthread.h>



/**
 *  external fun createBufferQueueAudioPlayer(sampleRate: Int, samplesPerBuf: Int)
    external fun createAssetAudioPlayer(
        assetManager: AssetManager?,
        filename: String?
    ): Boolean

    // true == PLAYING, false == PAUSED
    external fun setPlayingAssetAudioPlayer(isPlaying: Boolean)

    external fun createUriAudioPlayer(uri: String?): Boolean
    external fun setPlayingUriAudioPlayer(isPlaying: Boolean)
    external fun setLoopingUriAudioPlayer(isLooping: Boolean)
    external fun setChannelMuteUriAudioPlayer(chan: Int, mute: Boolean)
    external fun setChannelSoloUriAudioPlayer(chan: Int, solo: Boolean)
    external fun getNumChannelsUriAudioPlayer(): Int
    external fun setVolumeUriAudioPlayer(millibel: Int)
    external fun setMuteUriAudioPlayer(mute: Boolean)
    external fun enableStereoPositionUriAudioPlayer(enable: Boolean)
    external fun setStereoPositionUriAudioPlayer(permille: Int)
    external fun selectClip(which: Int, count: Int): Boolean
    external fun enableReverb(enabled: Boolean): Boolean
    external fun createAudioRecorder(): Boolean
    external fun startRecording()
    external fun shutdown()
 */

#define TAG "OPENSLES"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,TAG,__VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR,TAG,__VA_ARGS__)
static const char *classpath = "com/james/opensles/OpenslesHelper";


JavaVM *global_vm;
JNIEnv *global_env;
SLObjectItf slRecorderObject;
SLRecordItf slRecordItf;
SLEngineItf slEngineItf;
SLObjectItf slengineObject;
SLObjectItf outputMixItf;
SLEnvironmentalReverbItf slEnvironmentalReverbItf = nullptr;
SLAndroidSimpleBufferQueueItf slbpPlayerBufferQueue;
SLAndroidSimpleBufferQueueItf slbpRecorderBufferQueue;
SLEnvironmentalReverbSettings slEnvironmentalReverbSettings;
SLObjectItf bpPlayerObject;
SLVolumeItf slVolumeItf;
SLEffectSendItf slEffectSendItf;
SLPlayItf pPlayer;
static SLmilliHertz bqPlayerSampleRate = 0;
static jint bqPlayerBufSize = 0;


// synthesized sawtooth clip
#define SAWTOOTH_FRAMES 8000
static short sawtoothBuffer[SAWTOOTH_FRAMES];

pthread_mutex_t audioEngineLock = PTHREAD_MUTEX_INITIALIZER;

// 5 seconds of recorded audio at 16 kHz mono, 16-bit signed little endian
#define RECORDER_FRAMES (16000 * 5)
static short recorderBuffer[RECORDER_FRAMES];
static unsigned recorderSize = 0;
static short *resampleBuf = NULL;

// pointer and size of the next player buffer to enqueue, and number of remaining buffers
static short *nextBuffer;
static unsigned nextSize;
static int nextCount;

/**
 *
 * 流程：
 *  1.createEngine
 *  2.Realize 实例化对象
 *  3.通过对象Getinterface 获取interface对象
 *  4.通过interface 对象 可以调用接口对象中的方法
 *
 *
 *
 */

extern "C"
JNIEXPORT void createEngine(JNIEnv *env, jobject instance) {
    SLresult lresult;
    lresult = slCreateEngine(&slengineObject, 0, NULL, 0, NULL, NULL);
    (void) lresult;
    lresult = (*slengineObject)->Realize(slengineObject, SL_BOOLEAN_FALSE);
    (void) lresult;
    lresult = (*slengineObject)->GetInterface(slengineObject, SL_IID_ENGINE, &slEngineItf);
    (void) lresult;


    SLInterfaceID slInterfaceId[1] = {SL_IID_ENVIRONMENTALREVERB};
    SLboolean slb[1] = {SL_BOOLEAN_FALSE};
    lresult = (*slEngineItf)->CreateOutputMix(slEngineItf, &outputMixItf, 1, slInterfaceId, slb);
    (void) lresult;

    lresult = (*outputMixItf)->Realize(outputMixItf, SL_BOOLEAN_FALSE);
    (void) lresult;

    lresult = (*outputMixItf)->GetInterface(outputMixItf, SL_IID_ENVIRONMENTALREVERB,
                                            &slEnvironmentalReverbItf);

    //(*slEnvironmentalReverbItf)->GetDensity
//    (*slEnvironmentalReverbItf)->GetDecayTime

    (void) lresult;

    if (SL_RESULT_SUCCESS == lresult) {
        lresult = (*slEnvironmentalReverbItf)->SetEnvironmentalReverbProperties(
                slEnvironmentalReverbItf, &slEnvironmentalReverbSettings);
        (void) lresult;
    }
}

/*
 * Only support up-sampling
 */
short *createResampledBuf(uint32_t idx, uint32_t srcRate, unsigned *size) {
    short *src = NULL;
    short *workBuf;
    int upSampleRate;
    int32_t srcSampleCount = 0;

    if (0 == bqPlayerSampleRate) {
        return NULL;
    }
    if (bqPlayerSampleRate % srcRate) {
        /*
         * simple up-sampling, must be divisible
         */
        return NULL;
    }
    upSampleRate = bqPlayerSampleRate / srcRate;

    switch (idx) {
        case 0:
            return NULL;
//        case 1: // HELLO_CLIP
//            srcSampleCount = sizeof(hello) >> 1;
//            src = (short*)hello;
//            break;
//        case 2: // ANDROID_CLIP
//            srcSampleCount = sizeof(android) >> 1;
//            src = (short*) android;
//            break;
        case 3: // SAWTOOTH_CLIP
            srcSampleCount = SAWTOOTH_FRAMES;
            src = sawtoothBuffer;
            break;
        case 4: // captured frames
            srcSampleCount = recorderSize / sizeof(short);
            src = recorderBuffer;
            break;
        default:
            assert(0);
            return NULL;
    }

    resampleBuf = (short *) malloc((srcSampleCount * upSampleRate) << 1);
    if (resampleBuf == NULL) {
        return resampleBuf;
    }
    workBuf = resampleBuf;
    for (int sample = 0; sample < srcSampleCount; sample++) {
        for (int dup = 0; dup < upSampleRate; dup++) {
            *workBuf++ = src[sample];
        }
    }

    *size = (srcSampleCount * upSampleRate) << 1;     // sample format is 16 bit
    return resampleBuf;
}


void releaseResampleBuf(void) {
    if (0 == bqPlayerSampleRate) {
        /*
         * we are not using fast path, so we were not creating buffers, nothing to do
         */
        return;
    }

    free(resampleBuf);
    resampleBuf = NULL;
}


void bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *context) {
    assert(NULL == context);
    // for streaming playback, replace this test by logic to find and fill the next buffer
    if (--nextCount > 0 && NULL != nextBuffer && 0 != nextSize) {
        SLresult result;
        // enqueue another buffer
        result = (*slbpPlayerBufferQueue)->Enqueue(slbpPlayerBufferQueue, nextBuffer, nextSize);
        // the most likely other result is SL_RESULT_BUFFER_INSUFFICIENT,
        // which for this code example would indicate a programming error
        if (SL_RESULT_SUCCESS != result) {
            pthread_mutex_unlock(&audioEngineLock);
        }
        (void) result;
    } else {
        releaseResampleBuf();
        pthread_mutex_unlock(&audioEngineLock);
    }
}

extern "C"
JNIEXPORT void
createBufferQueueAudioPlayer(JNIEnv *env, jobject instance, jint sampleRate, jint samplesPerBuf) {
    SLresult lresult;
    if (sampleRate >= 0 && samplesPerBuf >= 0) {
        bqPlayerSampleRate = sampleRate * 1000;
        bqPlayerBufSize = samplesPerBuf;
    }

    //配置buffer queue
    SLDataLocator_AndroidSimpleBufferQueue inputlocator = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE,
                                                           2};

    /**
     * 	SLuint32 		formatType; 类型
	    SLuint32 		numChannels; 通道数量
     	SLuint32 		samplesPerSec;
	    SLuint32 		bitsPerSample;
	    SLuint32 		containerSize;
	    SLuint32 		channelMask; 通道方式 ：SL_SPEAKER_FRONT_CENTER 前景中央播放
	    SLuint32		endianness;
     *
     */
    //配置pcm 格式
    SLDataFormat_PCM dataFormat = {SL_DATAFORMAT_PCM, 1, SL_SAMPLINGRATE_8,
                                   SL_PCMSAMPLEFORMAT_FIXED_16, SL_PCMSAMPLEFORMAT_FIXED_16,
                                   SL_SPEAKER_FRONT_CENTER, SL_BYTEORDER_LITTLEENDIAN};

    if (bqPlayerSampleRate) {
        dataFormat.samplesPerSec = bqPlayerSampleRate;
    }

    /**
     * 定义音频来源
     */
    SLDataSource dataSource = {&inputlocator, &dataFormat};


    /**
     * 定义音频输出
     */
    SLDataLocator_OutputMix outputMixLocator = {SL_DATALOCATOR_OUTPUTMIX, outputMixItf};
    SLDataSink dataSink = {&outputMixLocator, NULL};
    SLInterfaceID interfaceIds[] = {SL_IID_BUFFERQUEUE, SL_IID_VOLUME, SL_IID_EFFECTSEND};
    SLboolean flags[] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE};

    lresult = (*slEngineItf)->CreateAudioPlayer(slEngineItf, &bpPlayerObject, &dataSource,
                                                &dataSink, bqPlayerSampleRate ? 2 : 3, interfaceIds,
                                                flags);
    (void) lresult;

    lresult = (*bpPlayerObject)->Realize(bpPlayerObject, SL_BOOLEAN_FALSE);
    (void) lresult;


    lresult = (*bpPlayerObject)->GetInterface(bpPlayerObject, SL_IID_VOLUME, &slVolumeItf);
    (void) lresult;


    lresult = (*bpPlayerObject)->GetInterface(bpPlayerObject, SL_IID_PLAY, &pPlayer);
    (void) lresult;


    lresult = (*bpPlayerObject)->GetInterface(bpPlayerObject, SL_IID_EFFECTSEND,
                                              &slEffectSendItf);
    (void) lresult;

    lresult = (*slbpPlayerBufferQueue)->RegisterCallback(slbpPlayerBufferQueue,
                                                         bqPlayerCallback,
                                                         nullptr);
    (void) lresult;
    lresult = (*pPlayer)->SetPlayState(pPlayer, SL_PLAYSTATE_PLAYING);
    (void) lresult;
}





extern "C"
JNIEXPORT jboolean enableReverb(JNIEnv *env, jobject instance, jboolean boolean) {
    return JNI_TRUE;
}


void bqRecorderCallback(SLAndroidSimpleBufferQueueItf bq, void *context) {
    assert(NULL == context);
    SLresult result;
    result = (*slRecordItf)->SetRecordState(slRecordItf, SL_RECORDSTATE_STOPPED);
    if (SL_RESULT_SUCCESS == result) {
        recorderSize = RECORDER_FRAMES * sizeof(short);
    }
    pthread_mutex_unlock(&audioEngineLock);
}


extern "C"
JNIEXPORT jboolean createAudioRecorder(JNIEnv *env, jobject instance, jboolean boolean) {
    SLresult lresult;


    SLDataLocator_IODevice loc_dev = {SL_DATALOCATOR_IODEVICE, SL_IODEVICE_AUDIOINPUT,
                                      SL_DEFAULTDEVICEID_AUDIOINPUT, NULL};
    SLDataSource audioSrc = {&loc_dev, NULL};

    SLDataLocator_AndroidSimpleBufferQueue loc_bq = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2};
    SLDataFormat_PCM format_pcm = {SL_DATAFORMAT_PCM, 1, SL_SAMPLINGRATE_16,
                                   SL_PCMSAMPLEFORMAT_FIXED_16, SL_PCMSAMPLEFORMAT_FIXED_16,
                                   SL_SPEAKER_FRONT_CENTER, SL_BYTEORDER_LITTLEENDIAN};
    SLDataSink audioSnk = {&loc_bq, &format_pcm};

    SLInterfaceID ids = {SL_IID_BUFFERQUEUE};
    SLboolean bools = {SL_BOOLEAN_TRUE};

    lresult = (*slEngineItf)->CreateAudioRecorder(slEngineItf, &slRecorderObject, &audioSrc,
                                                  &audioSnk, 1, &ids, &bools);
    (void) lresult;
    if (SL_BOOLEAN_FALSE == lresult) {
        LOGE("CREATE RECORDER ERROR");
        return JNI_FALSE;
    }

    lresult = (*slRecorderObject)->Realize(slRecorderObject, SL_BOOLEAN_FALSE);
    (void) lresult;
    lresult = (*slRecorderObject)->GetInterface(slRecorderObject, SL_IID_RECORD, &slRecordItf);
    (void) lresult;
    lresult = (*slRecorderObject)->GetInterface(slRecorderObject, SL_IID_BUFFERQUEUE,
                                                &slbpRecorderBufferQueue);
    (void) lresult;

    lresult = (*slbpRecorderBufferQueue)->RegisterCallback(slbpRecorderBufferQueue,
                                                           bqRecorderCallback, nullptr);
    (void) lresult;
    return JNI_TRUE;
}


extern "C"
JNIEXPORT void startRecording(JNIEnv *env, jobject instance) {
    SLresult lresult;
    if (pthread_mutex_trylock(&audioEngineLock)) {
        return;
    }

    lresult = (*slRecordItf)->SetRecordState(slRecordItf, SL_RECORDSTATE_STOPPED);
    (void) lresult;


    recorderSize = 0;

    lresult = (*slbpRecorderBufferQueue)->Enqueue(slbpRecorderBufferQueue, recorderBuffer,
                                                  RECORDER_FRAMES *
                                                  sizeof(short));
    (void) lresult;
    lresult = (*slRecordItf)->SetRecordState(slRecordItf, SL_RECORDSTATE_RECORDING);
    (void) lresult;

}


extern "C"
JNIEXPORT void shutdown(JNIEnv *env, jobject instance) {
    if (bpPlayerObject != nullptr) {
        (*bpPlayerObject)->Destroy(bpPlayerObject);
    }
    if (slengineObject != nullptr) {
        (*slengineObject)->Destroy(slengineObject);
    }
    if (outputMixItf != nullptr) {
        (*outputMixItf)->Destroy(outputMixItf);
    }

    if (slRecorderObject != nullptr) {
        (*slRecorderObject)->Destroy(slRecorderObject);
    }

    env->UnregisterNatives(global_env->FindClass(classpath));

    free(global_vm);
    free(global_env);

    free(resampleBuf);
    free(nextBuffer);
    pthread_mutex_destroy(&audioEngineLock);
}


extern "C"
JNIEXPORT void stopRecording(JNIEnv *env, jobject instance) {
    SLresult  lresult;
    if(pthread_mutex_trylock(&audioEngineLock)){
        LOGE("LOCK ERROR");
        return;
    }
    lresult = (*slRecordItf)->SetRecordState(slRecordItf,SL_RECORDSTATE_STOPPED);
    (void)lresult;
    pthread_mutex_unlock(&audioEngineLock);
}

extern "C"
JNIEXPORT void stopPlaying(JNIEnv *env, jobject instance) {

}

extern "C"
JNIEXPORT jboolean selectClip(JNIEnv *env, jobject instance, jint which, jint count) {

    if (pthread_mutex_trylock(&audioEngineLock)) {
        return JNI_FALSE;
    }

    switch (which) {
        case 4:     // CLIP_PLAYBACK
            nextBuffer = createResampledBuf(4, SL_SAMPLINGRATE_16, &nextSize);
            // we recorded at 16 kHz, but are playing buffers at 8 Khz, so do a primitive down-sample
            if (!nextBuffer) {
                unsigned i;
                for (i = 0; i < recorderSize; i += 2 * sizeof(short)) {
                    recorderBuffer[i >> 2] = recorderBuffer[i >> 1];
                }
                recorderSize >>= 1;
                nextBuffer = recorderBuffer;
                nextSize = recorderSize;
            }
            break;
    }

    nextCount = count;
    if (nextSize > 0) {
        SLresult result;
        result = (*slbpPlayerBufferQueue)->Enqueue(slbpPlayerBufferQueue, nextBuffer, nextSize);
        if (SL_RESULT_SUCCESS != result) {
            pthread_mutex_unlock(&audioEngineLock);
            return JNI_FALSE;
        }
    } else {
        pthread_mutex_unlock(&audioEngineLock);
    }
    return JNI_TRUE;
}

static JNINativeMethod methods[] = {{"createEngine",                 "()V",   (void *) createEngine},
                                    {"createBufferQueueAudioPlayer", "(II)V", (void *) createBufferQueueAudioPlayer},
                                    {"enableReverb",                 "(Z)Z",  (void *) enableReverb},
                                    {"createAudioRecorder",          "(Z)Z",  (void *) createAudioRecorder},
                                    {"startRecording",               "()V",   (void *) startRecording},
                                    {"shutdown",                     "()V",   (void *) shutdown},
                                    {"selectClip",                   "(II)Z", (void *) selectClip},
                                    {"stopRecording",                "()V",   (void *) stopRecording},
                                    {"stopPlaying",                  "()V",   (void *) stopPlaying}};


JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    global_vm = vm;
    if (global_vm->GetEnv((void **) &global_env, JNI_VERSION_1_6) < 0) {
        LOGE("GET ENV ERROR");
    }
    if (global_env->RegisterNatives(global_env->FindClass(classpath), methods,
                                    sizeof(methods) / sizeof(JNINativeMethod)) < 0) {
        LOGE("REGIST METHOD ERROR");
    }
    return JNI_VERSION_1_6;

}
