package dev.ftop.widget;

import android.content.Context;
import android.content.pm.ApplicationInfo;
import java.util.*;

/** Short-lived display cache. Controls create a fresh resolver for every validation. */
final class ProcessIdentity {
    private final Context context;
    private final Map<Integer,String[]> packages=new HashMap<>();
    private final Map<String,ApplicationInfo> apps=new HashMap<>();
    private final Map<String,String> labels=new HashMap<>();
    ProcessIdentity(Context context) { this.context=context; }
    static String choose(String raw,String[] candidates) {
        if(candidates==null)return "";
        String prefix=raw.split(":",2)[0];
        for(String p:candidates)if(p.equals(prefix))return p;
        return candidates.length==1?candidates[0]:"";
    }
    void resolve(ProcessEntry p,String raw) {
        p.rawName=raw; p.name=raw;
        if(!packages.containsKey(p.uid))packages.put(p.uid,context.getPackageManager().getPackagesForUid(p.uid));
        p.packageName=choose(raw,packages.get(p.uid));
        if(p.packageName.isEmpty())return;
        String pkg=p.packageName;
        if(!apps.containsKey(pkg)) {
            ApplicationInfo app=null;
            try { app=context.getPackageManager().getApplicationInfo(pkg,0); } catch(Exception ignored) { }
            apps.put(pkg,app);
            if(app!=null)labels.put(pkg,context.getPackageManager().getApplicationLabel(app).toString());
        }
        ApplicationInfo app=apps.get(pkg); p.appLabel=pkg;
        if(app!=null) { p.system|=(app.flags&ApplicationInfo.FLAG_SYSTEM)!=0; p.appLabel=labels.get(pkg); }
        p.name=p.appLabel+(raw.startsWith(pkg+":")?raw.substring(pkg.length()):raw.equals(pkg)?"":" · "+raw);
    }
}
