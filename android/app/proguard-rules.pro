# bridge.c uses name-based JNI lookup. Preserve exactly those entrypoints.
-keep,allowoptimization class org.haxcess.turmite.NativeEngine {
    static native <methods>;
}

# Referenced by a component-name string in res/xml/dream.xml.
-keepnames class org.haxcess.turmite.SettingsActivity
