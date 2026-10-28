package dev.ftop.widget;
import android.app.*;
import android.os.*;
import java.util.*;
import java.util.concurrent.*;
import org.json.JSONObject;
/** Existing permission-gated actions, shared by the App list and full detail page. */
final class ProcessActions {
    private final Activity activity;
    private final Handler main=new Handler(Looper.getMainLooper());
    private final ExecutorService commands=Executors.newSingleThreadExecutor();
    ProcessActions(Activity activity){this.activity=activity;}
    private void logs(ProcessEntry p){AppUi.openProcess(activity,ProcessLogActivity.class,p);}
    void close(){commands.shutdown();main.removeCallbacksAndMessages(null);}
    interface Reply {void done(JSONObject data);}
    void request(ProcessEntry p,int code,String action,boolean logs,Reply reply){
        commands.execute(()->{
            JSONObject result;try{result=ShizukuAccess.request(code,p,action,logs);}catch(Exception e){result=new JSONObject();try{result.put("error",e.getMessage()==null?e.toString():e.getMessage());}catch(Exception ignored){}}
            JSONObject value=result;main.post(()->{if(!activity.isFinishing()&&!activity.isDestroyed())reply.done(value);});
        });
    }
    void stopApp(ProcessEntry p){
        AppDialog checking=new AppDialog.Builder(activity).setTitle("停止应用").setMessage("正在检查操作权限…").setNegativeButton("取消",null).show();
        request(p,PrivilegedSampler.DETAIL,"capabilities",false,r->{
            if(!checking.isShowing())return;checking.dismiss();
            if(r.has("error")){message("无法停止应用",r.optString("error"));return;}
            if(!r.optBoolean("packageAllowed")){message("无法停止应用",r.optString("reason","当前权限不足"));return;}
            confirm(p,"force-stop");
        });
    }
    void menu(ProcessEntry p){
        AppDialog loadingDialog=new AppDialog.Builder(activity).setTitle("检查操作权限").setMessage(p.rawName).setNegativeButton("取消",null).show();
        request(p,PrivilegedSampler.DETAIL,"capabilities",false,r->{
            if(!loadingDialog.isShowing())return;loadingDialog.dismiss();
            if(r.has("error")){message("操作不可用",r.optString("error"));return;}
            List<String> labels=new ArrayList<>(),actions=new ArrayList<>();
            labels.add("查看近期日志");actions.add("logs");
            if(r.optBoolean("signalAllowed")){
                labels.add("结束此进程 · PID "+p.pid);actions.add("terminate");
                boolean paused=r.optString("state").equalsIgnoreCase("T");labels.add(paused?"继续此进程":"暂停此进程 · 不释放内存");actions.add(paused?"resume":"pause");
            }
            if(r.optBoolean("packageAllowed")){
                labels.add("清理应用后台");actions.add("background");
                labels.add("停止应用");actions.add("force-stop");
            }
            new AppDialog.Builder(activity).setTitle("进程操作").setItems(labels.toArray(new String[0]),(d,w)->{
                String action=actions.get(w);if(action.equals("logs"))logs(p);else confirm(p,action);
            }).setNeutralButton("权限说明",(d,w)->message("当前权限",r.optString("signalReason")+"\n"+r.optString("reason"))).setNegativeButton("取消",null).show();
        });
    }
    private void confirm(ProcessEntry p,String action){
        boolean app=action.equals("background")||action.equals("force-stop");
        String description=app?"将作用于整个应用："+p.appLabel+"\n"+p.packageName+" · 用户 "+p.userId()+"\n不是只结束选中的子进程。":
            "仅针对进程："+p.rawName+"\nPID "+p.pid+(action.equals("pause")?"\n暂停保留内存，相关功能可能无响应。":"");
        if(action.equals("force-stop"))description="停止“"+p.appLabel+"”及其后台进程。\n重新打开前，通知和后台任务可能暂停，未保存的内容可能丢失。\n\n"+p.packageName+(p.userId()==0?"":" · 用户 "+p.userId());
        String verb=action.equals("background")?"清理应用后台":action.equals("force-stop")?"停止应用":action.equals("terminate")?"结束此进程":action.equals("pause")?"暂停此进程":"继续此进程";
        AppDialog confirmation=new AppDialog.Builder(activity).setTitle(verb).setMessage(description).setNegativeButton("取消",null).setPositiveButton(verb,(d,w)->{
            AppDialog progress=new AppDialog.Builder(activity).setTitle("正在执行").setMessage("完成后重新核对进程状态").setCancelable(false).show();
            request(p,PrivilegedSampler.ACTION,action,false,r->{progress.dismiss();message(r.has("error")?"未完成操作":"操作结果",r.optString("error",r.optString("message")));FtopWidget.requestRefresh(activity);});
        }).show();
        if(!action.equals("resume"))confirmation.getButton(AppDialog.BUTTON_POSITIVE).setTextColor(new AppUi(activity).danger);
    }
    private void message(String title,String body){new AppDialog.Builder(activity).setTitle(title).setMessage(body).setPositiveButton("知道了",null).show();}
}
