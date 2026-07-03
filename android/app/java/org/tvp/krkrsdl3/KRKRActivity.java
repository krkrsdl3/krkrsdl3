package org.tvp.krkrsdl3;

import static org.tvp.krkrsdl3.MainActivity.SHAREDPREF_GAMECONFIG;

import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.res.AssetManager;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.view.WindowManager;

import androidx.appcompat.app.AlertDialog;
import androidx.core.content.FileProvider;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Objects;

public class KRKRActivity extends SDLActivity {
    private ArrayList<String> m_gameargs;

    // override sdl functions
    static {
        System.loadLibrary("SDL3");
        System.loadLibrary("krkrsdl3");
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {
                "SDL3",
                "krkrsdl3"
        };
    }

    @Override
    protected String[] getArguments() {
        return m_gameargs.toArray(new String[0]);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setNativeAssetManager(getAssets());
        Intent intent = getIntent();

        m_gameargs = intent.getStringArrayListExtra(SHAREDPREF_GAMECONFIG);
        this.fullscreen();
    }

    @Override
    protected void onResume() {
        super.onResume();
        this.fullscreen();
    }

    public void onWindowFocusChanged (boolean hasFocus) {
        if(hasFocus) this.fullscreen();
    }

    private void fullscreen() {
        View decorView = getWindow().getDecorView();
        int uiOptions = View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN ;
        decorView.setSystemUiVisibility(uiOptions);
        try {
            Objects.requireNonNull(this.getSupportActionBar()).hide();
        }
        catch (NullPointerException ignored){}
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
    }

    public native void setNativeAssetManager(AssetManager assetManager);
}
