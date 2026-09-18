package org.haxcess.turmite;

// All calls, including destruction, belong to one frame thread.
final class NativeEngine {
    static { System.loadLibrary("turmite_android"); }
    static native long create(int width, int height, int ants, int divisor);
    static native boolean frame(long handle, int[] pixels);
    static native void destroy(long handle);
    private NativeEngine() {}
}
