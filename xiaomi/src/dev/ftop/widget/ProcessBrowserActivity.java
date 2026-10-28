package dev.ftop.widget;


import android.content.Intent;
import android.os.Bundle;
import android.os.Parcelable;
import android.text.*;
import android.view.*;
import android.widget.*;
import java.util.*;

/** Real foreground samples, with separate navigation state for app and process lists. */
public final class ProcessBrowserActivity extends AppPage {
    private ForegroundSampler sampler;private ProcessActions actions;
    private boolean byCpu,touching,binding,direct;
    private int filter;private String selected="",query="",parentQuery="";
    private int parentFilter;private boolean parentCpu;
    private Parcelable parentPosition,pendingPosition;
    private Snapshot snapshot=new Snapshot();
    private TextView title,scope,status,emptyTitle,emptyText,processHeading;
    private ResourceSummaryView summary;
    private boolean searchExpanded;
    private String detailName="应用详情",detailPackage="",iconPackage="";
    private final java.util.concurrent.ExecutorService iconLoader=java.util.concurrent.Executors.newSingleThreadExecutor();
    private ListView list;private AppAdapter apps;private ProcessAdapter processes;
    private Button sort,category,emptyAction,stopApp,searchToggle;private EditText search;private LinearLayout appInfo;
    private ProcessEntry stopTarget;
    @Override public void onCreate(Bundle state){
        super.onCreate(state);actions=new ProcessActions(this);
        selected=state==null?getIntent().getStringExtra("groupKey"):state.getString("selected","");if(selected==null)selected="";
        direct=state==null?!selected.isEmpty():state.getBoolean("direct");
        if(state!=null){query=state.getString("query","");byCpu=state.getBoolean("byCpu");filter=state.getInt("filter");parentQuery=state.getString("parentQuery","");parentFilter=state.getInt("parentFilter");parentCpu=state.getBoolean("parentCpu");parentPosition=state.getParcelable("parentPosition");pendingPosition=state.getParcelable("position");}
        LinearLayout root=ui.root(this);title=ui.title(root,getString(R.string.app_running),this::goBack);
        Button about=ui.button("说明",()->{if(selected.isEmpty())startActivity(new Intent(this,AboutActivity.class));else new AppDialog.Builder(this).setTitle(detailName).setMessage((detailPackage.isEmpty()?"":detailPackage+"\n\n")+"CPU 按单核计。内存为可读进程合计，RSS 可能包含共享页。\n\n点击进程查看详情；长按查看可用操作。停止应用会作用于整个应用。").setPositiveButton("知道了",null).setNeutralButton("全部说明",(d,w)->startActivity(new Intent(this,AboutActivity.class))).show();});about.setContentDescription("数据说明");((LinearLayout)title.getParent()).addView(about,new LinearLayout.LayoutParams(-2,-2));
        if(state!=null){detailName=state.getString("detailName","应用详情");detailPackage=state.getString("detailPackage","");searchExpanded=state.getBoolean("searchExpanded");}
        LinearLayout top=ui.column();appInfo=ui.column();summary=new ResourceSummaryView(this,ui.dark);appInfo.addView(summary,new LinearLayout.LayoutParams(-1,-2));ui.gap(appInfo,8);top.addView(appInfo);
        root.addView(ui.boundedScroll(top,.38f,330),new LinearLayout.LayoutParams(-1,-2));
        search=new EditText(this);search.setId(R.id.app_search_input);search.setSingleLine(true);search.setTextSize(14);search.setTextColor(ui.ink);search.setHintTextColor(ui.muted);search.setPadding(ui.dp(14),ui.dp(6),ui.dp(14),ui.dp(6));search.setMinHeight(ui.dp(48));search.setBackground(ui.shape(ui.surface,12));search.setText(query);root.addView(search,new LinearLayout.LayoutParams(-1,-2));
        search.addTextChangedListener(new TextWatcher(){public void beforeTextChanged(CharSequence s,int a,int c,int f){}public void onTextChanged(CharSequence s,int a,int b,int c){if(!binding){query=s.toString();render(true);}}public void afterTextChanged(Editable e){}});
        LinearLayout controls=ui.horizontal();sort=ui.button("内存排序",()->new AppDialog.Builder(this).setTitle("排序方式").setSingleChoiceItems(new String[]{"内存从高到低","CPU 从高到低"},byCpu?1:0,(d,w)->{d.dismiss();byCpu=w==1;render(true);}).setNegativeButton("取消",null).show());
        category=ui.button("全部",()->new AppDialog.Builder(this).setTitle("应用分类").setSingleChoiceItems(new String[]{"全部","应用","系统","归属不明"},filter,(d,w)->{d.dismiss();filter=w;render(true);}).setNegativeButton("取消",null).show());
        processHeading=ui.heading("进程",16);controls.addView(processHeading,new LinearLayout.LayoutParams(0,-2,1));
        controls.addView(sort,new LinearLayout.LayoutParams(-2,-2));controls.addView(category,new LinearLayout.LayoutParams(0,-2,1));
        searchToggle=ui.button("搜索",()->{searchExpanded=!searchExpanded;if(!searchExpanded){query="";binding=true;search.setText("");binding=false;search.clearFocus();getWindow().getInsetsController().hide(WindowInsets.Type.ime());}render(true);});controls.addView(searchToggle,new LinearLayout.LayoutParams(-2,-2));root.addView(controls);
        scope=ui.text("正在采样…",13,ui.muted);scope.setPadding(0,ui.dp(5),0,ui.dp(8));root.addView(scope);
        list=new ListView(this);list.setId(R.id.process_list);ui.configureList(list);apps=new AppAdapter(this);processes=new ProcessAdapter(this);list.setAdapter(apps);
        list.setOnTouchListener((v,e)->{touching=e.getActionMasked()!=MotionEvent.ACTION_UP&&e.getActionMasked()!=MotionEvent.ACTION_CANCEL;return false;});
        list.setOnScrollListener(new AbsListView.OnScrollListener(){public void onScroll(AbsListView v,int f,int n,int t){}public void onScrollStateChanged(AbsListView v,int s){touching=s!=SCROLL_STATE_IDLE;}});
        list.setOnItemClickListener((a,v,p,id)->{if(selected.isEmpty())enter(apps.getItem(p).key);else AppUi.openProcess(this,ProcessDetailActivity.class,processes.getItem(p));});
        list.setOnItemLongClickListener((a,v,p,id)->{if(selected.isEmpty())enter(apps.getItem(p).key);else actions.menu(processes.getItem(p));return true;});
        FrameLayout frame=new FrameLayout(this);frame.addView(list,new FrameLayout.LayoutParams(-1,-1));LinearLayout empty=ui.column();empty.setGravity(Gravity.CENTER);emptyTitle=ui.heading("",17);emptyTitle.setGravity(Gravity.CENTER);emptyText=ui.text("",14,ui.muted);emptyText.setGravity(Gravity.CENTER);empty.addView(emptyTitle);empty.addView(emptyText);emptyAction=ui.button("清除筛选",()->{filter=0;query="";binding=true;search.setText("");binding=false;render(true);});empty.addView(emptyAction);
        ScrollView emptyScroll=ui.scroll(empty);emptyScroll.setFillViewport(true);frame.addView(emptyScroll,new FrameLayout.LayoutParams(-1,-1));list.setEmptyView(emptyScroll);root.addView(frame,new LinearLayout.LayoutParams(-1,0,1));
        stopApp=ui.button("停止应用",()->{ProcessEntry target=stopTarget;if(target!=null)actions.stopApp(target);});stopApp.setId(R.id.stop_application);stopApp.setTextColor(new android.content.res.ColorStateList(new int[][]{new int[]{-android.R.attr.state_enabled},new int[]{}},new int[]{ui.muted,ui.danger}));stopApp.setBackground(ui.ripple(ui.surface,14));LinearLayout.LayoutParams stopSize=new LinearLayout.LayoutParams(-1,-2);stopSize.topMargin=ui.dp(10);stopSize.bottomMargin=ui.dp(8);root.addView(stopApp,stopSize);
        status=ui.text("",12,ui.muted);status.setGravity(Gravity.CENTER);status.setPadding(0,0,0,ui.dp(6));root.addView(status);
        setContentView(root);snapshot=Store.snapshot(this);render(true);sampler=new ForegroundSampler(this,1500,s->{snapshot=s;render(false);});
    }
    private boolean matches(String value){return value.toLowerCase(Locale.ROOT).contains(query.toLowerCase(Locale.ROOT));}
    private void enter(String key){parentQuery=query;parentFilter=filter;parentCpu=byCpu;parentPosition=list.onSaveInstanceState();selected=key;direct=false;query="";searchExpanded=false;detailName="应用详情";detailPackage="";binding=true;search.setText("");binding=false;search.clearFocus();getWindow().getInsetsController().hide(WindowInsets.Type.ime());render(true);list.setSelection(0);}
    private void render(boolean explicit){
        if(list==null||!explicit&&(touching||ui.readingWithAccessibility(list)))return;
        sort.setText(selected.isEmpty()?(byCpu?"CPU 排序 ↓":"内存排序 ↓"):(byCpu?"CPU ↓":"内存 ↓"));sort.setStateDescription("从高到低");category.setText(new String[]{"全部","应用","系统","归属不明"}[filter]);
        boolean browsing=selected.isEmpty();category.setVisibility(browsing?View.VISIBLE:View.GONE);appInfo.setVisibility(browsing?View.GONE:View.VISIBLE);stopApp.setVisibility(browsing?View.GONE:View.VISIBLE);stopTarget=null;
        processHeading.setVisibility(browsing?View.GONE:View.VISIBLE);searchToggle.setVisibility(browsing?View.GONE:View.VISIBLE);searchToggle.setText(searchExpanded?"收起":"搜索");searchToggle.setContentDescription(searchExpanded?"收起进程搜索":"搜索进程");search.setVisibility(browsing||searchExpanded?View.VISIBLE:View.GONE);
        search.setHint(getString(browsing?R.string.app_search:R.string.app_search_process));search.setContentDescription(search.getHint());
        List<AppGroup> groups=AppGroup.collect(snapshot.processes,byCpu);
        if(browsing){
            title.setText(getString(R.string.app_running));scope.setVisibility(View.VISIBLE);if(list.getAdapter()!=apps)list.setAdapter(apps);list.setDivider(new android.graphics.drawable.ColorDrawable(android.graphics.Color.TRANSPARENT));list.setDividerHeight(ui.dp(8));
            List<AppGroup> visible=new ArrayList<>();for(AppGroup g:groups)if((filter==0||filter==1&&!g.packageName.isEmpty()&&!g.system||filter==2&&g.system||filter==3&&g.packageName.isEmpty())&&(matches(g.title()+" "+g.packageName)||g.members.stream().anyMatch(p->matches(p.rawName))))visible.add(g);
            apps.setByCpu(byCpu);apps.update(visible);scope.setText(visible.size()+" 项");
            emptyTitle.setText(query.isEmpty()&&filter==0?"暂无可读进程":"没有匹配的应用");emptyText.setText(query.isEmpty()&&filter==0?"等待采样；可在设置中连接 Shizuku。":"换个名称，或清除当前筛选。");
        }else{
            title.setText("应用详情");AppGroup found=null;for(AppGroup g:groups)if(g.key.equals(selected)){found=g;break;}
            if(found!=null&&!found.members.isEmpty()){stopTarget=found.members.get(0);for(ProcessEntry p:found.members)if(p.rawName.equals(found.packageName)){stopTarget=p;break;}}
            if(list.getAdapter()!=processes)list.setAdapter(processes);list.setDivider(null);list.setDividerHeight(0);List<ProcessEntry> rows=new ArrayList<>();if(found!=null)for(ProcessEntry p:found.members)if(matches(p.name+" "+p.rawName+" "+p.pid))rows.add(p);
            if(found!=null){detailName=found.title();detailPackage=found.packageName;}
            bindIcon(detailPackage);
            summary.bind(detailName,"",found==null?"当前无可读进程":found.members.size()+" 个进程可见",found==null?"—":found.cpuText(),found==null?"—":found.memoryValue(),"");
            scope.setVisibility(query.isEmpty()?View.GONE:View.VISIBLE);processes.update(rows,list);processHeading.setText("进程"+(found==null?"":" · "+found.members.size()));scope.setText(query.isEmpty()?(found==null?"":"点按查看详情 · 长按可用操作"):"匹配 "+rows.size()+" 个进程");
            emptyTitle.setText(query.isEmpty()?"当前无可读进程":"没有匹配的进程");emptyText.setText(query.isEmpty()?"应用已退出或访问受限":"换个关键词试试");
        }
        stopApp.setEnabled(stopTarget!=null);emptyAction.setVisibility(query.isEmpty()&&(!browsing||filter==0)?View.GONE:View.VISIBLE);status.setText(CardRenderer.updated(snapshot));
        if(pendingPosition!=null&&list.getCount()>0){list.onRestoreInstanceState(pendingPosition);pendingPosition=null;}
    }
    private void bindIcon(String packageName){
        if(packageName.equals(iconPackage))return;iconPackage=packageName;summary.setIcon(getDrawable(android.R.drawable.sym_def_app_icon));
        if(packageName.isEmpty())return;
        iconLoader.execute(()->{try{android.graphics.drawable.Drawable icon=getPackageManager().getApplicationIcon(packageName);runOnUiThread(()->{if(!isDestroyed()&&packageName.equals(iconPackage))summary.setIcon(icon);});}catch(Exception ignored){}});
    }
    private void goBack(){
        if(selected.isEmpty()||direct){finish();return;}
        selected="";query=parentQuery;filter=parentFilter;byCpu=parentCpu;binding=true;search.setText(query);binding=false;pendingPosition=parentPosition;render(true);
    }
    @Override public void onBackPressed(){
        WindowInsets insets=getWindow().getDecorView().getRootWindowInsets();
        if(insets!=null&&insets.isVisible(WindowInsets.Type.ime())){
            getWindow().getInsetsController().hide(WindowInsets.Type.ime());search.clearFocus();return;
        }
        goBack();
    }
    @Override protected void onSaveInstanceState(Bundle b){b.putString("detailName",detailName);b.putString("detailPackage",detailPackage);b.putBoolean("searchExpanded",searchExpanded);b.putString("selected",selected);b.putString("query",query);b.putBoolean("byCpu",byCpu);b.putInt("filter",filter);b.putBoolean("direct",direct);b.putString("parentQuery",parentQuery);b.putInt("parentFilter",parentFilter);b.putBoolean("parentCpu",parentCpu);b.putParcelable("parentPosition",parentPosition);b.putParcelable("position",list.onSaveInstanceState());super.onSaveInstanceState(b);}
    @Override protected void onResume(){super.onResume();if(themeCurrent())sampler.resume();}
    @Override protected void onPause(){sampler.pause();touching=false;super.onPause();}
    @Override protected void onDestroy(){sampler.close();actions.close();apps.close();iconLoader.shutdownNow();super.onDestroy();}
}
