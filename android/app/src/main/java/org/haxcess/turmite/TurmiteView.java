package org.haxcess.turmite;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.os.SystemClock;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.widget.Toast;

final class TurmiteView extends SurfaceView implements SurfaceHolder.Callback {
    private static final int MAX_SIDE = 960;
    private static final long FRAME_MS = 33;
    // Lifecycle fields are main-thread owned. Only the stop flag crosses threads.
    private boolean active;
    private int surfaceWidth, surfaceHeight;
    private Thread frameThread;
    private volatile boolean stopping;

    TurmiteView(Context context) {
        super(context);
        getHolder().addCallback(this);
    }

    void setRunning(boolean running) {
        active = running;
        if (running) startFrames(); else stopFrames();
    }

    @Override public void surfaceCreated(SurfaceHolder holder) {}
    @Override public void surfaceChanged(SurfaceHolder holder, int format, int w, int h) {
        stopFrames();
        surfaceWidth = w;
        surfaceHeight = h;
        startFrames();
    }
    @Override public void surfaceDestroyed(SurfaceHolder holder) {
        stopFrames();
        surfaceWidth = surfaceHeight = 0;
    }
    @Override protected void onDetachedFromWindow() {
        stopFrames();
        super.onDetachedFromWindow();
    }

    private void startFrames() {
        if (!active || frameThread != null || surfaceWidth <= 0 || surfaceHeight <= 0) return;
        double scale = Math.min(1.0, MAX_SIDE / (double)Math.max(surfaceWidth, surfaceHeight));
        final int w = Math.max(1, (int)(surfaceWidth * scale));
        final int h = Math.max(1, (int)(surfaceHeight * scale));
        // Unusually small transient surfaces cannot safely hold the initial population.
        if (w < 16 || h < 16) return;
        final SaverSettings settings = SaverSettings.load(getContext());
        stopping = false;
        frameThread = new Thread(() -> renderLoop(w, h, settings), "turmite-frames");
        frameThread.start();
    }

    private void stopFrames() {
        stopping = true;
        Thread thread = frameThread;
        if (thread == null) return;
        thread.interrupt();
        boolean interrupted = false;
        // Surface destruction must wait until the producer stops using its canvas.
        for (;;) {
            try { thread.join(); break; }
            catch (InterruptedException e) { interrupted = true; }
        }
        frameThread = null;
        if (interrupted) Thread.currentThread().interrupt();
    }

    private void renderLoop(int w, int h, SaverSettings settings) {
        long engine = 0;
        Bitmap bitmap = null;
        try {
            int[] pixels = new int[w * h];
            bitmap = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
            Paint paint = new Paint();
            paint.setFilterBitmap(false);
            Rect destination = new Rect();
            long restarted = 0;
            while (!stopping) {
                long started = SystemClock.uptimeMillis();
                if (engine == 0 || started - restarted >= settings.lifetimeMillis()) {
                    NativeEngine.destroy(engine);
                    engine = 0;
                    if (stopping) break;
                    engine = NativeEngine.create(w, h, settings.ants, settings.divisor);
                    if (engine == 0) throw new IllegalStateException("Native engine initialization failed");
                    restarted = started;
                }
                if (!NativeEngine.frame(engine, pixels))
                    throw new IllegalStateException("Native frame conversion failed");
                bitmap.setPixels(pixels, 0, w, 0, 0, w, h);
                Canvas canvas = getHolder().lockCanvas();
                if (canvas != null) {
                    try {
                        destination.set(0, 0, canvas.getWidth(), canvas.getHeight());
                        canvas.drawBitmap(bitmap, null, destination, paint);
                    } finally { getHolder().unlockCanvasAndPost(canvas); }
                }
                long remaining = FRAME_MS - (SystemClock.uptimeMillis() - started);
                if (remaining > 0) Thread.sleep(remaining);
            }
        } catch (InterruptedException expected) {
            Thread.currentThread().interrupt();
        } catch (RuntimeException | LinkageError | OutOfMemoryError failure) {
            Log.e("Turmite", "Frame loop failed", failure);
            if (!stopping) post(() -> Toast.makeText(getContext(), R.string.render_failed,
                    Toast.LENGTH_LONG).show());
        } finally {
            if (engine != 0) NativeEngine.destroy(engine);
            if (bitmap != null) bitmap.recycle();
        }
    }
}
