package org.haxcess.turmite;

import android.content.Context;
import android.content.SharedPreferences;

final class SaverSettings {
    static final String ANTS = "ants", MINUTES = "minutes_tenths", DIVISOR = "token_rate_divisor";
    final int ants, minutesTenths, divisor;
    private SaverSettings(int ants, int minutesTenths, int divisor) {
        this.ants = ants;
        this.minutesTenths = minutesTenths;
        this.divisor = divisor;
    }
    static SharedPreferences preferences(Context context) {
        return context.getSharedPreferences("screensaver", Context.MODE_PRIVATE);
    }
    private static int bounded(SharedPreferences p, String key, int fallback, int min, int max) {
        try { return Math.max(min, Math.min(max, p.getInt(key, fallback))); }
        catch (ClassCastException invalid) { return fallback; }
    }
    static SaverSettings load(Context context) {
        SharedPreferences p = preferences(context);
        return new SaverSettings(bounded(p, ANTS, 8, 2, 8),
                bounded(p, MINUTES, 50, 1, 50), bounded(p, DIVISOR, 1, 1, 100));
    }
    long lifetimeMillis() { return minutesTenths * 6000L; }
}
