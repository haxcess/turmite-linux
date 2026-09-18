package org.haxcess.turmite;

import android.service.dreams.DreamService;

public final class TurmiteDreamService extends DreamService {
    private TurmiteView view;
    @Override public void onAttachedToWindow() {
        super.onAttachedToWindow();
        setInteractive(false);
        setFullscreen(true);
        view = new TurmiteView(this);
        setContentView(view);
    }
    @Override public void onDreamingStarted() {
        super.onDreamingStarted();
        if (view != null) view.setRunning(true);
    }
    @Override public void onDreamingStopped() {
        if (view != null) view.setRunning(false);
        super.onDreamingStopped();
    }
    @Override public void onDetachedFromWindow() {
        if (view != null) view.setRunning(false);
        view = null;
        super.onDetachedFromWindow();
    }
    @Override public void onDestroy() {
        if (view != null) view.setRunning(false);
        super.onDestroy();
    }
}
