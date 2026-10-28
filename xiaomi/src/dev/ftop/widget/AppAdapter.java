package dev.ftop.widget;

import android.content.Context;
import android.graphics.*;
import android.os.*;
import android.util.LruCache;
import android.view.*;
import android.widget.*;
import java.util.*;
import java.util.concurrent.*;

/** Native rows with a bounded icon cache; package-manager IO stays off the UI thread. */
final class AppAdapter extends BaseAdapter {
    private final Context context;
    private final int ink,muted;
    private final Handler main=new Handler(Looper.getMainLooper());
    private final ExecutorService loader=Executors.newSingleThreadExecutor();
    private final LruCache<String,Bitmap> icons=new LruCache<>(64);
    private final Set<String> pending=new HashSet<>();
    private boolean closed,byCpu;
    void setByCpu(boolean value){byCpu=value;}
    private final Set<String> unavailable=new HashSet<>();
    final List<AppGroup> rows=new ArrayList<>();
    AppAdapter(Context context){this.context=context;boolean dark=Store.dark(context);ink=new AppUi(context).ink;muted=new AppUi(context).muted;}
    void update(List<AppGroup> next){rows.clear();rows.addAll(next);notifyDataSetChanged();}
    void close(){closed=true;loader.shutdownNow();main.removeCallbacksAndMessages(null);icons.evictAll();}
    public int getCount(){return rows.size();}
    public AppGroup getItem(int i){return rows.get(i);}
    public long getItemId(int i){return rows.get(i).stableId();}
    public boolean hasStableIds(){return true;}
    private int dp(int n){return Math.round(n*context.getResources().getDisplayMetrics().density);}
    public View getView(int i,View view,ViewGroup parent){
        AppRow h=view instanceof AppRow?(AppRow)view:new AppRow(context);
        AppGroup g=getItem(i);h.name.setText(g.title());h.meta.setText((g.packageName.isEmpty()?"归属不明 · ":g.system?"系统 · ":"")+g.members.size()+" 个进程");
        h.cpu.setText("CPU "+g.cpuText());h.memory.setText(g.memoryValue());h.emphasis(byCpu);
        String key=g.packageName;h.icon.setTag(key);Bitmap icon=icons.get(key);
        if(icon!=null){h.icon.clearColorFilter();h.icon.setImageBitmap(icon);}else{h.icon.setImageResource(android.R.drawable.ic_menu_manage);h.icon.setColorFilter(muted);loadIcon(key,h.icon);}
        h.setContentDescription(g.title()+"，"+g.members.size()+" 个进程，CPU "+g.cpuText()+"，"+g.memoryText());return h;
    }
    private void loadIcon(String key,ImageView target){
        if(closed||key.isEmpty()||unavailable.contains(key)||!pending.add(key))return;
        loader.execute(()->{
            Bitmap bitmap=null;
            try{android.graphics.drawable.Drawable drawable=context.getPackageManager().getApplicationIcon(key);int size=Math.min(96,dp(32));bitmap=Bitmap.createBitmap(size,size,Bitmap.Config.ARGB_8888);drawable.setBounds(0,0,size,size);drawable.draw(new Canvas(bitmap));}catch(Exception ignored){}
            Bitmap result=bitmap;main.post(()->{pending.remove(key);if(closed)return;if(result!=null){icons.put(key,result);if(key.equals(target.getTag())){target.clearColorFilter();target.setImageBitmap(result);}}else unavailable.add(key);});
        });
    }
}
