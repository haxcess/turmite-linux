package org.haxcess.turmite;

import android.app.Activity;
import android.os.Bundle;
import android.view.WindowManager;

public final class PreviewActivity extends Activity {
    private TurmiteView view;
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN
                | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        view = new TurmiteView(this);
        setContentView(view);
    }
    @Override public void onResume() { super.onResume(); view.setRunning(true); }
    @Override public void onPause() { view.setRunning(false); super.onPause(); }
}
