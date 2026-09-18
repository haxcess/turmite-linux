package org.haxcess.turmite;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.os.Bundle;
import android.provider.Settings;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

public final class MainActivity extends Activity {
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(Gravity.CENTER);
        layout.setPadding(32, 32, 32, 32);
        TextView title = new TextView(this);
        title.setText(R.string.app_name);
        title.setTextSize(28);
        layout.addView(title);
        TextView intro = new TextView(this);
        intro.setText(R.string.intro);
        layout.addView(intro);
        Button preview = new Button(this);
        preview.setText(R.string.preview);
        preview.setOnClickListener(v -> startActivity(new Intent(this, PreviewActivity.class)));
        layout.addView(preview);
        Button configure = new Button(this);
        configure.setText(R.string.configure);
        configure.setOnClickListener(v -> startActivity(new Intent(this, SettingsActivity.class)));
        layout.addView(configure);
        Button settings = new Button(this);
        settings.setText(R.string.screensaver_settings);
        settings.setOnClickListener(v -> {
            try { startActivity(new Intent(Settings.ACTION_DREAM_SETTINGS)); }
            catch (ActivityNotFoundException e) {
                Toast.makeText(this, R.string.settings_unavailable, Toast.LENGTH_LONG).show();
            }
        });
        layout.addView(settings);
        setContentView(layout);
        preview.requestFocus();
    }
}
