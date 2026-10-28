package dev.ftop.widget;

import android.app.Activity;

import android.appwidget.AppWidgetManager;
import android.content.ComponentName;
import android.content.Intent;
import android.widget.Toast;
import rikka.shizuku.Shizuku;

/** UI entry points only. The authorization protocol and widget providers are unchanged. */
final class AppAccess {
    private final Activity activity;private final Runnable changed;
    private final Shizuku.OnRequestPermissionResultListener listener;
    AppAccess(Activity activity,Runnable changed){
        this.activity=activity;this.changed=changed;
        listener=(code,result)->{if(code!=31||activity.isDestroyed())return;
            if(result==android.content.pm.PackageManager.PERMISSION_GRANTED){toast("已授权，正在刷新桌面卡片");FtopWidget.requestRefresh(activity);}
            else toast("未授权，继续显示本应用进程");changed.run();};
        Shizuku.addRequestPermissionResultListener(listener);
    }
    void close(){Shizuku.removeRequestPermissionResultListener(listener);}
    void toast(String text){Toast.makeText(activity,text,Toast.LENGTH_SHORT).show();}
    void authorize(){
        if(!ShizukuAccess.available()){
            new AppDialog.Builder(activity).setTitle("启动 Shizuku").setMessage("先在 Shizuku 中通过无线调试或电脑启动服务，再回到此处授权。无需 Root；手机重启后需要重新启动服务。")
                .setPositiveButton("打开 Shizuku",(d,w)->{Intent intent=activity.getPackageManager().getLaunchIntentForPackage("moe.shizuku.privileged.api");if(intent!=null)activity.startActivity(intent);else toast("请先安装 Shizuku");}).setNegativeButton("取消",null).show();return;
        }
        if(ShizukuAccess.authorized()){new AppDialog.Builder(activity).setTitle("进程与流量访问").setMessage(ShizukuAccess.status()+"\n\n可以读取系统允许访问的进程和应用流量计数。授权不等同于 Root。")
            .setPositiveButton("知道了",null).show();return;}
        try{if(Shizuku.shouldShowRequestPermissionRationale())new AppDialog.Builder(activity).setTitle("允许读取进程与流量")
            .setMessage("你已拒绝授权。请在 Shizuku 的“已授权应用”中允许 ftop，然后回到此处。")
            .setPositiveButton("知道了",null).show();else Shizuku.requestPermission(31);
        }catch(RuntimeException e){toast("Shizuku 已断开，请重新启动服务");}
    }
    void pin(Class<?> provider){
        AppWidgetManager manager=AppWidgetManager.getInstance(activity);
        if(manager.isRequestPinAppWidgetSupported()&&manager.requestPinAppWidget(new ComponentName(activity,provider),null,null))return;
        new AppDialog.Builder(activity).setTitle("添加桌面卡片").setMessage("回到桌面，长按空白处，进入小组件。找到 ftop；若没有显示，请查看“安卓小部件”入口。")
            .setPositiveButton("知道了",null).show();
    }
}
