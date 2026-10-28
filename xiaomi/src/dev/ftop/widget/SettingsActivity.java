package dev.ftop.widget;


import android.content.Intent;
import android.os.Bundle;
import android.widget.*;

public final class SettingsActivity extends AppPage {
    private AppAccess access;
    private TextView permission;
    private Button permissionAction;

    @Override public void onCreate(Bundle state){
        super.onCreate(state);access=new AppAccess(this,this::updatePermission);
        LinearLayout root=ui.root(this);TextView title=ui.title(root,getString(R.string.app_settings),this::finish);
        ((LinearLayout)title.getParent()).addView(ui.button("说明",()->new AppDialog.Builder(this).setTitle("设置说明")
            .setMessage("Shizuku 授权用于读取系统允许访问的进程和应用流量计数。无需 Root；手机重启后需要重新启动服务。\n\n未授权时，仍可显示能够直接读取的数据。\n\n显示模式同时应用于应用与桌面卡片。")
            .setPositiveButton("知道了",null).show()));
        LinearLayout content=ui.column();root.addView(ui.scroll(content),new LinearLayout.LayoutParams(-1,0,1));
        LinearLayout permissions=ui.group(content,"访问权限");
        LinearLayout accessCard=ui.column();accessCard.setPadding(ui.dp(16),ui.dp(16),ui.dp(16),ui.dp(12));permissions.addView(accessCard,new LinearLayout.LayoutParams(-1,-2));
        accessCard.addView(ui.heading("进程与应用流量",18));ui.gap(accessCard,8);
        permission=ui.text("",16,ui.ink);accessCard.addView(permission);
        ui.gap(accessCard,8);permissionAction=ui.button("",access::authorize);accessCard.addView(permissionAction,new LinearLayout.LayoutParams(-1,-2));

        LinearLayout display=ui.group(content,"外观");String[] names={"跟随系统","深色","浅色"};
        ui.setting(display,"显示模式",names[themeIndex()],()->new AppDialog.Builder(this).setTitle("显示模式")
            .setSingleChoiceItems(names,themeIndex(),(dialog,which)->{dialog.dismiss();if(which!=Store.theme(this)){Store.theme(this,which);FtopWidget.requestRefresh(this);recreate();}}).setNegativeButton("取消",null).show());

        LinearLayout widgets=ui.group(content,"桌面卡片");
        ui.setting(widgets,"添加监控卡片","",()->access.pin(FtopWidget.class));
        ui.setting(widgets,"添加进程卡片","4 × 4",()->access.pin(ProcessWidget.class));
        ui.setting(widgets,"刷新卡片","",()->{FtopWidget.requestRefresh(this);access.toast("正在刷新桌面卡片");});
        LinearLayout about=ui.group(content,"关于");
        ui.setting(about,"数据与权限说明","",()->startActivity(new Intent(this,AboutActivity.class)));
        TextView version=ui.text("ftop · "+BuildVersion.name(this),13,ui.muted);
        version.setGravity(android.view.Gravity.CENTER);version.setPadding(0,ui.dp(24),0,ui.dp(20));content.addView(version);
        updatePermission();setContentView(root);
    }
    private int themeIndex(){return Math.max(0,Math.min(2,Store.theme(this)));}
    private void updatePermission(){
        if(permission==null)return;
        boolean running=ShizukuAccess.available(),authorized=ShizukuAccess.authorized();
        permission.setText(authorized?"Shizuku · 已授权":running?"Shizuku · 等待授权":"Shizuku · 未运行");
        permissionAction.setText(authorized?"查看授权说明":running?"授权访问":"查看启动方式");
    }
    @Override protected void onResume(){super.onResume();updatePermission();}
    @Override protected void onDestroy(){if(access!=null)access.close();super.onDestroy();}
}
