package dev.ftop.widget;

import android.app.Application;
import android.content.Context;
import rikka.shizuku.ShizukuProvider;

public final class FtopApp extends Application {
    @Override protected void attachBaseContext(Context base) {
        super.attachBaseContext(base);
        ShizukuProvider.enableMultiProcessSupport(getPackageName().equals(Application.getProcessName()));
    }
    @Override public void onCreate() {
        super.onCreate();
        if (!getPackageName().equals(Application.getProcessName()))
            ShizukuProvider.requestBinderForNonProviderProcess(this);
    }
}
