package com.james.opensles

import android.content.Intent
import android.os.Bundle
import android.util.Log
import androidx.appcompat.app.AppCompatActivity
import kotlinx.android.synthetic.main.activity_home.*

class HomeActivity : AppCompatActivity() {

    private var openslesHelper: OpenslesHelper? = null

    private var play = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_home)
        openslesHelper = OpenslesHelper()
        val flag = openslesHelper?.createAssetAudioPlayer(assets, "background.mp3")
        Log.i("create state =",flag.toString())
        btn_record_play.onClick {
            stopPlay()
            startActivity(Intent(this, MainActivity::class.java))
        }

        btn_asset.onClick {
            play = !play
            openslesHelper?.setPlayingAssetAudioPlayer(play)
        }
    }


    private fun stopPlay(){
        play = false
        openslesHelper?.setPlayingAssetAudioPlayer(play)
    }

    override fun onDestroy() {
        stopPlay()
        openslesHelper?.shutdown()
        openslesHelper = null
        super.onDestroy()
    }
}