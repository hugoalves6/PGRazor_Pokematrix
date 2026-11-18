package dev.pgrazor;
import android.app.Activity;
import android.app.AlertDialog;
import android.os.Handler;
import android.os.Looper;
import java.util.Locale;

/** Shared confirmation for every user teleport; no action runs before explicit confirmation. */
public final class TeleportDialog {
    private TeleportDialog() {}
    public interface Action { boolean apply(double latitude,double longitude); }
    private static AlertDialog active;
    static String duration(long millis) {
        long seconds=(Math.max(0,millis)+999)/1000;
        return String.format(Locale.US,"%dh %02dm %02ds",seconds/3600,(seconds/60)%60,seconds%60);
    }
    private static String warning(double lat,double lon) {
        return String.format(Locale.US,"Destination: %.6f, %.6f\n\n",lat,lon)
            +"Cooldown when you arrive: "+duration(Cooldown.remainingAt(lat,lon));
    }
    public interface Remember { void choice(boolean teleport); }
    /** Double-tap variant: Walk, Teleport or Cancel, with "Don't ask again" applied to the chosen action. */
    public static AlertDialog confirmMapTap(Activity activity,double lat,double lon,Action teleport,Action walk,Remember remember) {
        if(activity.isFinishing()||activity.isDestroyed())return null;
        if(!CooldownState.valid(lat,lon))throw new IllegalArgumentException("Invalid teleport coordinates");
        if(active!=null&&active.isShowing())return active;
        Handler main=new Handler(Looper.getMainLooper());
        android.content.Context ui=MapTheme.wrap(activity);
        android.widget.LinearLayout body=new android.widget.LinearLayout(ui);body.setOrientation(android.widget.LinearLayout.VERTICAL);
        int pad=MapTheme.dp(ui,20);body.setPadding(pad,pad/2,pad,0);
        android.widget.TextView message=new android.widget.TextView(ui);message.setText(warning(lat,lon));body.addView(message);
        android.widget.CheckBox noAsk=new android.widget.CheckBox(ui);noAsk.setText("Don't ask again");body.addView(noAsk);
        android.widget.TextView hint=new android.widget.TextView(ui);hint.setText("Walk turns double-tap teleport off; Teleport skips this question. Change it later in Game map tap.");hint.setTextSize(12);body.addView(hint);
        AlertDialog dialog=new AlertDialog.Builder(ui).setTitle("Walk or teleport?").setView(body)
            .setNegativeButton("Cancel",null).setNeutralButton("Walk",null).setPositiveButton("Teleport",null).create();
        Runnable refresh=new Runnable(){public void run(){if(dialog.isShowing()){message.setText(warning(lat,lon));main.postDelayed(this,1000);}}};
        active=dialog;
        DialogLifecycle.bind(activity,dialog,()->{main.removeCallbacksAndMessages(null);if(active==dialog)active=null;});
        dialog.setOnShowListener(d->{MapTheme.style(body);MapTheme.styleButtons(dialog);main.post(refresh);
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v->{if(teleport.apply(lat,lon)){if(noAsk.isChecked())remember.choice(true);dialog.dismiss();}});
            dialog.getButton(AlertDialog.BUTTON_NEUTRAL).setOnClickListener(v->{if(walk.apply(lat,lon)){if(noAsk.isChecked())remember.choice(false);dialog.dismiss();}});
        });dialog.show();return dialog;
    }
    public static AlertDialog confirm(Activity activity,double lat,double lon,Action teleport) {
        if(activity.isFinishing()||activity.isDestroyed())return null;
        if(!CooldownState.valid(lat,lon))throw new IllegalArgumentException("Invalid teleport coordinates");
        if(active!=null&&active.isShowing())return active;
        Handler main=new Handler(Looper.getMainLooper());
        AlertDialog dialog=new AlertDialog.Builder(MapTheme.wrap(activity)).setTitle("Teleport cooldown")
            .setMessage(warning(lat,lon)).setNegativeButton("Cancel",null).setPositiveButton("Teleport",null).create();
        Runnable refresh=new Runnable(){public void run(){if(dialog.isShowing()){dialog.setMessage(warning(lat,lon));main.postDelayed(this,1000);}}};
        active=dialog;
        DialogLifecycle.bind(activity,dialog,()->{main.removeCallbacksAndMessages(null);if(active==dialog)active=null;});
        dialog.setOnShowListener(d->{MapTheme.styleButtons(dialog);main.post(refresh);
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v->{if(teleport.apply(lat,lon))dialog.dismiss();});
        });dialog.show();return dialog;
    }
}
