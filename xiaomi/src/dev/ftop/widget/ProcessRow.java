package dev.ftop.widget;

import android.content.Context;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;

/** Process identity first, explicitly labeled readings below. */
final class ProcessRow extends LinearLayout {
    private final AppUi ui;
    private final TextView name,cpu,memory;
    ProcessRow(Context context){
        super(context);ui=new AppUi(context);setOrientation(VERTICAL);setPadding(0,0,0,ui.dp(8));
        LinearLayout card=ui.column();card.setPadding(ui.dp(16),ui.dp(14),ui.dp(16),ui.dp(14));card.setBackground(ui.ripple(ui.surface,14));addView(card,new LayoutParams(-1,-2));
        LinearLayout heading=ui.horizontal();name=ui.heading("",16);heading.addView(name,new LayoutParams(0,-2,1));heading.addView(ui.text("›",22,ui.muted));card.addView(heading);
        LinearLayout readings=ui.horizontal();readings.setPadding(0,ui.dp(10),0,0);if(ui.large())readings.setOrientation(VERTICAL);
        cpu=ui.text("",14,ui.ink);memory=ui.text("",14,ui.ink);
        readings.addView(cpu,ui.large()?new LayoutParams(-1,-2):new LayoutParams(0,-2,1));readings.addView(memory,ui.large()?new LayoutParams(-1,-2):new LayoutParams(0,-2,1));card.addView(readings);
        setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_YES);card.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS);
    }
    void bind(ProcessEntry entry){
        String label=entry.rawName.equals(entry.packageName)?"主进程":entry.rawName.startsWith(entry.packageName+":")?entry.rawName.substring(entry.packageName.length()+1):entry.name;
        name.setText(label);
        cpu.setText("CPU  "+Metrics.percent(entry.cpu));memory.setText("内存  "+(entry.memoryBytes>=0?Metrics.bytes(entry.memoryBytes):"—"));
        setContentDescription(label+"，进程 "+entry.rawName+"，PID "+entry.pid+"，"+cpu.getText()+"，"+memory.getText()+"，点按查看详情，长按查看操作");
    }
}
