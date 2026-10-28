package dev.ftop.widget;

import android.os.Bundle;
import android.widget.*;
import org.json.JSONObject;

public final class ProcessDetailActivity extends AppPage {
    private ProcessActions actions;
    @Override public void onCreate(Bundle state){
        super.onCreate(state);actions=new ProcessActions(this);LinearLayout root=ui.root(this);ui.title(root,"进程详情",this::finish);
        ProcessEntry p;
        try{p=ProcessEntry.from(new JSONObject(getIntent().getStringExtra("process")));}catch(Exception e){ui.note(root,"进程信息已失效，请返回并重新选择。");setContentView(root);return;}
        LinearLayout c=ui.column();root.addView(ui.scroll(c),new LinearLayout.LayoutParams(-1,0,1));
        ResourceSummaryView summary=new ResourceSummaryView(this,ui.dark);summary.setIcon(getPackageManager().getDefaultActivityIcon());
        if(!p.packageName.isEmpty())try{summary.setIcon(getPackageManager().getApplicationIcon(p.packageName));}catch(android.content.pm.PackageManager.NameNotFoundException|SecurityException ignored){}
        summary.bind(p.name,"","PID "+p.pid+" · 打开时快照",Metrics.percent(p.cpu),Metrics.bytes(p.memoryBytes),"");c.addView(summary,new LinearLayout.LayoutParams(-1,-2));
        LinearLayout details=ui.group(c,"更多信息");ui.setting(details,"进程身份","",()->new AppDialog.Builder(this).setTitle("进程身份")
            .setMessage("所属应用\n"+(p.appLabel.isEmpty()?"归属未解析":p.appLabel)+"\n\n进程名\n"+p.rawName+"\n\n包名\n"+(p.packageName.isEmpty()?"—":p.packageName)+"\n\nUID · 用户空间\n"+p.uid+" · "+p.userId()+"\n\n系统状态\n"+(p.state.isEmpty()?"—":p.state))
            .setPositiveButton("关闭",null).show());
        final String[] serviceText={"正在读取…"};final AppDialog[] serviceDialog={null};
        ui.setting(details,"运行服务","",()->{serviceDialog[0]=new AppDialog.Builder(this).setTitle("关联运行服务").setMessage(serviceText[0]+"\n\n组件与进程不是一一对应。名称不证明具体用途。").setPositiveButton("关闭",null).show();});
        ui.setting(details,"数值说明","",()->new AppDialog.Builder(this).setTitle("资源快照").setMessage("显示打开页面时的读数。返回列表可查看最新数值。\n\nCPU 以单核 100% 计。内存口径为 "+p.memoryKind+"，RSS 可能包含共享页。")
            .setPositiveButton("关闭",null).show());
        LinearLayout buttons=ui.large()?ui.column():ui.horizontal();buttons.addView(ui.button("近期日志",()->AppUi.openProcess(this,ProcessLogActivity.class,p)),actionSize());
        buttons.addView(ui.button("更多操作",()->actions.menu(p)),actionSize());root.addView(buttons);setContentView(root);
        actions.request(p,PrivilegedSampler.DETAIL,"",false,result->{String error=result.optString("error"),text=result.optString("services");serviceText[0]=!error.isEmpty()?error:(text.isEmpty()?"当前未报告关联服务":text)+(result.optBoolean("servicesTruncated")?"\n服务输出已截断":"");if(serviceDialog[0]!=null&&serviceDialog[0].isShowing())serviceDialog[0].setMessage(serviceText[0]+"\n\n组件与进程不是一一对应。名称不证明具体用途。");});
    }
    private LinearLayout.LayoutParams actionSize(){return ui.large()?new LinearLayout.LayoutParams(-1,-2):new LinearLayout.LayoutParams(0,-2,1);}
    @Override protected void onDestroy(){actions.close();super.onDestroy();}
}
