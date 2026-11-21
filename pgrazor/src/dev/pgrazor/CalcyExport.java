package dev.pgrazor;
import android.app.Activity;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.widget.Toast;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import org.json.JSONObject;
/** Writes the whole Pokemon storage as a CalcyIV "Import History from CSV" file (CalcyIV skips rows it already has). */
final class CalcyExport {
    private CalcyExport() {}
    // Column order of CalcyIV 3.4x "Export History to CSV".
    private static final String HEADER="Ancestor?,Scan date,Nr,Name,Temp Evo,Gender,Nickname,Level,possibleLevels,CP,HP,Dust cost,min IV%,ØIV%,max IV%,ØATT IV,ØDEF IV,ØHP IV,Unique?,Fast move,Fast move (ID),Special move,Special move (ID),Special move 2,Special move 2 (ID),DPS,LL Rank (min),LL Rank (max),GL Evo,GL Rank (min),GL Rank (max),UL Evo,UL Rank (min),UL Rank (max),Box,Custom1,Custom2,Saved,Egg,Lucky?,Favorite,BuddyBoosted,Form,ShadowForm,MultiForm?,Dynamax,Height (cm),Weight (g),Height Tag,Catch Date,Catch Level";

    static void start(Activity activity){
        try{NativeCore.calcyRows(true);}catch(UnsatisfiedLinkError e){toast(activity,"Unavailable in this build.");return;}
        toast(activity,"Reading Pokémon storage…");
        Handler h=new Handler(Looper.getMainLooper());long deadline=System.currentTimeMillis()+15000;
        h.postDelayed(new Runnable(){public void run(){
            String rows=NativeCore.calcyRows(false);
            if(rows==null){if(System.currentTimeMillis()<deadline)h.postDelayed(this,300);else toast(activity,"Pokémon storage not ready. Open the map and try again.");return;}
            if(rows.startsWith("ERROR\t")){toast(activity,"Export failed: "+rows.substring(6));return;}
            String name="pgrazor-calcy-"+new SimpleDateFormat("yyyyMMdd-HHmm",Locale.US).format(new Date())+".csv";
            GpxDocuments.create(activity,name,"text/csv",new GpxDocuments.Callback(){
                public void onPicked(Uri uri){new Thread(()->{
                    String msg;
                    try{byte[] csv=csv(activity,rows).getBytes(StandardCharsets.UTF_8);
                        try(OutputStream out=activity.getContentResolver().openOutputStream(uri,"wt")){if(out==null)throw new java.io.IOException("Cannot open file");out.write(csv);}
                        msg="Saved. In CalcyIV: Export / Backup › Import History from CSV.";}
                    catch(Exception e){msg="Export failed: "+e.getMessage();}
                    String m=msg;activity.runOnUiThread(()->toast(activity,m));},"pgrazor-calcy-export").start();}
                public void onCancelled(){}
            });
        }},300);
    }
    static String csv(Activity activity,String rows){
        JSONObject names=PokemonSpeciesDialog.readNames(activity);
        String now=new SimpleDateFormat("M/d/yy HH:mm:ss",Locale.US).format(new Date());
        StringBuilder sb=new StringBuilder(HEADER).append('\n');
        for(String line:rows.split("\n")){
            String[] f=line.split("\t",-1);if(f.length<18)continue;
            int atk=num(f[3]),def=num(f[4]),sta=num(f[5]);double iv=Math.round((atk+def+sta)*1000/45.0)/10.0;
            String gender=f[10].equals("1")?"♂":f[10].equals("2")?"♀":"-";
            // shortcut: CalcyIV's ShadowForm codes are inferred (1 normal, 2 shadow, 3 purified); unverified for shadow/purified.
            String shadow=f[11].equals("1")?"2":f[11].equals("2")?"3":"1";
            long created=Long.parseLong(f[17].isEmpty()?"0":f[17]);
            String catchDate=created>0?new SimpleDateFormat("yyyy-MM-dd",Locale.US).format(new Date(created)):"?";
            String[] c={"0",now,f[0],names==null?"":names.optString(f[0],""),"-",gender,f[14],f[6],f[6],f[1],f[2]," - ",
                d(iv),d(iv),d(iv),atk+".0",def+".0",sta+".0","1",or(f[7])," - ",or(f[8])," - ",or(f[9])," - "," - ",
                " - "," - "," - "," - "," - "," - "," - "," - ","Default","","","1","0",f[12],f[13],"0","",shadow,"0"," - ",
                String.valueOf(Math.round(dbl(f[15])*100)),String.valueOf(Math.round(dbl(f[16])*1000))," - ",catchDate," - "};
            for(int i=0;i<c.length;i++){if(i>0)sb.append(',');sb.append(quote(c[i]));}
            sb.append('\n');
        }
        return sb.toString();
    }
    private static String or(String s){return s.isEmpty()?" - ":s;}
    private static String d(double v){return String.format(Locale.US,"%.1f",v);}
    private static int num(String s){try{return Integer.parseInt(s);}catch(NumberFormatException e){return 0;}}
    private static double dbl(String s){try{return Double.parseDouble(s);}catch(NumberFormatException e){return 0;}}
    private static String quote(String s){return s.indexOf(',')<0&&s.indexOf('"')<0?s:'"'+s.replace("\"","\"\"")+'"';}
    private static void toast(Activity a,String m){Toast.makeText(a,m,Toast.LENGTH_LONG).show();}
}
