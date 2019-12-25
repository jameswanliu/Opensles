package com.james.opensles

import android.content.Context
import android.media.AudioManager
import android.os.Build
import android.os.Bundle
import android.os.SystemClock
import android.view.View
import androidx.appcompat.app.AppCompatActivity
import com.hjq.permissions.OnPermission
import com.hjq.permissions.Permission
import com.hjq.permissions.XXPermissions
import com.james.opensles.Constants.STATE_COMPLETE
import com.james.opensles.Constants.STATE_NORMAL
import com.james.opensles.Constants.STATE_PAUSE
import com.james.opensles.Constants.STATE_PLAYING
import com.james.opensles.Constants.STATE_RECORD
import kotlinx.android.synthetic.main.activity_main.*

class MainActivity : AppCompatActivity() {

    private var currentState = STATE_NORMAL
    private var isPlaying = false
    private var isRecord = false
    private var openslesHelper: OpenslesHelper? = null

    companion object{
        var created = false
    }


    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        requestPermission()
        openslesHelper = OpenslesHelper()
        openslesHelper?.createEngine()


        var sampleRate = 0
        var bufSize = 0
        /*
         * retrieve fast audio path sample rate and buf size; if we have it, we pass to native
         * side to create a player with fast audio enabled [ fast audio == low latency audio ];
         * IF we do not have a fast audio path, we pass 0 for sampleRate, which will force native
         * side to pick up the 8Khz sample rate.
         */
        /*
         * retrieve fast audio path sample rate and buf size; if we have it, we pass to native
         * side to create a player with fast audio enabled [ fast audio == low latency audio ];
         * IF we do not have a fast audio path, we pass 0 for sampleRate, which will force native
         * side to pick up the 8Khz sample rate.
         */if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.JELLY_BEAN_MR1) {
            val myAudioMgr =
                getSystemService(Context.AUDIO_SERVICE) as AudioManager
            var nativeParam =
                myAudioMgr.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE)
            sampleRate = nativeParam.toInt()
            nativeParam = myAudioMgr.getProperty(AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER)
            bufSize = nativeParam.toInt()
        }

        openslesHelper?.createBufferQueueAudioPlayer(sampleRate, bufSize)



        requestPermission()

        iv_action.onClick {
            changeState()
        }

        iv_delete.onClick {
            changeNormalState()
        }

        iv_save.onClick {
            changeNormalState()
        }
    }


    private fun changeNormalState() {
        currentState = STATE_NORMAL
        chronometer.base = SystemClock.elapsedRealtime()
//        MediaPlayerHelper.release()
//        MediaRecorderHelper.stopAndRelease()
        rl_bottom.visibility = View.GONE
        iv_action.setImageResource(R.mipmap.btn_clue_audio)
    }


    private fun requestPermission() {
        XXPermissions.with(this)
            .permission(
                Permission.Group.STORAGE, arrayOf(
                    Permission.WRITE_EXTERNAL_STORAGE, Permission.RECORD_AUDIO
                )
            )
            .request(object : OnPermission {
                override fun hasPermission(
                    granted: List<String>,
                    isAll: Boolean
                ) = Unit

                override fun noPermission(
                    denied: List<String>,
                    quick: Boolean
                ) = Unit
            })
    }


    private fun recordAudio() {
        if (!created) {
            created = openslesHelper?.createAudioRecorder()!!
        }
        if (created) {
            openslesHelper?.startRecording()
        }
    }


    private fun changeUiByState(state: Int) {
        when (state) {
            STATE_NORMAL -> {
                isRecord = false
                isPlaying = false
                openslesHelper?.shutdown()
                iv_action.setImageResource(R.mipmap.btn_clue_audio)
            }
            STATE_RECORD -> {
                if (!XXPermissions.isHasPermission(
                        this, arrayOf(
                            Permission.WRITE_EXTERNAL_STORAGE, Permission.RECORD_AUDIO
                        )
                    )
                ){
                    requestPermission()
                    return
                }
                    iv_action.setImageResource(R.mipmap.pause)
                chronometer.base = SystemClock.elapsedRealtime()
                chronometer.start()
                isRecord = true
                waveView.visibility = View.VISIBLE
                recordAudio()
            }
            STATE_COMPLETE -> {
                iv_action.setImageResource(R.mipmap.icon_audio_state_uploaded)
                chronometer.stop()
                waveView.visibility = View.INVISIBLE
                rl_bottom.visibility = View.VISIBLE
//                MediaRecorderHelper.stopAndRelease()
                isRecord = false
            }
            STATE_PLAYING -> {
//                MediaPlayerHelper.resume()
                openslesHelper?.selectClip(4,2)
                isPlaying = true
                iv_action.setImageResource(R.mipmap.icon_audio_state_uploaded_play)
//                MediaPlayerHelper.playsound(MediaRecorderHelper.playpath, MediaPlayer.OnCompletionListener {
//                    currentState = STATE_COMPLETE
//                    changeState()
//                })
            }
            STATE_PAUSE -> {
//                MediaPlayerHelper.pause()
                isPlaying = true
                iv_action.setImageResource(R.mipmap.icon_audio_state_uploaded)
            }
        }
    }






    private fun changeState() {
        when (currentState) {
            STATE_NORMAL -> {
                currentState = STATE_RECORD
            }
            STATE_RECORD -> {
                currentState = STATE_COMPLETE
            }
            STATE_COMPLETE -> {
                currentState = STATE_PLAYING
            }
            STATE_PLAYING -> {
                currentState = STATE_PAUSE
            }
            STATE_PAUSE -> {
                currentState = STATE_PLAYING
            }
        }
        changeUiByState(currentState)
    }


    override fun onDestroy() {
        openslesHelper?.shutdown()
        openslesHelper = null
        super.onDestroy()
    }


}
