package dev.ftop.widget;

import android.os.Bundle;
import android.graphics.Typeface;
import android.widget.*;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import org.json.JSONObject;

public final class ProcessLogActivity extends AppPage {
    private ProcessActions actions;
    private TextView body,status;
    private Button refresh;
    @Override public void onCreate(Bundle state){super.onCreate(state);actions=new ProcessActions(this);LinearLayout root=ui.root(this);ui.title(root,"近期日志",this::finish);
        LinearLayout content=ui.column();root.addView(ui.scroll(content),new LinearLayout.LayoutParams(-1,0,1));
        ProcessEntry p;try{p=ProcessEntry.from(new JSONObject(getIntent().getStringExtra("process")));}catch(Exception e){ui.note(content,"进程信息已失效，请返回并重新选择。");setContentView(root);return;}
        LinearLayout identity=ui.column();identity.setPadding(ui.dp(16),ui.dp(16),ui.dp(16),ui.dp(16));identity.setBackground(ui.shape(ui.surface,18));
        identity.addView(ui.heading(p.appLabel.isEmpty()?p.name:p.appLabel,20));identity.addView(ui.text("PID "+p.pid,14,ui.muted));content.addView(identity,new LinearLayout.LayoutParams(-1,-2));
        LinearLayout toolbar=ui.large()?ui.column():ui.horizontal();status=ui.text("准备读取",14,ui.muted);toolbar.addView(status,ui.large()?new LinearLayout.LayoutParams(-1,-2):new LinearLayout.LayoutParams(0,-2,1));
        LinearLayout controls=ui.horizontal();refresh=ui.button("刷新",()->load(p));controls.addView(refresh);controls.addView(ui.button("说明",()->new AppDialog.Builder(this).setTitle("日志范围")
            .setMessage(p.rawName+"\n\n只读取当前 PID 本次启动后的输出，最多 300 条、128 KiB。时间为开机后秒数。\n\n旧记录可能已覆盖，也可能缺少系统代记事件。打开页面或手动刷新时读取。")
            .setPositiveButton("关闭",null).show()));toolbar.addView(controls);content.addView(toolbar,new LinearLayout.LayoutParams(-1,-2));
        body=ui.text("",14,ui.ink);body.setTypeface(Typeface.MONOSPACE);body.setTextIsSelectable(true);body.setPadding(ui.dp(16),ui.dp(16),ui.dp(16),ui.dp(16));body.setBackground(ui.shape(ui.surface,18));content.addView(body,new LinearLayout.LayoutParams(-1,-2));
        setContentView(root);load(p);
    }
    private void load(ProcessEntry p){refresh.setEnabled(false);status.setText("正在读取…");if(body.getText().length()==0)body.setText("正在读取近期日志…");
        actions.request(p,PrivilegedSampler.DETAIL,"",true,result->{refresh.setEnabled(true);String error=result.optString("error"),text=result.optString("logs");
            if(!error.isEmpty()){status.setText("读取失败 · 可重试");body.setText(error);return;}
            status.setText("已更新 · "+new SimpleDateFormat("HH:mm:ss",Locale.ROOT).format(new Date())+(result.optBoolean("logsTruncated")?" · 已截断":""));
            body.setText(text.isEmpty()?"暂无可读日志":text);
        });}
    @Override protected void onDestroy(){actions.close();super.onDestroy();}
}
