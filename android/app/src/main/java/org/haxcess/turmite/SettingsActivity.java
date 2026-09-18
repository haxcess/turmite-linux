package org.haxcess.turmite;

import android.app.Activity;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;
import java.util.Locale;

public final class SettingsActivity extends Activity {
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        ScrollView scroll = new ScrollView(this);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        int padding = (int)(32 * getResources().getDisplayMetrics().density);
        layout.setPadding(padding, padding, padding, padding);
        scroll.addView(layout);
        TextView title = new TextView(this);
        title.setText(R.string.configure);
        title.setTextSize(24);
        layout.addView(title);
        SaverSettings current = SaverSettings.load(this);
        SeekBar first = slider(layout, R.string.ants, SaverSettings.ANTS, 2, 8, current.ants, false);
        slider(layout, R.string.minutes, SaverSettings.MINUTES, 1, 50, current.minutesTenths, true);
        slider(layout, R.string.divisor, SaverSettings.DIVISOR, 1, 100, current.divisor, false);
        TextView note = new TextView(this);
        note.setText(R.string.settings_note);
        layout.addView(note);
        Button done = new Button(this);
        done.setText(R.string.done);
        done.setOnClickListener(v -> finish());
        layout.addView(done);
        setContentView(scroll);
        first.requestFocus();
    }

    private SeekBar slider(LinearLayout layout, int labelId, String key,
                           int min, int max, int value, boolean tenths) {
        TextView label = new TextView(this);
        layout.addView(label);
        SeekBar slider = new SeekBar(this);
        slider.setId(View.generateViewId());
        label.setLabelFor(slider.getId());
        slider.setMax(max - min);
        slider.setKeyProgressIncrement(1);
        slider.setProgress(value - min);
        slider.setMinimumHeight((int)(48 * getResources().getDisplayMetrics().density));
        slider.setFocusable(true);
        updateLabel(label, slider, labelId, value, tenths);
        slider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar bar, int progress, boolean fromUser) {
                int selected = min + progress;
                updateLabel(label, bar, labelId, selected, tenths);
                if (fromUser) SaverSettings.preferences(SettingsActivity.this).edit().putInt(key, selected).apply();
            }
            @Override public void onStartTrackingTouch(SeekBar bar) {}
            @Override public void onStopTrackingTouch(SeekBar bar) {}
        });
        layout.addView(slider, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));
        return slider;
    }

    private void updateLabel(TextView label, SeekBar bar, int labelId, int value, boolean tenths) {
        String number = tenths ? String.format(Locale.getDefault(), "%.1f", value / 10.0)
                              : String.format(Locale.getDefault(), "%d", value);
        String text = getString(R.string.setting_value, getString(labelId), number);
        label.setText(text);
        bar.setContentDescription(text);
    }
}
