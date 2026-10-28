package dev.ftop.widget;

import android.content.Context;
import android.util.AtomicFile;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import org.json.JSONObject;

/** Atomic files work across the app and widget processes without stale preference caches. */
public final class Store {
    private static AtomicFile file(Context c, String name) { return new AtomicFile(new File(c.getFilesDir(), name)); }
    public static JSONObject read(Context c, String name) {
        try { return new JSONObject(new String(file(c,name).readFully(),StandardCharsets.UTF_8)); }
        catch (Exception ignored) { return new JSONObject(); }
    }
    public static void write(Context c, String name, JSONObject data) {
        AtomicFile f=file(c,name); FileOutputStream out=null;
        try { out=f.startWrite(); out.write(data.toString().getBytes(StandardCharsets.UTF_8)); f.finishWrite(out); }
        catch(Exception e) { if(out!=null) f.failWrite(out); android.util.Log.w("ftop", "Cannot save "+name,e); }
    }
    public static int theme(Context c) { return read(c,"settings.json").optInt("theme",0); }
    public static void theme(Context c,int value) {
        try { write(c,"settings.json",new JSONObject().put("theme",value)); } catch(Exception ignored) {}
    }
    public static boolean dark(Context c) {
        int theme=theme(c);
        return theme==1 || theme==0 && (c.getResources().getConfiguration().uiMode & 48)==32;
    }
    public static Snapshot snapshot(Context c) { return Snapshot.from(read(c,"snapshot.json")); }
    public static void save(Context c,Snapshot s) {
        try { write(c,"snapshot.json",s.json()); } catch(Exception e) { android.util.Log.w("ftop","Snapshot save failed",e); }
    }
}
