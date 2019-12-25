package com.james.opensles

import android.view.View

inline fun View.onClick(noinline block: () -> Unit) {
    this.setOnClickListener { block() }
}