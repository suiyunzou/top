package dev.ftop.widget;
import android.os.Bundle;
import android.widget.LinearLayout;
public final class AboutActivity extends AppPage {
    @Override public void onCreate(Bundle state){super.onCreate(state);LinearLayout root=ui.root(this);ui.title(root,"数据与权限说明",this::finish);
        LinearLayout c=ui.column();root.addView(ui.scroll(c),new LinearLayout.LayoutParams(-1,0,1));
        LinearLayout memory=ui.group(c,"如何读懂数值");
        entry(memory,"设备已用内存","总量 − 可用量，包含系统占用。容量按 1024 换算。");
        entry(memory,"进程 CPU","以单核 100% 计。使用多个核心时，可以超过 100%。");
        entry(memory,"RSS 与 PSS","RSS 包含共享页，直接相加可能重复计数。PSS 按比例分摊共享页。");
        entry(memory,"横线 —","读取受限，或尚未完成采样。它不表示数值为零。");
        LinearLayout update=ui.group(c,"何时更新");
        entry(update,"应用页面","监控页面可见时自动采样。进程详情显示打开时的快照。");
        entry(update,"桌面卡片","显示最近快照。手动刷新约 2 秒；定时刷新约 30 分钟，可能被系统延后。网速为采样窗口内的平均值。");
        LinearLayout access=ui.group(c,"权限与进程操作");
        entry(access,"Shizuku","提供系统允许的 shell 访问能力，不等同于 Root。断开或撤销授权后回到基础模式。手机重启后需重新启动 Shizuku。");
        entry(access,"应用与子进程","清理后台与强行停止作用于整个应用。结束、暂停和继续仅在具备对应信号权限时提供。执行前会重新核对进程身份。");
        entry(access,"如何理解驻留","进程存在或名称包含某个业务词，并不能证明它正在执行该业务。");
        LinearLayout traffic=ui.group(c,"流量与日志");
        entry(traffic,"应用流量","按 UID 汇总前台和后台流量。共享 UID 无法拆分，VPN 可能重复计数。流量大小不说明传输了什么内容。");
        entry(traffic,"近期日志","读取当前 PID 本次启动后的输出，最多 300 条、128 KiB。打开页面或手动刷新时读取。旧记录可能被覆盖，也可能缺少系统代记事件。");ui.gap(c,16);setContentView(root);
    }
    private void entry(LinearLayout box,String title,String detail){ui.setting(box,title,"",()->new AppDialog.Builder(this).setTitle(title).setMessage(detail).setPositiveButton("关闭",null).show());}
}
