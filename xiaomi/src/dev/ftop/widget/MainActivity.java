package dev.ftop.widget;

import android.content.Intent;
import android.os.Bundle;
import android.view.View;
import android.widget.*;

public final class MainActivity extends AppPage {
    private ForegroundSampler sampler;private AppAccess permissions;
    private MonitorPanel preview;private boolean byCpu,touching;
    private TextView processScope;private Button cpuSort,memorySort,access;
    private ListView processList;private AppAdapter processAdapter;
    private Snapshot shown=new Snapshot();
    @Override public void onCreate(Bundle state){
        super.onCreate(state);byCpu=state!=null&&state.getBoolean("byCpu");permissions=new AppAccess(this,this::updateAccess);
        LinearLayout root=ui.root(this),nav=ui.horizontal();nav.setMinimumHeight(ui.dp(60));
        nav.addView(ui.heading("ftop",26),new LinearLayout.LayoutParams(0,-2,1));
        nav.addView(ui.iconButton("设备",false,()->startActivity(new Intent(this,DeviceActivity.class))),new LinearLayout.LayoutParams(ui.dp(48),ui.dp(48)));
        nav.addView(ui.iconButton("设置",true,()->startActivity(new Intent(this,SettingsActivity.class))),new LinearLayout.LayoutParams(ui.dp(48),ui.dp(48)));root.addView(nav);
        int width=getResources().getConfiguration().screenWidthDp,height=getResources().getConfiguration().screenHeightDp;
        boolean wide=width>=600&&(!ui.large()||width>=840);
        LinearLayout body=new LinearLayout(this);body.setOrientation(wide?LinearLayout.HORIZONTAL:LinearLayout.VERTICAL);root.addView(body,new LinearLayout.LayoutParams(-1,0,1));
        LinearLayout monitor=ui.column(),listHost=ui.column();
        preview=new MonitorPanel(this);preview.setContentDescription("实时设备状态");monitor.addView(preview,new LinearLayout.LayoutParams(-1,-2));
        access=ui.button("连接 Shizuku",permissions::authorize);access.setTextSize(13);preview.addView(access,new LinearLayout.LayoutParams(-1,-2));
        ScrollView monitorScroll=wide?ui.scroll(monitor):ui.boundedScroll(monitor,height<700?.40f:.46f,ui.large()?310:350);
        LinearLayout.LayoutParams ml=new LinearLayout.LayoutParams(wide?0:-1,wide?-1:-2,wide?1:0);if(wide)ml.setMarginEnd(ui.dp(22));
        body.addView(monitorScroll,ml);body.addView(listHost,new LinearLayout.LayoutParams(wide?0:-1,wide?-1:0,1));
        LinearLayout header=ui.horizontal();header.setPadding(ui.dp(14),0,0,0);header.addView(ui.heading(getString(R.string.app_running),17),new LinearLayout.LayoutParams(0,-2,1));
        Button all=ui.button("›",()->startActivity(new Intent(this,ProcessBrowserActivity.class)));all.setTextSize(26);all.setTextColor(ui.ink);all.setContentDescription("全部");all.setTooltipText("查看全部运行应用");
        LinearLayout sorting=ui.horizontal();processScope=ui.text("",13,ui.muted);
        cpuSort=ui.button("CPU",()->{byCpu=true;renderApps(true);});memorySort=ui.button("内存",()->{byCpu=false;renderApps(true);});
        if(ui.large()){header.addView(all);listHost.addView(header);sorting.addView(processScope,new LinearLayout.LayoutParams(0,-2,1));sorting.addView(cpuSort);sorting.addView(memorySort);listHost.addView(sorting);}
        else{header.addView(cpuSort);header.addView(memorySort);header.addView(all);listHost.addView(header);}
        processList=new ListView(this);processList.setId(R.id.process_list);ui.configureList(processList);processList.setDivider(new android.graphics.drawable.ColorDrawable(android.graphics.Color.TRANSPARENT));processList.setDividerHeight(ui.dp(8));processList.setContentDescription("运行应用列表，上下滑动查看");
        processAdapter=new AppAdapter(this);processList.setAdapter(processAdapter);
        processList.setOnTouchListener((v,e)->{touching=e.getActionMasked()!=android.view.MotionEvent.ACTION_UP&&e.getActionMasked()!=android.view.MotionEvent.ACTION_CANCEL;return false;});
        processList.setOnScrollListener(new AbsListView.OnScrollListener(){public void onScroll(AbsListView v,int f,int n,int t){}public void onScrollStateChanged(AbsListView v,int s){touching=s!=SCROLL_STATE_IDLE;}});
        processList.setOnItemClickListener((a,v,p,id)->startActivity(new Intent(this,ProcessBrowserActivity.class).putExtra("groupKey",processAdapter.getItem(p).key)));
        FrameLayout frame=new FrameLayout(this);frame.addView(processList,new FrameLayout.LayoutParams(-1,-1));
        LinearLayout empty=ui.column();empty.setGravity(android.view.Gravity.CENTER);TextView hint=ui.text("暂无可读进程\n连接 Shizuku 后可查看更多应用",14,ui.muted);hint.setGravity(android.view.Gravity.CENTER);empty.addView(hint);empty.addView(ui.button("连接 Shizuku",permissions::authorize));
        frame.addView(empty,new FrameLayout.LayoutParams(-1,-1));processList.setEmptyView(empty);listHost.addView(frame,new LinearLayout.LayoutParams(-1,0,1));

        setContentView(root);update(Store.snapshot(this));sampler=new ForegroundSampler(this,1000,this::update);
        if(state!=null){android.os.Parcelable saved=state.getParcelable("list");if(saved!=null)processList.onRestoreInstanceState(saved);}
    }
    private void updateAccess(){if(access!=null){boolean ready=ShizukuAccess.authorized();access.setVisibility(ready?View.GONE:View.VISIBLE);access.setText("连接 Shizuku ›");}}
    private void update(Snapshot s){shown=s;preview.update(s);updateAccess();renderApps(false);}
    private void renderApps(boolean explicit){
        if(!explicit&&(touching||ui.readingWithAccessibility(processList)))return;
        processAdapter.setByCpu(byCpu);processAdapter.update(AppGroup.collect(shown.processes,byCpu));
        cpuSort.setTextColor(byCpu?ui.ink:ui.muted);memorySort.setTextColor(byCpu?ui.muted:ui.ink);
        cpuSort.setBackground(ui.ripple(byCpu?ui.surface:android.graphics.Color.TRANSPARENT,10));memorySort.setBackground(ui.ripple(byCpu?android.graphics.Color.TRANSPARENT:ui.surface,10));
        cpuSort.setSelected(byCpu);memorySort.setSelected(!byCpu);cpuSort.setStateDescription(byCpu?"已选中，从高到低":"未选中");memorySort.setStateDescription(byCpu?"未选中":"已选中，从高到低");
        processScope.setText(processAdapter.getCount()+" 项");
    }
    @Override protected void onSaveInstanceState(Bundle b){b.putBoolean("byCpu",byCpu);b.putParcelable("list",processList.onSaveInstanceState());super.onSaveInstanceState(b);}
    @Override protected void onResume(){super.onResume();if(themeCurrent()){updateAccess();sampler.resume();}}
    @Override protected void onPause(){sampler.pause();touching=false;super.onPause();}
    @Override protected void onDestroy(){sampler.close();permissions.close();processAdapter.close();super.onDestroy();}
}
