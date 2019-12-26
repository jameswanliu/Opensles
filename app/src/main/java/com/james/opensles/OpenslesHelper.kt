package com.james.opensles

import android.content.res.AssetManager


class OpenslesHelper {

    companion object{
        init {
            System.loadLibrary("native-lib")
        }
    }

    external fun createBufferQueueAudioPlayer(path:String,sampleRate: Int, samplesPerBuf: Int)
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
    external fun startRecording(path: String)
    external fun shutdown()
    external fun stopRecording():Boolean
    external fun releaseFile():Boolean
    external fun stopPlaying()

}


object Constants{
    const val STATE_NORMAL = 0
    const val STATE_RECORD = 1
    const val STATE_COMPLETE = 2
    const val STATE_PLAYING = 3
    const val STATE_PAUSE = 4
}