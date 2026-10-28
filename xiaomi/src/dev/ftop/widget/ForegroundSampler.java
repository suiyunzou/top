package dev.ftop.widget;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** One worker per visible page; stale results never reach a resumed/destroyed page. */
final class ForegroundSampler {
    interface Listener {void sampled(Snapshot value);}
    private final Handler main=new Handler(Looper.getMainLooper());
    private final ExecutorService worker=Executors.newSingleThreadExecutor();
    private final Sampler sampler;private final Listener listener;private final int delay;
    private boolean active,busy,closed;private int generation;
    ForegroundSampler(Context context,int delay,Listener listener){sampler=new Sampler(context);this.delay=delay;this.listener=listener;}
    private final Runnable tick=()->sample();
    private void sample(){
        if(!active||closed||busy)return;busy=true;int token=generation;
        worker.execute(()->{Snapshot result=null;try{result=sampler.sample();}catch(Exception e){android.util.Log.w("ftop","Page sampling failed",e);}Snapshot value=result;
            main.post(()->{busy=false;if(closed||!active)return;if(token==generation&&value!=null)listener.sampled(value);main.removeCallbacks(tick);main.postDelayed(tick,token==generation?delay:0);});});
    }
    void resume(){if(closed)return;active=true;generation++;main.removeCallbacks(tick);main.post(tick);}
    void pause(){active=false;generation++;main.removeCallbacks(tick);}
    void close(){pause();closed=true;worker.shutdownNow();main.removeCallbacksAndMessages(null);}
}
