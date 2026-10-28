package dev.ftop.widget;

import android.content.*;
import android.os.*;
import android.view.*;
import android.widget.*;
import java.util.*;
import java.util.concurrent.*;
import org.json.*;

/** Live application traffic, sampled only while this page is visible. */
public final class NetworkActivity extends AppPage {
    private final Handler main=new Handler(Looper.getMainLooper());
    private final ExecutorService worker=Executors.newSingleThreadExecutor();
    private final Runnable tick=()->sample();
    private NetworkCounters.Tracker tracker=new NetworkCounters.Tracker();
    private final Map<Integer,String> labels=new HashMap<>(),packages=new HashMap<>();
    private List<NetworkCounters.Reading> readings=new ArrayList<>();
    private boolean upload,active,busy,touching,closed;private int generation;
    private TextView status,empty;private Button down,up,access;private ListView list;private Rows adapter;private AppAccess permissions;
    static Intent intent(Context c,boolean upload){return new Intent(c,NetworkActivity.class).putExtra("upload",upload);}
    static void open(Context c,boolean upload){c.startActivity(intent(c,upload));}
    @Override public void onCreate(Bundle saved){super.onCreate(saved);upload=saved!=null?saved.getBoolean("upload"):getIntent().getBooleanExtra("upload",false);
        permissions=new AppAccess(this,()->{main.removeCallbacks(tick);main.post(tick);});
        LinearLayout root=ui.root(this);ui.title(root,"应用流量",this::finish);
        LinearLayout intro=ui.column();intro.setPadding(ui.dp(16),ui.dp(14),ui.dp(16),ui.dp(12));intro.setBackground(ui.shape(ui.surface,18));
        status=ui.text("正在读取应用流量…",14,ui.ink);intro.addView(status);
        access=ui.button("连接 Shizuku",permissions::authorize);intro.addView(access);
        root.addView(ui.boundedScroll(intro,.26f,150));ui.gap(root,8);
        LinearLayout label=ui.horizontal();label.addView(ui.text("按实时速度排序",14,ui.muted),new LinearLayout.LayoutParams(0,-2,1));
        label.addView(ui.button("说明",()->new AppDialog.Builder(this).setTitle("流量如何统计")
            .setMessage("按应用 UID 汇总，包含前台和后台。多个应用共享 UID 时，无法拆分到单个应用或子进程。\n\nVPN 可能造成重复计数。这里只显示流量大小，不能据此判断传输了哪些内容。\n\n本次流量从进入页面或重新连接时开始累计。离开页面后停止采样。")
            .setPositiveButton("知道了",null).show()));root.addView(label);
        LinearLayout sort=ui.horizontal();down=ui.button("↓ 下载",()->{upload=false;render();});up=ui.button("↑ 上传",()->{upload=true;render();});
        sort.addView(down,new LinearLayout.LayoutParams(0,-2,1));sort.addView(up,new LinearLayout.LayoutParams(0,-2,1));root.addView(sort);
        FrameLayout frame=new FrameLayout(this);list=new ListView(this);ui.configureList(list);adapter=new Rows();list.setAdapter(adapter);
        list.setOnItemClickListener((parent,view,position,id)->{NetworkCounters.Reading r=adapter.getItem(position);String names=packages.getOrDefault(r.uid,"");new AppDialog.Builder(this)
            .setTitle(labels.getOrDefault(r.uid,"应用归属")).setMessage("UID "+r.uid+"\n\n"+(names.isEmpty()?"归属未解析":names)+"\n\n同一 UID 的流量合并显示，不能拆分到子进程。")
            .setPositiveButton("关闭",null).show();});
        list.setOnScrollListener(new AbsListView.OnScrollListener(){public void onScroll(AbsListView v,int f,int n,int t){}public void onScrollStateChanged(AbsListView v,int s){touching=s!=SCROLL_STATE_IDLE;}});
        list.setOnTouchListener((v,e)->{touching=e.getActionMasked()!=android.view.MotionEvent.ACTION_UP&&e.getActionMasked()!=android.view.MotionEvent.ACTION_CANCEL;return false;});
        frame.addView(list,new FrameLayout.LayoutParams(-1,-1));empty=ui.text("等待两次采样，约 3 秒…",16,ui.muted);empty.setPadding(ui.dp(16),ui.dp(16),ui.dp(16),ui.dp(16));empty.setGravity(Gravity.CENTER);frame.addView(empty,new FrameLayout.LayoutParams(-1,-1));list.setEmptyView(empty);
        ui.gap(root,8);list.setDivider(new android.graphics.drawable.ColorDrawable(android.graphics.Color.TRANSPARENT));list.setDividerHeight(ui.dp(10));
        root.addView(frame,new LinearLayout.LayoutParams(-1,0,1));setContentView(root);render();
    }
    private void sample(){
        if(!active||busy||closed)return;
        access.setVisibility(ShizukuAccess.authorized()?View.GONE:View.VISIBLE);
        if(!ShizukuAccess.authorized()){failed("需要连接并授权 Shizuku，才能读取其他应用的流量。");main.postDelayed(tick,3000);return;}
        busy=true;int token=generation;
        worker.execute(()->{JSONObject result=null;String error=null;
            try{result=ShizukuAccess.request(PrivilegedSampler.NETWORK,null,"",false);}catch(Exception e){error="无法读取实时应用流量。请检查 Shizuku 连接或重试。";}
            JSONObject data=result;String failure=error;
            main.post(()->{busy=false;if(closed||!active)return;
                if(token==generation){if(!ShizukuAccess.authorized())failed("Shizuku 已断开或授权已撤销，请重新连接。");else if(failure!=null)failed(failure);else try{accept(data);}catch(Exception e){failed("系统未提供可用的实时应用流量。此处不显示历史流量估算。");}}
                main.removeCallbacks(tick);main.postDelayed(tick,token==generation?3000:0);
            });
        });
    }
    private void failed(String reason){tracker=new NetworkCounters.Tracker();readings.clear();adapter.values.clear();adapter.notifyDataSetChanged();empty.setText(reason);status.setText("实时流量不可用");}
    private void accept(JSONObject data)throws Exception{
        JSONArray rows=data.getJSONArray("rows");Map<Integer,long[]> counters=new HashMap<>();labels.clear();packages.clear();
        for(int i=0;i<rows.length();i++){JSONObject row=rows.getJSONObject(i);int uid=row.getInt("uid");counters.put(uid,new long[]{row.getLong("rx"),row.getLong("tx")});labels.put(uid,row.getString("label"));
            JSONArray pkgs=row.getJSONArray("packages");StringBuilder names=new StringBuilder();for(int j=0;j<pkgs.length();j++){if(j>0)names.append('\n');names.append(pkgs.getString(j));}packages.put(uid,names.toString());}
        readings=tracker.update(counters,data.getLong("at"));
        boolean ready=false;int transferring=0;for(NetworkCounters.Reading r:readings){ready|=Double.isFinite(r.rx);if(r.rx>0||r.tx>0)transferring++;}
        status.setText(ready?transferring+" 组传输中 · "+new java.text.SimpleDateFormat("HH:mm:ss",Locale.ROOT).format(new Date()):"等待第二次采样…");
        empty.setText(ready?"本次观测尚未发现应用传输流量":"等待两次采样，约 3 秒…");
        if(!touching&&!ui.readingWithAccessibility(list))render();
    }
    private void render(){
        down.setSelected(!upload);up.setSelected(upload);down.setStateDescription(upload?"未选中":"已选中，从高到低");up.setStateDescription(upload?"已选中，从高到低":"未选中");
        down.setBackground(ui.ripple(upload?0:ui.surface,10));up.setBackground(ui.ripple(upload?ui.surface:0,10));
        ArrayList<NetworkCounters.Reading> next=new ArrayList<>();for(NetworkCounters.Reading r:readings)if(r.received>0||r.sent>0)next.add(r);
        next.sort((a,b)->{double av=upload?a.tx:a.rx,bv=upload?b.tx:b.rx;int d=Double.compare(Double.isFinite(bv)?bv:-1,Double.isFinite(av)?av:-1);if(d==0)d=Long.compare(upload?b.sent:b.received,upload?a.sent:a.received);return d!=0?d:Integer.compare(a.uid,b.uid);});
        adapter.values=next;adapter.notifyDataSetChanged();
    }
    private final class Rows extends BaseAdapter {
        List<NetworkCounters.Reading> values=new ArrayList<>();
        public int getCount(){return values.size();}public NetworkCounters.Reading getItem(int n){return values.get(n);}public long getItemId(int n){return getItem(n).uid;}public boolean hasStableIds(){return true;}
        public boolean isEnabled(int n){return true;}
        public View getView(int n,View recycled,ViewGroup parent){NetworkCounters.Reading r=getItem(n);LinearLayout row=ui.column();row.setPadding(ui.dp(16),ui.dp(16),ui.dp(16),ui.dp(16));row.setBackground(ui.shape(ui.surface,18));
            LinearLayout heading=ui.horizontal();TextView title=ui.heading(labels.getOrDefault(r.uid,"UID "+r.uid),16);heading.addView(title,new LinearLayout.LayoutParams(0,-2,1));heading.addView(ui.text("›",22,ui.muted));row.addView(heading);ui.gap(row,12);
            LinearLayout rates=ui.large()?ui.column():ui.horizontal();metric(rates,"↓ 下载",r.rx,r.received);metric(rates,"↑ 上传",r.tx,r.sent);row.addView(rates,new LinearLayout.LayoutParams(-1,-2));return row;
        }
        private void metric(LinearLayout parent,String label,double rate,long total){LinearLayout cell=ui.column();cell.addView(ui.text(label,14,ui.muted));
            TextView value=ui.heading(speed(rate),20);value.setFontFeatureSettings("tnum");cell.addView(value);cell.addView(ui.text("本次 "+Metrics.bytes(total),14,ui.muted));
            LinearLayout.LayoutParams lp=ui.large()?new LinearLayout.LayoutParams(-1,-2):new LinearLayout.LayoutParams(0,-2,1);if(ui.large()&&parent.getChildCount()>0)lp.topMargin=ui.dp(12);parent.addView(cell,lp);}
    }
    private static String speed(double n){return Double.isFinite(n)?Metrics.bytes(n)+"/s":"—";}
    @Override protected void onSaveInstanceState(Bundle b){b.putBoolean("upload",upload);super.onSaveInstanceState(b);}
    @Override protected void onResume(){super.onResume();if(!themeCurrent())return;active=true;generation++;tracker=new NetworkCounters.Tracker();readings.clear();render();main.post(tick);}
    @Override protected void onPause(){active=false;generation++;touching=false;main.removeCallbacks(tick);super.onPause();}
    @Override protected void onDestroy(){closed=true;worker.shutdownNow();main.removeCallbacksAndMessages(null);permissions.close();super.onDestroy();}
}
