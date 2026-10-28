package dev.ftop.widget;

import android.app.Activity;
import android.os.Bundle;

/** Common native window/theme behavior for App pages only. */
public abstract class AppPage extends Activity {
    AppUi ui;
    private int theme;
    @Override public void onCreate(Bundle state){
        setTheme(Store.dark(this)?R.style.AppThemeDark:R.style.AppThemeLight);
        super.onCreate(state);ui=new AppUi(this);theme=Store.theme(this);
    }
    final boolean themeCurrent(){return ui.dark==Store.dark(this)&&theme==Store.theme(this);}
    @Override protected void onResume(){super.onResume();if(!themeCurrent())recreate();}
}
