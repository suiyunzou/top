package dev.ftop.widget;

import android.content.Context;
import android.content.Intent;
import android.content.pm.ResolveInfo;
import android.system.Os;
import android.system.OsConstants;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.concurrent.TimeUnit;
import org.json.JSONObject;

/** No arbitrary shell interface. Revalidates the process instance and package before every mutation. */
public final class ProcessControl {
    private static final java.util.regex.Pattern LOG_TIME=java.util.regex.Pattern.compile("^\\s*(\\d+\\.\\d+)\\s+.*");
    private final Context context;
    public ProcessControl(Context context) { this.context=context; }
    private static String read(String path) throws IOException {
        try(BufferedReader r=new BufferedReader(new FileReader(path))) {return r.readLine();}
    }
    private ProcessEntry current(int pid) throws Exception {
        if(pid<=1)throw new SecurityException("系统核心进程不可操作");
        String stat=read("/proc/"+pid+"/stat");
        ProcessEntry p=new ProcessEntry(); p.pid=pid; p.uid=Os.stat("/proc/"+pid).st_uid;
        p.started=ProcessMetrics.ticks(stat)[1]; p.system=p.uid%100000<10000;
        p.state=stat.substring(stat.lastIndexOf(')')+2).split(" ")[0];
        byte[] bytes=new byte[512]; int size;
        try(FileInputStream f=new FileInputStream("/proc/"+pid+"/cmdline")) {size=f.read(bytes);}
        int end=0; while(end<size&&bytes[end]!=0)end++;
        new ProcessIdentity(context).resolve(p,new String(bytes,0,end,StandardCharsets.UTF_8)); return p;
    }
    public ProcessEntry validate(ProcessEntry selected) throws Exception {
        ProcessEntry now;
        try {now=current(selected.pid);}catch(Exception e){throw new IllegalStateException("进程已退出或不可读，请刷新列表");}
        if(selected.started<=0||now.started!=selected.started||now.uid!=selected.uid||!now.rawName.equals(selected.rawName)||!now.packageName.equals(selected.packageName))
            throw new SecurityException("进程身份已变更，未执行操作，请重新选择");
        return now;
    }
    private boolean protectedTarget(ProcessEntry p) {
        if(p.uid%100000<10000)return true;
        String pkg=p.packageName;
        if(pkg.equals("dev.ftop.widget")||pkg.equals("moe.shizuku.privileged.api")||pkg.equals("com.android.systemui")||pkg.equals("com.android.settings"))return true;
        ResolveInfo home=context.getPackageManager().resolveActivity(new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_HOME),0);
        return home!=null&&home.activityInfo!=null&&pkg.equals(home.activityInfo.packageName);
    }
    private boolean signalAllowed(ProcessEntry p) {
        if(protectedTarget(p))return false;
        try {Os.kill(p.pid,0);return true;}catch(Exception e){return false;}
    }
    private boolean packageAllowed(ProcessEntry p) {
        if(protectedTarget(p)||!p.packageName.matches("[a-zA-Z0-9_]+(?:\\.[a-zA-Z0-9_]+)+"))return false;
        String[] packages=context.getPackageManager().getPackagesForUid(p.uid);
        // A shared UID can give package-level operations broader scope than this application.
        return packages!=null&&packages.length==1&&packages[0].equals(p.packageName);
    }
    private static final class CommandResult {
        String text; int exit; boolean truncated;
    }
    private static CommandResult command(String... args) throws Exception {
        Process child=new ProcessBuilder(args).redirectErrorStream(true).start();
        ByteArrayOutputStream bytes=new ByteArrayOutputStream(); boolean[] truncated={false};
        Thread reader=new Thread(()->{
            try(InputStream in=child.getInputStream()) {
                byte[] chunk=new byte[4096]; int count;
                while((count=in.read(chunk))!=-1) {
                    int keep=Math.min(count,131072-bytes.size()); if(keep>0)bytes.write(chunk,0,keep);
                    if(keep<count)truncated[0]=true;
                }
            } catch(IOException ignored) { }
        },"ftop-command-output"); reader.setDaemon(true); reader.start();
        try {
            if(!child.waitFor(4,TimeUnit.SECONDS))throw new IOException("系统命令超时，请刷新核实结果");
            reader.join(600); if(reader.isAlive())throw new IOException("系统输出未完成，请重试");
            CommandResult result=new CommandResult(); result.exit=child.exitValue();
            result.text=new String(bytes.toByteArray(),StandardCharsets.UTF_8); result.truncated=truncated[0]; return result;
        } finally {child.destroy(); try {child.getInputStream().close();}catch(Exception ignored) {}}
    }
    public JSONObject detail(ProcessEntry target,boolean logs) throws Exception { return detail(target,logs,true); }
    static String currentLifetimeLogs(String raw,double startedSeconds) {
        StringBuilder kept=new StringBuilder();
        for(String line:raw.split("\n")) {
            java.util.regex.Matcher stamp=LOG_TIME.matcher(line);
            if(stamp.matches())try {if(Double.parseDouble(stamp.group(1))>=startedSeconds)kept.append(line).append('\n');}catch(NumberFormatException ignored) { }
        }
        return kept.toString();
    }
    public JSONObject detail(ProcessEntry target,boolean logs,boolean servicesRequested) throws Exception {
        ProcessEntry p=validate(target); JSONObject result=new JSONObject();
        boolean signals=signalAllowed(p),packages=packageAllowed(p);
        result.put("signalAllowed",signals).put("packageAllowed",packages).put("state",p.state)
            .put("reason",protectedTarget(p)?"核心或受保护应用，仅提供查看":!packages?"归属不明或共享 UID，不提供应用级停止":"当前操作影响整个所属应用")
            .put("signalReason",signals?"可操作此进程；执行时仍会重新核验":"当前权限不能单独结束或暂停此进程");
        if(logs) {
            CommandResult log=command("/system/bin/logcat","-b","main","-b","system","-b","crash","-d","-t","300","--pid="+p.pid,"-v","threadtime","-v","monotonic");
            validate(target); // Do not attribute output to a reused PID.
            String filtered=currentLifetimeLogs(log.text,p.started/(double)Os.sysconf(OsConstants._SC_CLK_TCK));
            result.put("logs",log.exit==0?(filtered.trim().isEmpty()?"缓冲区内无可读记录；不表示没有活动":filtered):"日志不可读："+log.text)
                .put("logsTruncated",log.truncated);
        } else if(servicesRequested&&!p.packageName.isEmpty()) {
            CommandResult services=command("/system/bin/dumpsys","activity","services",p.packageName);
            StringBuilder matched=new StringBuilder();
            for(String block:services.text.split("(?m)(?=^\\s*\\* ServiceRecord\\{)")) {
                if(!block.trim().startsWith("* ServiceRecord{"))continue;
                boolean owns=false;
                for(String line:block.split("\\n"))if(line.contains("app=ProcessRecord{")&&line.matches(".*[ \\t]"+p.pid+":.*"))owns=true;
                if(owns)for(String line:block.split("\\n")) {
                    String t=line.trim();
                    if(t.startsWith("* ServiceRecord{")||t.startsWith("app=ProcessRecord{")||t.startsWith("processName=")||t.startsWith("createTime=")||t.startsWith("startRequested="))matched.append(t).append('\n');
                }
            }
            result.put("services",services.exit!=0?"关联服务不可读":matched.length()==0?"未找到当前 PID 的关联运行服务；不代表没有组件":matched.toString())
                .put("servicesTruncated",services.truncated);
        } else result.put("services","应用归属不明，无法关联服务");
        return result;
    }
    private boolean same(ProcessEntry p) {
        try {ProcessEntry n=current(p.pid); return n.uid==p.uid&&n.started==p.started&&n.rawName.equals(p.rawName);}catch(Exception e){return false;}
    }
    private List<ProcessEntry> packageProcesses(ProcessEntry target) {
        List<ProcessEntry> rows=new ArrayList<>(); File[] dirs=new File("/proc").listFiles(f->f.getName().matches("[0-9]+"));
        if(dirs!=null)for(File dir:dirs)try {
            if(Os.stat(dir.getPath()).st_uid!=target.uid)continue;
            ProcessEntry p=current(Integer.parseInt(dir.getName()));
            if(p.packageName.equals(target.packageName))rows.add(p);
        }catch(Exception ignored) { }
        return rows;
    }
    public JSONObject act(ProcessEntry target,String action) throws Exception {
        if(!Arrays.asList("background","force-stop","terminate","pause","resume").contains(action))throw new SecurityException("不支持的操作");
        ProcessEntry p=validate(target);
        boolean signals=action.equals("terminate")||action.equals("pause")||action.equals("resume");
        if(signals) {
            if(!signalAllowed(p))throw new SecurityException("当前权限不能操作此进程；未改为停止整个应用");
            validate(target);
            Os.kill(p.pid,action.equals("pause")?OsConstants.SIGSTOP:action.equals("resume")?OsConstants.SIGCONT:OsConstants.SIGKILL);
            Thread.sleep(150);
            String status;
            if(action.equals("terminate"))status=same(p)?"请求已发送，进程仍可见":"原进程已退出";
            else if(!same(p))status="原进程已退出";
            else {boolean stopped=current(p.pid).state.equalsIgnoreCase("T"); status=action.equals("pause")?(stopped?"进程已暂停，内存仍保留":"暂停未确认"):(stopped?"恢复未确认":"进程已继续运行");}
            return new JSONObject().put("message",status).put("scope","此进程");
        }
        if(!packageAllowed(p))throw new SecurityException("目标受保护、归属不明或共享 UID，未执行应用级操作");
        List<ProcessEntry> before=packageProcesses(p); validate(target);
        CommandResult result=command("/system/bin/am",action.equals("background")?"kill":"force-stop","--user",Integer.toString(p.userId()),p.packageName);
        if(result.exit!=0||result.text.contains("Exception")||result.text.contains("Error")||result.text.contains("Permission Denial"))
            throw new IllegalStateException("系统拒绝操作："+result.text);
        Thread.sleep(500); List<ProcessEntry> after=packageProcesses(p);
        int exited=0; for(ProcessEntry old:before)if(!same(old))exited++;
        boolean restarted=after.stream().anyMatch(n->before.stream().noneMatch(o->o.pid==n.pid&&o.started==n.started));
        String status="已核实退出 "+exited+" 个原进程；当前可读 "+after.size()+" 个";
        if(restarted)status+="；检测到新进程，应用可能已重启";
        if(exited==0&&action.equals("background"))status+="。系统未清理这些进程，不代表执行失败";
        return new JSONObject().put("message",status).put("scope",p.packageName+" · 用户 "+p.userId()).put("exited",exited).put("remaining",after.size()).put("restarted",restarted);
    }
}
