package dev.ftop.widget;
import java.io.*;
import java.util.*;
public final class NetworkCountersTest {
    private static int checks;
    private static void check(boolean value,String name){if(!value)throw new AssertionError(name);checks++;}
    private static Map<Integer,long[]> values(long rx,long tx){Map<Integer,long[]> m=new HashMap<>();m.put(10123,new long[]{rx,tx});return m;}
    public static void main(String[] args)throws Exception{
        Map<Integer,long[]> parsed=NetworkCounters.parse(new StringReader("historical bucket rb=999999\n  mAppUidStatsMap:\n    uid rxBytes rxPackets txBytes txPackets\n    10123 8000000000 3 7000000000 4\n  mStatsMapA:\nignore"));
        check(parsed.size()==1&&parsed.get(10123)[0]==8000000000L&&parsed.get(10123)[1]==7000000000L,"only live table and correct byte columns");
        for(String bad:new String[]{"history only","mAppUidStatsMap:\nwrong header","mAppUidStatsMap:\nuid rxBytes rxPackets txBytes txPackets\n1 -1 0 2 0","mAppUidStatsMap:\nuid rxBytes rxPackets txBytes txPackets\n1 3 0 2 0\n1 4 0 2 0"}){
            try{NetworkCounters.parse(new StringReader(bad));throw new AssertionError("accepted malformed data");}catch(IOException expected){checks++;}}
        check(NetworkCounters.parse(new StringReader("mAppUidStatsMap:\nuid rxBytes rxPackets txBytes txPackets\nmStatsMapA:\n")).isEmpty(),"empty live table valid");
        NetworkCounters.Tracker t=new NetworkCounters.Tracker();
        NetworkCounters.Reading r=t.update(values(10000,20000),1000).get(0);check(Double.isNaN(r.rx)&&r.received==0,"first sample is baseline, no historic totals");
        r=t.update(values(16000,23000),4000).get(0);check(r.rx==2000&&r.tx==1000&&r.received==6000&&r.sent==3000,"real elapsed time and session totals");
        r=t.update(values(16000,23000),7000).get(0);check(r.rx==0&&r.tx==0&&r.received==6000,"idle is zero with totals retained");
        r=t.update(values(3,4),10000).get(0);check(Double.isNaN(r.rx)&&r.received==6000,"counter reset never negative or spike");
        r=t.update(values(1003,2004),30000).get(0);check(Double.isNaN(r.rx)&&r.received==6000,"long gap establishes baseline");
        t.update(Collections.emptyMap(),33000);r=t.update(values(1000000,2000000),36000).get(0);check(Double.isNaN(r.rx),"returning UID has no fabricated delta");
        System.out.println(checks+" network counter checks passed");
    }
}
