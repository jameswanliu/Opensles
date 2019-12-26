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
#define CHANEL 2
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
static int bqPlayerSampleRate = 44100;
static size_t bqPlayerBufSize = static_cast<size_t >( CHANEL * bqPlayerSampleRate * 2);

unsigned char *buffer; //缓冲区

pthread_mutex_t audioEngineLock = PTHREAD_MUTEX_INITIALIZER;

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


class AudioContext {

public:
    AudioContext(FILE *file,
                 unsigned char *buffer,
                 size_t bufferSize) {
        this->file = file;
        this->buffer = buffer;
        this->bufferSize = bufferSize;
    }

public:
    FILE *file;
    unsigned char *buffer;
    size_t bufferSize;


    ~AudioContext() {
        free(file);
        free(buffer);
        bufferSize = 0;
    }

};

AudioContext *audioContext;

void createEngine() {
    SLresult lresult;

    SLEngineOption options[] = {{(SLuint32) SL_ENGINEOPTION_THREADSAFE, (SLuint32) SL_BOOLEAN_TRUE}};
    lresult = slCreateEngine(&slengineObject, 1, options, 0, NULL, NULL);
    (void) lresult;
    lresult = (*slengineObject)->Realize(slengineObject, SL_BOOLEAN_FALSE);
    (void) lresult;
    lresult = (*slengineObject)->GetInterface(slengineObject, SL_IID_ENGINE, &slEngineItf);
    (void) lresult;

}

void CreateOutputMix() {

    SLresult lresult;
    SLInterfaceID slInterfaceId[1] = {SL_IID_ENVIRONMENTALREVERB};
    SLboolean slb[1] = {SL_BOOLEAN_FALSE};
    lresult = (*slEngineItf)->CreateOutputMix(slEngineItf, &outputMixItf, 1, slInterfaceId, slb);
    (void) lresult;

    lresult = (*outputMixItf)->Realize(outputMixItf, SL_BOOLEAN_FALSE);
    (void) lresult;

    lresult = (*outputMixItf)->GetInterface(outputMixItf, SL_IID_ENVIRONMENTALREVERB,
                                            &slEnvironmentalReverbItf);
    (void) lresult;

    if (SL_RESULT_SUCCESS == lresult) {
        lresult = (*slEnvironmentalReverbItf)->SetEnvironmentalReverbProperties(
                slEnvironmentalReverbItf, &slEnvironmentalReverbSettings);
        (void) lresult;
    }
}


void bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *context) {
    // for streaming playback, replace this test by logic to find and fill the next buffer
    AudioContext *audioContext = reinterpret_cast<AudioContext *>(context);

    if (audioContext != nullptr) {
        if (audioContext->buffer != nullptr && audioContext->bufferSize > 0) {
            SLresult result;
            // enqueue another buffer
            result = (*slbpPlayerBufferQueue)->Enqueue(slbpPlayerBufferQueue, audioContext->buffer,
                                                       audioContext->bufferSize);
            // the most likely other result is SL_RESULT_BUFFER_INSUFFICIENT,
            // which for this code example would indicate a programming error
            if (SL_RESULT_SUCCESS != result) {
                pthread_mutex_unlock(&audioEngineLock);
            }
            (void) result;
        } else {
            pthread_mutex_unlock(&audioEngineLock);
            LOGE("buffer IS NULL");
        }

    } else {
        pthread_mutex_unlock(&audioEngineLock);
        LOGE("AUDIOCONTEXT IS NULL");
    }
}

extern "C"
JNIEXPORT void
createBufferQueueAudioPlayer(JNIEnv *env, jobject instance, jstring path, jint sampleRate,
                             jint samplesPerBuf) {
    SLresult lresult;
    const char *filePath = env->GetStringUTFChars(path, JNI_FALSE);
    FILE *file = fopen(filePath, "r");

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


    lresult = (*bpPlayerObject)->GetInterface(bpPlayerObject, SL_IID_BUFFERQUEUE,
                                              &slbpPlayerBufferQueue);
    (void) lresult;


    buffer = (unsigned char *) malloc(bqPlayerBufSize);

    audioContext = new AudioContext(file, buffer, bqPlayerBufSize);

    lresult = (*slbpPlayerBufferQueue)->RegisterCallback(slbpPlayerBufferQueue,
                                                         bqPlayerCallback,
                                                         audioContext);
    (void) lresult;
    lresult = (*pPlayer)->SetPlayState(pPlayer, SL_PLAYSTATE_PLAYING);
    (void) lresult;

    env->ReleaseStringUTFChars(path, filePath);
}





extern "C"
JNIEXPORT jboolean enableReverb(JNIEnv *env, jobject instance, jboolean boolean) {
    return JNI_TRUE;
}


void bqRecorderCallback(SLAndroidSimpleBufferQueueItf bq, void *context) {
    SLresult result;
    result = (*slRecordItf)->SetRecordState(slRecordItf, SL_RECORDSTATE_STOPPED);
    (void) result;
    pthread_mutex_unlock(&audioEngineLock);
}


extern "C"
JNIEXPORT jboolean createAudioRecorder(JNIEnv *env, jobject instance) {
    SLresult lresult;


    SLDataLocator_IODevice loc_dev = {SL_DATALOCATOR_IODEVICE, SL_IODEVICE_AUDIOINPUT,
                                      SL_DEFAULTDEVICEID_AUDIOINPUT, NULL};
    SLDataSource audioSrc = {&loc_dev, NULL};

    SLDataLocator_AndroidSimpleBufferQueue loc_bq = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2};
    SLDataFormat_PCM format_pcm = {SL_DATAFORMAT_PCM, 1, SL_SAMPLINGRATE_16,
                                   SL_PCMSAMPLEFORMAT_FIXED_16, SL_PCMSAMPLEFORMAT_FIXED_16,
                                   SL_SPEAKER_FRONT_CENTER, SL_BYTEORDER_LITTLEENDIAN};
    SLDataSink audioSnk = {&loc_bq, &format_pcm};

    const SLInterfaceID ids[1] = {SL_IID_ANDROIDSIMPLEBUFFERQUEUE};
    const SLboolean bools[1] = {SL_BOOLEAN_TRUE};

    lresult = (*slEngineItf)->CreateAudioRecorder(slEngineItf, &slRecorderObject, &audioSrc,
                                                  &audioSnk, 1, ids, bools);
    (void) lresult;
    if (SL_RESULT_SUCCESS != lresult) {
        LOGE("CREATE RECORDER ERROR");
        return JNI_FALSE;
    }

    lresult = (*slRecorderObject)->Realize(slRecorderObject, SL_BOOLEAN_FALSE);
    (void) lresult;
    lresult = (*slRecorderObject)->GetInterface(slRecorderObject, SL_IID_RECORD, &slRecordItf);
    (void) lresult;
    lresult = (*slRecorderObject)->GetInterface(slRecorderObject, SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
                                                &slbpRecorderBufferQueue);
    (void) lresult;

    lresult = (*slbpRecorderBufferQueue)->RegisterCallback(slbpRecorderBufferQueue,
                                                           bqRecorderCallback, nullptr);
    (void) lresult;
    return JNI_TRUE;
}





extern "C"
JNIEXPORT void startRecording(JNIEnv *env, jobject instance, jstring path) {
    SLresult lresult;
    if (pthread_mutex_trylock(&audioEngineLock)) {
        return;
    }

    lresult = (*slRecordItf)->SetRecordState(slRecordItf, SL_RECORDSTATE_STOPPED);
    (void) lresult;

    lresult = (*slbpRecorderBufferQueue)->Clear(slbpRecorderBufferQueue);
    (void) lresult;

    const char *filePath = env->GetStringUTFChars(path, JNI_FALSE);
    FILE *file = fopen(filePath, "w");
    if (file == NULL) {
        LOGE("OPEN FILE ERROR");
        return;
    }

    if (buffer != nullptr) {
        free(buffer);
    }
    buffer = (unsigned char *) malloc(bqPlayerBufSize);
    audioContext = new AudioContext(file, buffer, bqPlayerBufSize);
    lresult = (*slbpRecorderBufferQueue)->Enqueue(slbpRecorderBufferQueue, audioContext->buffer,
                                                  audioContext->bufferSize);
    (void) lresult;
    lresult = (*slRecordItf)->SetRecordState(slRecordItf, SL_RECORDSTATE_RECORDING);
    (void) lresult;
    env->ReleaseStringUTFChars(path, filePath);
}


extern "C"
JNIEXPORT jboolean stopRecording(JNIEnv *env, jobject instance) {
    SLresult lresult;
    lresult = (*slRecordItf)->SetRecordState(slRecordItf, SL_RECORDSTATE_STOPPED);
    (void) lresult;
    pthread_mutex_unlock(&audioEngineLock);
    return JNI_TRUE;
}


extern "C"
JNIEXPORT void shutdown(JNIEnv *env, jobject instance) {
    if (bpPlayerObject != nullptr) {
        (*bpPlayerObject)->Destroy(bpPlayerObject);
        slVolumeItf = NULL;
        slEffectSendItf = NULL;
        pPlayer = NULL;
        slbpPlayerBufferQueue = NULL;
    }
    if (slengineObject != nullptr) {
        (*slengineObject)->Destroy(slengineObject);
        slEngineItf = NULL;
    }
    if (outputMixItf != nullptr) {
        (*outputMixItf)->Destroy(outputMixItf);
        slEnvironmentalReverbItf = NULL;
    }

    if (slRecorderObject != nullptr) {
        (*slRecorderObject)->Destroy(slRecorderObject);
        slRecordItf = NULL;
        slbpRecorderBufferQueue = NULL;
    }

    env->UnregisterNatives(global_env->FindClass(classpath));

    free(global_vm);
    delete audioContext;
    free(global_env);
    free(buffer);
    pthread_mutex_destroy(&audioEngineLock);
}




extern "C"
JNIEXPORT void stopPlaying(JNIEnv *env, jobject instance) {

}

extern "C"
JNIEXPORT jboolean releaseFile(JNIEnv *env, jobject instance) {
    SLresult lresult;
    pthread_mutex_unlock(&audioEngineLock);
    lresult = (*slbpRecorderBufferQueue)->Clear(slbpRecorderBufferQueue);
    (void) lresult;

    lresult = (*slbpPlayerBufferQueue)->Clear(slbpPlayerBufferQueue);
    (void) lresult;
    if (SL_RESULT_SUCCESS != lresult) {
        return JNI_FALSE;
    }

    delete audioContext;
    return JNI_TRUE;
}


extern "C"
JNIEXPORT jboolean selectClip(JNIEnv *env, jobject instance, jint which, jint count) {
    if (pthread_mutex_trylock(&audioEngineLock)) {
        return JNI_FALSE;
    }
    SLresult result;
    result = (*slbpPlayerBufferQueue)->Enqueue(slbpPlayerBufferQueue, buffer, bqPlayerBufSize);
    if (SL_RESULT_SUCCESS != result) {
        pthread_mutex_unlock(&audioEngineLock);
        return JNI_FALSE;
    }
    return JNI_TRUE;
}

static JNINativeMethod methods[] = {
//        {"createEngine",                 "()V",   (void *) createEngine},
        {"createBufferQueueAudioPlayer", "(Ljava/lang/String;II)V", (void *) createBufferQueueAudioPlayer},
        {"enableReverb",                 "(Z)Z",                    (void *) enableReverb},
        {"createAudioRecorder",          "()Z",                     (void *) createAudioRecorder},
        {"startRecording",               "(Ljava/lang/String;)V",   (void *) startRecording},
        {"shutdown",                     "()V",                     (void *) shutdown},
        {"selectClip",                   "(II)Z",                   (void *) selectClip},
        {"stopRecording",                "()Z",                     (void *) stopRecording},
        {"stopPlaying",                  "()V",                     (void *) stopPlaying},
        {"releaseFile",                  "()Z",                     (void *) releaseFile}};


JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    global_vm = vm;
    if (global_vm->GetEnv((void **) &global_env, JNI_VERSION_1_6) < 0) {
        LOGE("GET ENV ERROR");
    }
    if (global_env->RegisterNatives(global_env->FindClass(classpath), methods,
                                    sizeof(methods) / sizeof(JNINativeMethod)) < 0) {
        LOGE("REGIST METHOD ERROR");
    }

    createEngine();
    CreateOutputMix();

    return JNI_VERSION_1_6;

}
