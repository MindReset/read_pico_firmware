/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：ReadPico 墨水屏的安卓推送 App（单 Activity，纯框架 API，无第三方依赖）。
 * 界面：一个输入框（启动自动读剪贴板）+「读取剪贴板」+「发送到墨水屏」按钮 +
 * 日志区 + 单词卡模式开关。协议层在 PushProto.java（与 push_proto.h 逐字段对表，
 * 宿主测试 25/25 通过）。
 *
 * 推送流程（与 push_pico.py / ReadPicoPush.bsh 一致）：
 *   扫描 PicoPush-XXXX（12s） -> 连接 -> 协商 MTU 247 -> 发现服务 ->
 *   订阅 5f13 通知 -> 写 BEGIN 到 5f12 -> 等 READY/RESUME -> DATA×N 写 5f11（WWR，
 *   逐包等 ATT 回执） -> 写 END 到 5f12 -> 等 DONE/ERR。
 *   写入方式实证修正记录（gen12）：固件 5f11/5f12 特性位一直是 F_WRITE（WWR），
 *   F_WRITE_NO_RSP 从未在固件出现；ATT 逐包回执是第一道防线，设备侧 RESUME
 *   是第二道防线。
 *   gen14 加固：MTU 协商失败（<32）明确失败重试，不闷头走 Prepare Write 长写
 *   （固件 prep 队列已加深到 16 治 rc=6，但各安卓栈长写行为参差）；RESUME 帧
 *   进 STATUS 候选（offset 0 按 READY 继续，非 0 明确提示重新完整发送）。
 *   gen14 hardening: a failed MTU exchange (<32) fails explicitly for a retry
 *   instead of silently entering a Prepare Write long write (the firmware prep
 *   queue is now 16 deep to fix rc=6, but Android long-write behaviour varies);
 *   RESUME joins the STATUS candidates (offset 0 continues as READY, non-zero
 *   prompts a full resend).
 *
 * English: minimal single-activity push client for the ReadPico e-ink device.
 * Pure framework APIs; protocol codec lives in PushProto.java.
 */
package dev.readpico.push;

import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.location.LocationManager;
import android.os.Bundle;
import android.text.method.ScrollingMovementMethod;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import android.app.AlertDialog;
import android.content.DialogInterface;

import java.nio.charset.Charset;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

public class MainActivity extends Activity {

    private static final Charset UTF8 = Charset.forName("UTF-8");
    // gen22 一键推流快捷方式：长按桌面图标 →「一键推流」→ 读剪贴板并直接推送，
    // 不建界面（透明 Activity），只弹 Toast。
    // gen22 one-tap push shortcut: long-press the launcher icon -> "一键推流" ->
    // read the clipboard and push straight away, no layout (translucent), Toast only.
    public static final String ACTION_PUSH_CLIPBOARD = "dev.readpico.push.ACTION_PUSH_CLIPBOARD";
    private static boolean sShortcutMode = false;
    private static final int REQ_PERMS = 1;
    private static final int REQ_ENABLE_BT = 2;
    private static final long SCAN_TIMEOUT_MS = 12000;
    private static final long CONNECT_TIMEOUT_MS = 20000;
    private static final long STEP_TIMEOUT_MS = 20000;
    private static final long FINAL_TIMEOUT_MS = 60000; // END 后设备要校验+落盘，放宽
    // gen15：session_id 必须每次推送都不同。设备端 CONNECTED+BEGIN 有一条
    // 「同 sid 且 has_part → 直接 RESUME」的分支；sid 恒定（原为 1）时，只要有一次
    // 推送失败留下 .part，此后**每一次**推送都会命中该分支被判成续传，而本客户端
    // 明确不支持续传（gen14 起遇到非 0 offset 就提示重发并放弃）→ 永久卡死。
    // gen14 真机铁证：首推 CRC 失败后，连续 4 次重连都只有「一次 notify 随即断开」。
    // gen15: the session id must differ per push. The device's CONNECTED+BEGIN path
    // resumes whenever the same sid arrives while a partial is on disk; with a
    // constant sid (previously 1), a single failed push poisons every later push
    // into a resume this client cannot serve (since gen14 it bails out on any
    // non-zero offset) - a permanent deadlock. gen14 hardware evidence: after the
    // first CRC failure, four reconnects each ended after a single notify.
    private static int nextSessionId() {
        return ((int) (System.nanoTime() >>> 1)) | 1; // 非零、逐次不同 / non-zero, distinct
    }

    private EditText input;
    private CheckBox wordCard;
    private TextView logView;
    private ScrollView logScroll;
    // gen23：定位服务状态横幅（红=缺权限/GPS 未开，绿=就绪）。
    // gen23: location-services status banner (red = missing, green = ready).
    private TextView locBanner;

    private BluetoothAdapter adapter;
    private BluetoothGatt gatt;

    /* ---- 同步门：worker 线程等待 BLE 回调 ---- */
    private final Object lock = new Object();
    private int connState = -1;
    private int mtuValue = 23;
    private boolean mtuDone = false;
    private boolean svcDone = false;
    private int svcStatus = -1;
    private int writeStatus = -1;
    private boolean dscDone = false;
    private byte[] lastStatus = null;
    private boolean scanDone = false;
    private volatile String foundAddr = null;
    private volatile int foundToken = -1;
    private volatile String foundName = null;
    private String failReason = null;
    private volatile int scanFailedCode = -1; // 最近一次 onScanFailed 的 code，-1 = 无

    /* ---- gen22 防串台：候选设备列表（不再「扫到第一个就连」） ---- */
    // gen22 anti-crosstalk: candidate list (no longer "connect to the first hit").
    private static final long MULTI_SCAN_GRACE_MS = 2500; // 首个设备后继续收多久 / extra window after the first hit
    private static final class Found {
        final String addr;
        final String name;
        final int token;
        Found(String addr, String name, int token) { this.addr = addr; this.name = name; this.token = token; }
    }
    private final List<Found> candidates = new ArrayList<Found>();
    private volatile long firstFoundMs = 0;

    /* ---- BLE 回调 ---- */

    private final BluetoothGattCallback gattCb = new BluetoothGattCallback() {
        @Override
        public void onConnectionStateChange(BluetoothGatt g, int status, int newState) {
            synchronized (lock) {
                if (status != BluetoothGatt.GATT_SUCCESS
                        && newState == BluetoothGatt.STATE_DISCONNECTED) {
                    failReason = "连接中断（GATT status=" + status + "）";
                }
                connState = newState;
                lock.notifyAll();
            }
        }

        @Override
        public void onMtuChanged(BluetoothGatt g, int mtu, int status) {
            synchronized (lock) {
                if (status == BluetoothGatt.GATT_SUCCESS) {
                    mtuValue = mtu;
                }
                mtuDone = true;
                lock.notifyAll();
            }
        }

        @Override
        public void onServicesDiscovered(BluetoothGatt g, int status) {
            synchronized (lock) {
                svcStatus = status;
                svcDone = true;
                lock.notifyAll();
            }
        }

        @Override
        public void onCharacteristicWrite(BluetoothGatt g, BluetoothGattCharacteristic c, int status) {
            synchronized (lock) {
                writeStatus = status;
                lock.notifyAll();
            }
        }

        @Override
        public void onDescriptorWrite(BluetoothGatt g, BluetoothGattDescriptor d, int status) {
            synchronized (lock) {
                dscDone = true;
                lock.notifyAll();
            }
        }

        @Override
        public void onCharacteristicChanged(BluetoothGatt g, BluetoothGattCharacteristic c) {
            if (!PushProto.STATUS_UUID.equalsIgnoreCase(c.getUuid().toString())) {
                return;
            }
            synchronized (lock) {
                byte[] v = c.getValue();
                lastStatus = (v == null) ? null : v.clone();
                lock.notifyAll();
            }
        }
    };

    /* ---- 扫描回调：按广播名前缀 PicoPush- 过滤 ---- */

    private final ScanCallback scanCb = new ScanCallback() {
        @Override
        public void onScanResult(int callbackType, ScanResult result) {
            String name = null;
            if (result.getScanRecord() != null) {
                name = result.getScanRecord().getDeviceName();
            }
            if (name == null && result.getDevice() != null) {
                name = result.getDevice().getName();
            }
            Integer token = PushProto.tokenFromAdvName(name);
            if (token != null && result.getDevice() != null) {
                // gen22 防串台：不再「第一个命中就收工」。全部收集起来（按地址去重），
                // 由 scanForDevice 在宽限期后统一挑选——多台并存时交给用户点选，
                // 避免把私密长文推到别人的墨水屏上。
                // gen22 anti-crosstalk: no longer "first hit wins". Collect every match
                // (de-duplicated by address); scanForDevice picks after a grace window -
                // with several panels around, the user chooses, so a private long text
                // never lands on someone else's screen.
                synchronized (lock) {
                    String addr = result.getDevice().getAddress();
                    boolean dup = false;
                    for (int i = 0; i < candidates.size(); i++) {
                        if (candidates.get(i).addr.equals(addr)) { dup = true; break; }
                    }
                    if (!dup) {
                        candidates.add(new Found(addr, name, token.intValue()));
                        if (firstFoundMs == 0) firstFoundMs = System.currentTimeMillis();
                        lock.notifyAll();
                    }
                }
            }
        }

        @Override
        public void onScanFailed(int errorCode) {
            synchronized (lock) {
                scanFailedCode = errorCode;
                // SCAN_FAILED_ALREADY_STARTED(1) 可恢复：上一轮扫描还没停净。
                // 唤醒等待循环（不置 failReason），由 scanForDevice 的重试逻辑
                // 停一次再起——否则会白等满 12 秒。
                // SCAN_FAILED_ALREADY_STARTED (1) is recoverable: the previous
                // scan has not fully stopped. Wake the waiter (without setting
                // failReason) so the retry loop in scanForDevice can stop and
                // restart - otherwise we would idle out the full 12 seconds.
                if (errorCode == 1) {
                    if (candidates.isEmpty()) {
                        scanDone = true;
                        lock.notifyAll();
                    }
                    return;
                }
                if (candidates.isEmpty() && failReason == null) {
                    failReason = "扫描启动失败（code=" + errorCode + "）";
                    scanDone = true;
                    lock.notifyAll();
                }
            }
        }
    };

    /* ---- Activity 生命周期 ---- */

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // setTheme 必须在 super.onCreate 之前：快捷方式用透明无界面主题，
        // 用户看不到任何窗口，只有 Toast。/ setTheme must precede super.onCreate:
        // the shortcut uses a translucent no-display theme so only a Toast shows.
        sShortcutMode = ACTION_PUSH_CLIPBOARD.equals(getIntent().getAction());
        if (sShortcutMode) {
            // 对话框主题：快捷方式必须弹出一个**可见的小窗口**才行。
            // 真机实测（荣耀 / Android 16）：无界面的透明 Activity 会被系统判为后台，
            // 读剪贴板被隐私权限拦截（弹「允许读取剪贴板」后选「仅使用期间允许」仍无效）。
            // 所以改为可见对话框 + 用户在窗口里点「发送」——有前台交互，系统放行。
            // Dialog theme: the shortcut MUST show a visible small window. On real
            // hardware (Honor / Android 16) a faceless translucent activity is treated
            // as background and clipboard reads get blocked by the privacy guard (even
            // after granting "only while in use"). A visible dialog plus an explicit tap
            // on 发送 is foreground interaction, which the system allows.
            setTheme(android.R.style.Theme_DeviceDefault_Dialog_MinWidth);
        }
        super.onCreate(savedInstanceState);
        if (sShortcutMode) {
            runShortcutPush();
            return;
        }

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(16);
        root.setPadding(pad, pad, pad, pad);

        TextView head = new TextView(this);
        head.setText("ReadPico 推送");
        head.setTextSize(20);
        head.setPadding(0, 0, 0, dp(8));
        root.addView(head, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));

        // gen23：定位服务检测横幅，紧跟标题之下。/ gen23: location banner right under the title.
        locBanner = new TextView(this);
        locBanner.setTextSize(13);
        locBanner.setPadding(dp(10), dp(8), dp(10), dp(8));
        locBanner.setVisibility(View.GONE); // onCreate 末尾按状态刷新显示
        root.addView(locBanner, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));

        input = new EditText(this);
        input.setHint("在此粘贴或输入要推送到墨水屏的文字……");
        input.setGravity(Gravity.TOP | Gravity.START);
        input.setTextSize(16);
        input.setBackgroundColor(0xFFF3F4F6);
        input.setPadding(dp(10), dp(10), dp(10), dp(10));
        root.addView(input, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));

        wordCard = new CheckBox(this);
        wordCard.setText("单词卡模式（正文忽略，title 即单词）");
        wordCard.setPadding(0, dp(6), 0, dp(6));
        root.addView(wordCard, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);

        Button btnClip = new Button(this);
        btnClip.setText("读取剪贴板");
        btnClip.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                loadClipboard();
                uiLog("已从剪贴板读取（" + input.getText().length() + " 字符）");
            }
        });
        row.addView(btnClip, new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));

        Button btnSend = new Button(this);
        btnSend.setText("发送到墨水屏");
        btnSend.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                onSendClicked();
            }
        });
        row.addView(btnSend, new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        root.addView(row, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));

        logScroll = new ScrollView(this);
        logView = new TextView(this);
        logView.setTextSize(12);
        logView.setPadding(0, dp(8), 0, 0);
        logView.setMovementMethod(new ScrollingMovementMethod());
        logScroll.addView(logView, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));
        root.addView(logScroll, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));

        setContentView(root);

        BluetoothManager bm = (BluetoothManager) getSystemService(Context.BLUETOOTH_SERVICE);
        adapter = (bm == null) ? null : bm.getAdapter();
        if (adapter == null) {
            uiLog("本机没有蓝牙硬件");
        }

        loadClipboard();
        uiLog("就绪。在墨水屏上开启「接收推送」后，点「发送到墨水屏」。");
        refreshLocationBanner(); // gen23：进入 App 检测一次定位服务/权限
    }

    @Override
    protected void onResume() {
        super.onResume();
        // gen23：从系统设置（开 GPS / 授权限）返回时刷新一次横幅；事件触发，非轮询。
        // gen23: refresh the banner when returning from system settings; event-driven, not polling.
        if (!sShortcutMode) {
            refreshLocationBanner();
        }
    }

    @Override
    protected void onDestroy() {
        closeQuietly();
        super.onDestroy();
    }

    /* ---- gen22 一键推流（桌面快捷方式） ---- */

    // 长按桌面图标点「一键推流」后走这里：无界面，读剪贴板 → 扫描连接 → 推送 → Toast 收尾。
    // Android 10+ 禁止后台读剪贴板，所以必须由快捷方式拉起这个（透明的）Activity——
    // 它处于前台，读剪贴板才合法；纯后台 Service 拿不到剪贴板内容。
    // Entry for the launcher shortcut: no layout; read clipboard -> scan/connect ->
    // push -> finish with a Toast. Android 10+ forbids background clipboard access,
    // so the shortcut must raise this (translucent) Activity - it is foreground, which
    // is what makes reading the clipboard legal.
    private void runShortcutPush() {
        // 只检查、不弹系统授权框：NoDisplay 的 Activity 上权限弹窗显示不出来，
        // 弹了就会卡在这里。缺权限就提示用户先正常打开一次 App。
        // Check only, never raise the system permission dialog: it cannot render on a
        // NoDisplay activity and would hang here. Missing permissions -> ask the user
        // to open the app normally once.
        if (!hasBluetoothPermissions()) {
            toast("请先打开 PicoPush 一次并授予蓝牙权限");
            finish();
            return;
        }
        BluetoothManager bm = (BluetoothManager) getSystemService(Context.BLUETOOTH_SERVICE);
        adapter = (bm == null) ? null : bm.getAdapter();
        if (adapter == null) {
            toast("本机没有蓝牙硬件");
            finish();
            return;
        }
        if (!adapter.isEnabled()) {
            toast("请先开启手机蓝牙");
            finish();
            return;
        }
        // gen23：老系统位置服务未开 → 扫描必然空结果，提前提示（与主界面同因）。
        // gen23: legacy systems with location services off scan into the void - warn early.
        if (Build.VERSION.SDK_INT < 31 && !isLocationServiceOn()) {
            toast("请先打开手机定位服务（GPS）才能扫描墨水屏");
            finish();
            return;
        }
        showShortcutDialog();
    }

    // 极简确认弹窗：显示读到的字数预览，用户点「发送」才真正推送。
    // **关键**：剪贴板在用户点「发送」的那一刻再读一次——那是明确的用户手势，
    // 系统一定放行（弹窗打开时先读一次只是为了给预览，读不到也不阻塞）。
    // Minimal confirm dialog: show a preview and push only after the user taps 发送.
    // Key point: the clipboard is read AGAIN inside the tap handler - an explicit user
    // gesture the system always allows. The read at dialog-open time is only for the
    // preview and failing it must not block.
    private void showShortcutDialog() {
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(20);
        box.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText("一键推流");
        title.setTextSize(18);
        title.setPadding(0, 0, 0, dp(10));
        box.addView(title, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));

        TextView info = new TextView(this);
        info.setTextSize(14);
        info.setPadding(0, 0, 0, dp(14));
        String preview = readClipboardText();
        if (preview == null || preview.trim().isEmpty()) {
            info.setText("剪贴板暂时读不到内容。\n点「发送」时会在你的点击下重新读取。");
        } else {
            String head = preview.length() > 40 ? preview.substring(0, 40) + "…" : preview;
            info.setText("已读取剪贴板（" + preview.length() + " 个字符）\n\n" + head);
        }
        box.addView(info, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));

        Button send = new Button(this);
        send.setText("发送");
        send.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                String text = readClipboardText();   // 用户手势内读取 / read inside the gesture
                if (text == null || text.trim().isEmpty()) {
                    toast("剪贴板是空的，先复制要推送的文字");
                    return;
                }
                final String body = text;
                toast("正在推送到墨水屏……");
                new Thread(new Runnable() {
                    @Override
                    public void run() {
                        try {
                            doPush(body, false);
                            toast("推送完成");
                        } catch (Throwable t) {
                            toast("推送失败：" + t.getMessage());
                        }
                        runOnUiThread(new Runnable() {
                            @Override
                            public void run() {
                                finish();   // finish 必须回 UI 线程 / finish on the UI thread
                            }
                        });
                    }
                }).start();
            }
        });
        box.addView(send, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));

        Button cancel = new Button(this);
        cancel.setText("取消");
        cancel.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                finish();
            }
        });
        box.addView(cancel, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));

        setContentView(box);
    }

    // 直接读剪贴板文本（不写入 input 控件，快捷模式没有界面）。
    // Read the clipboard directly (no EditText to fill in shortcut mode).
    private String readClipboardText() {
        try {
            ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
            if (cm == null || !cm.hasPrimaryClip()) return null;
            ClipData data = cm.getPrimaryClip();
            if (data == null || data.getItemCount() == 0) return null;
            CharSequence cs = data.getItemAt(0).getText();
            return cs == null ? null : cs.toString();
        } catch (Throwable t) {
            return null;
        }
    }

    /* ---- 发送入口 ---- */

    private void onSendClicked() {
        final String text = (input.getText() == null) ? "" : input.getText().toString();
        final boolean isWord = wordCard.isChecked();
        if (text.trim().isEmpty()) {
            toast(isWord ? "单词卡需要输入单词" : "正文为空");
            return;
        }
        if (adapter == null) {
            toast("本机没有蓝牙硬件");
            return;
        }
        if (!adapter.isEnabled()) {
            startActivityForResult(new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE), REQ_ENABLE_BT);
            uiLog("请先打开蓝牙");
            return;
        }
        if (!ensurePermissions()) {
            return;
        }
        refreshLocationBanner(); // gen23：发送前再检测一次
        // gen23：老系统（8~11）上位置服务总开关不开，BLE 扫描会静默返回空——
        // 三星 Android 10 真机实证「一直未发现设备」。这里提前拦一下并说明原因。
        // gen23: on legacy systems (8-11) a scan silently returns nothing while the
        // location master switch is off (proven on Samsung Android 10: endless
        // "no device found"). Intercept early with an explanation.
        if (Build.VERSION.SDK_INT < 31 && !isLocationServiceOn()) {
            uiLog("位置服务（GPS）未开启：系统会禁止蓝牙扫描返回结果");
            toast("请先打开手机定位服务（GPS）");
            return;
        }
        new Thread(new Runnable() {
            @Override
            public void run() {
                doPush(text, isWord);
            }
        }).start();
    }

    /* ---- 推送主流程（worker 线程） ---- */

    private void doPush(String text, boolean wordCard) {
        try {
            resetState();
            byte[] body = wordCard ? new byte[0] : text.getBytes(UTF8);
            if (body.length > PushProto.TOTAL_MAX) {
                uiLog("正文 " + body.length + " 字节超过 512KB 上限");
                return;
            }
            String title = wordCard ? text.trim() : firstLine(text);
            int ptype = wordCard ? PushProto.TYPE_WORD : PushProto.TYPE_TEXT;

            if (!scanForDevice()) {
                uiLog(failReason == null ? "未发现设备" : failReason);
                return;
            }
            uiLog("发现 " + foundName + "（token=0x"
                    + String.format("%04X", foundToken & 0xFFFF) + "）");

            BluetoothDevice dev = adapter.getRemoteDevice(foundAddr);
            uiLog("连接中……");
            gatt = dev.connectGatt(this, false, gattCb, BluetoothDevice.TRANSPORT_LE);
            if (gatt == null) {
                uiLog("connectGatt 返回 null");
                return;
            }
            if (!waitFlag(STATE_CONNECTED_WAIT, CONNECT_TIMEOUT_MS)) {
                return;
            }
            uiLog("已连接，协商 MTU……");
            gatt.requestMtu(247);
            waitFlag(MTU_WAIT, 8000); // 失败不致命：退回默认 23，分片变小
            int chunk = PushProto.chunkForMtu(mtuValue);
            uiLog("MTU=" + mtuValue + "，每包 " + chunk + " 字节");
            // gen14：MTU 协商失败（有效 23）时 83 字节 BEGIN 要走 Prepare Write 长写。
            // 固件侧 prep 队列已加深到 16（gen14 修 rc=6），但各安卓栈对长写的处理
            // 参差（gen13 真机：长写第 5 包设备侧 ENOMEM，客户端表现还是白等），明确
            // 失败让用户重试比闷头走长写可靠。
            // gen14: with the MTU exchange failed (effective 23) the 83 B BEGIN
            // needs a Prepare Write long write. The firmware prep queue is now 16
            // deep (gen14 fixes rc=6), but Android stacks differ in long-write
            // handling (gen13 on hardware: the 5th packet hit device-side ENOMEM
            // while the client just sat there) - an explicit failure with a retry
            // hint beats a silent long write.
            if (mtuValue < 32) {
                uiLog("MTU 协商失败（" + mtuValue + "），本次无法发送，请重试");
                return;
            }

            uiLog("发现 GATT 服务……");
            gatt.discoverServices();
            if (!waitFlag(SVC_WAIT, 10000)) {
                return;
            }
            if (svcStatus != BluetoothGatt.GATT_SUCCESS) {
                uiLog("服务发现失败（status=" + svcStatus + "）");
                return;
            }
            BluetoothGattService svc = gatt.getService(UUID.fromString(PushProto.SERVICE_UUID));
            if (svc == null) {
                uiLog("设备上没有推送服务（" + PushProto.SERVICE_UUID + "）");
                return;
            }
            BluetoothGattCharacteristic cData = svc.getCharacteristic(UUID.fromString(PushProto.DATA_UUID));
            BluetoothGattCharacteristic cCtrl = svc.getCharacteristic(UUID.fromString(PushProto.CTRL_UUID));
            BluetoothGattCharacteristic cStatus = svc.getCharacteristic(UUID.fromString(PushProto.STATUS_UUID));
            if (cData == null || cCtrl == null || cStatus == null) {
                uiLog("推送特征缺失（5f11/5f12/5f13）");
                return;
            }

            uiLog("订阅设备通知（5f13）……");
            gatt.setCharacteristicNotification(cStatus, true);
            BluetoothGattDescriptor cccd = cStatus.getDescriptor(UUID.fromString(PushProto.CCCD_UUID));
            if (cccd == null) {
                uiLog("找不到 CCCD 描述符");
                return;
            }
            cccd.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
            if (!gatt.writeDescriptor(cccd) || !waitFlag(DSC_WAIT, 10000)) {
                return;
            }

            // gen22：先把真值时间带给设备——PMU 没有外部晶振、深睡靠内部 RC 走时，
            // 一天能快几分钟。独立操作码，设备未武装也能收；失败不阻断推送。
            // gen22: hand the device the true time first - the PMU has no crystal and
            // its internal RC gains minutes a day in sleep. Separate opcode, accepted
            // even when unarmed; a failure here must not block the push.
            byte[] setTime = PushProto.encodeSetTime(System.currentTimeMillis() / 1000L);
            uiLog("校准设备时钟……");
            writeChar(cCtrl, setTime);

            final int pushSid = nextSessionId();
            byte[] begin = PushProto.encodeBegin(body.length, PushProto.crc32(body),
                    pushSid, foundToken, ptype, title);
            uiLog("发送 BEGIN（" + begin.length + " 字节，标题「" + title + "」）……");
            if (!writeChar(cCtrl, begin)) {
                return;
            }
            // gen14：RESUME 进候选。设备有半成品簿记（上一轮断开前收过包）时会先答
            // RESUME 而不是 READY；gen13 只等 READY/ERR，RESUME 被当非目标帧丢弃，
            // 白等 20 秒超时。/ gen14: RESUME joins the candidates. A device with
            // partial bookkeeping answers RESUME before READY; gen13 waited for
            // READY/ERR only, dropped RESUME as a non-target frame and idled out
            // the full 20 s timeout.
            byte[] st = waitStatus(new int[] { PushProto.ST_READY, PushProto.ST_RESUME,
                    PushProto.ST_ERR }, STEP_TIMEOUT_MS);
            if (st == null) {
                return;
            }
            if (PushProto.statusOp(st) == PushProto.ST_ERR) {
                uiLog("设备拒绝：" + PushProto.errText(PushProto.statusBody(st)[0] & 0xFF));
                return;
            }
            if (PushProto.statusOp(st) == PushProto.ST_RESUME) {
                // RESUME body：offset(u32 LE) + next_seq(u16 LE)。offset 0 = 设备没有
                // 可续的字节，等价于 READY（同 sid 重发 BEGIN 的续传路径）；非 0 = 要求
                // 从中间续传，本版本客户端不做断点续发，明确告知重新完整发送。
                // RESUME body: offset(u32 LE) + next_seq(u16 LE). offset 0 means
                // nothing to resume (same as READY); non-zero asks for a mid-stream
                // resume this client version does not implement - say so.
                final long off = PushProto.resumeOffset(st);
                final long nseq = PushProto.resumeNextSeq(st);
                if (off != 0 || nseq != 0) {
                    uiLog("设备要求从 " + off + " 字节续传（seq " + nseq + "），"
                            + "请重新完整发送");
                    return;
                }
                uiLog("设备有旧会话簿记，按重发处理……");
            }
            final long sid = PushProto.statusOp(st) == PushProto.ST_READY
                    ? PushProto.readySessionId(st) : -1L;
            uiLog("设备就绪（session=" + (sid < 0 ? "?" : Long.toHexString(sid))
                    + "），正文 " + body.length + " 字节，开始传输……");

            int seq = 0;
            if (body.length == 0) {
                // 空正文（单词卡/URL）：仍发一包 seq=0 长度 0，对齐有正文路径
                if (!writeChar(cData, PushProto.encodeData(0, new byte[0]))) {
                    return;
                }
            } else {
                int off = 0;
                while (off < body.length) {
                    int n = Math.min(chunk, body.length - off);
                    byte[] piece = new byte[n];
                    System.arraycopy(body, off, piece, 0, n);
                    if (!writeChar(cData, PushProto.encodeData(seq, piece))) {
                        return;
                    }
                    off += n;
                    seq++;
                    if ((seq & 7) == 0) {
                        uiLog("已传 " + off + " / " + body.length + " 字节");
                    }
                }
            }

            uiLog("数据发送完毕（" + seq + " 包），发送 END 等待设备校验……");
            if (!writeChar(cCtrl, PushProto.encodeCtrl(PushProto.OP_END))) {
                return;
            }
            st = waitStatus(new int[] { PushProto.ST_DONE, PushProto.ST_ERR }, FINAL_TIMEOUT_MS);
            if (st == null) {
                return;
            }
            if (PushProto.statusOp(st) == PushProto.ST_ERR) {
                uiLog("设备拒绝：" + PushProto.errText(PushProto.statusBody(st)[0] & 0xFF));
                return;
            }
            uiLog("推送完成：设备已校验 CRC32 并开书 ✓");
            toast("推送完成 ✓");
        } finally {
            closeQuietly();
        }
    }

    /* ---- 等待原语（worker 线程） ---- */

    private static final int STATE_CONNECTED_WAIT = 1;
    private static final int MTU_WAIT = 2;
    private static final int SVC_WAIT = 3;
    private static final int DSC_WAIT = 4;
    private static final int WRITE_WAIT = 5;

    /** 等一个回调条件；超时或 failReason 时记录原因并返回 false。 */
    private boolean waitFlag(int what, long timeoutMs) {
        long deadline = System.currentTimeMillis() + timeoutMs;
        synchronized (lock) {
            while (true) {
                if (failReason != null) {
                    uiLog(failReason);
                    return false;
                }
                if (what == STATE_CONNECTED_WAIT && connState == BluetoothGatt.STATE_CONNECTED) {
                    return true;
                }
                if (what == MTU_WAIT && mtuDone) {
                    return true;
                }
                if (what == SVC_WAIT && svcDone) {
                    return true;
                }
                if (what == DSC_WAIT && dscDone) {
                    return true;
                }
                if (what == WRITE_WAIT && writeStatus != -1) {
                    return true;
                }
                long rem = deadline - System.currentTimeMillis();
                if (rem <= 0) {
                    uiLog("等待设备回应超时（" + whatName(what) + "）");
                    return false;
                }
                try {
                    lock.wait(Math.min(rem, 250));
                } catch (InterruptedException e) {
                    return false;
                }
            }
        }
    }

    private static String whatName(int what) {
        switch (what) {
            case STATE_CONNECTED_WAIT: return "连接";
            case MTU_WAIT: return "MTU 协商";
            case SVC_WAIT: return "服务发现";
            case DSC_WAIT: return "描述符写入";
            case WRITE_WAIT: return "特征写入";
            default: return "步骤 " + what;
        }
    }

    /** 等一条 opcode 属于 wanted 的 STATUS 通知；其他 opcode（PROGRESS/BATT 等）丢弃继续等。 */
    private byte[] waitStatus(int[] wanted, long timeoutMs) {
        long deadline = System.currentTimeMillis() + timeoutMs;
        synchronized (lock) {
            while (true) {
                if (failReason != null) {
                    uiLog(failReason);
                    return null;
                }
                byte[] f = lastStatus;
                if (f != null) {
                    lastStatus = null;
                    int op = PushProto.statusOp(f);
                    for (int i = 0; i < wanted.length; i++) {
                        if (op == wanted[i]) {
                            return f;
                        }
                    }
                    continue; // 非目标帧：丢弃，继续等
                }
                long rem = deadline - System.currentTimeMillis();
                if (rem <= 0) {
                    uiLog("等待设备 STATUS 超时");
                    return null;
                }
                try {
                    lock.wait(Math.min(rem, 250));
                } catch (InterruptedException e) {
                    return null;
                }
            }
        }
    }

    /** WWR 写（WRITE_TYPE_DEFAULT）：逐包等 ATT 回执。 */
    private boolean writeChar(BluetoothGattCharacteristic c, byte[] value) {
        synchronized (lock) {
            writeStatus = -1;
        }
        c.setValue(value);
        c.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);
        if (!gatt.writeCharacteristic(c)) {
            uiLog("写入调用失败");
            return false;
        }
        if (!waitFlag(WRITE_WAIT, STEP_TIMEOUT_MS)) {
            return false;
        }
        int ws;
        synchronized (lock) {
            ws = writeStatus;
            writeStatus = -1;
        }
        if (ws != BluetoothGatt.GATT_SUCCESS) {
            uiLog("设备未接受写入（GATT status=" + ws + "）");
            return false;
        }
        return true;
    }

    /* ---- 扫描 ---- */

    // gen22 防串台：只有一台才自动连；多台时必须让用户按墨水屏上显示的代号点选。
    // 名字本身就是 MAC 后四位（PicoPush-1A2B），屏幕与列表一一对应，不会认错。
    // gen22 anti-crosstalk: auto-connect only when there is exactly one panel; with
    // several, the user must pick by the code shown on the e-ink screen. The name is
    // the MAC's last four hex digits (PicoPush-1A2B), so screen and list match exactly.
    private boolean chooseDevice(final List<Found> list) {
        if (list.isEmpty()) return false;
        if (list.size() == 1) {
            Found only = list.get(0);
            synchronized (lock) {
                foundAddr = only.addr;
                foundToken = only.token;
                foundName = only.name;
            }
            return true;
        }
        final String[] labels = new String[list.size()];
        for (int i = 0; i < list.size(); i++) {
            Found f = list.get(i);
            labels[i] = f.name + "   [" + f.addr + "]";
        }
        final int[] pickedIndex = new int[] { -1 };
        final CountDownLatch picked = new CountDownLatch(1);
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                new AlertDialog.Builder(MainActivity.this)
                        .setTitle("选择要推送的墨水屏（代号见设备屏幕）")
                        .setItems(labels, new DialogInterface.OnClickListener() {
                            @Override
                            public void onClick(DialogInterface dialog, int which) {
                                pickedIndex[0] = which;
                                picked.countDown();
                            }
                        })
                        .setOnCancelListener(new DialogInterface.OnCancelListener() {
                            @Override
                            public void onCancel(DialogInterface dialog) {
                                picked.countDown();
                            }
                        })
                        .show();
            }
        });
        try {
            picked.await(90, TimeUnit.SECONDS);
        } catch (InterruptedException e) {
            /* 用户不等了 / give up waiting */
        }
        int index = pickedIndex[0];
        if (index < 0 || index >= list.size()) {
            failReason = "未选择设备（发现 " + list.size() + " 台，请选一台）";
            return false;
        }
        Found chosen = list.get(index);
        synchronized (lock) {
            foundAddr = chosen.addr;
            foundToken = chosen.token;
            foundName = chosen.name;
        }
        return true;
    }

    private boolean scanForDevice() {
        BluetoothLeScanner scanner = adapter.getBluetoothLeScanner();
        if (scanner == null) {
            failReason = "拿不到 BLE 扫描器（蓝牙未开？）";
            return false;
        }
        ScanSettings settings = new ScanSettings.Builder()
                .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build();
        // 上一轮扫描停净是异步的：紧接着再 startScan 会收
        // SCAN_FAILED_ALREADY_STARTED(1)。真机日志见过连续两发 code=1——
        // 不是权限问题，是没停净。最多重试 3 轮，每轮先停再等 600ms。
        // The previous scan stops asynchronously: an immediate restart collects
        // SCAN_FAILED_ALREADY_STARTED (1). Real logs showed two code=1s back to
        // back - not a permission problem, just an unstopped scan. Up to three
        // attempts, each stopping first and waiting 600 ms.
        for (int attempt = 1; attempt <= 3; attempt++) {
            uiLog(attempt == 1
                    ? "扫描 PicoPush-XXXX 广播（最多 12 秒）……"
                    : "重试扫描（第 " + attempt + " 次，先停上一轮）……");
            synchronized (lock) {
                scanDone = false;
                foundAddr = null;
                failReason = null;
                scanFailedCode = -1;
                candidates.clear();      // gen22：每轮重新收集 / recollect each round
                firstFoundMs = 0;
            }
            scanner.stopScan(scanCb); // 幂等：没在扫也是安全调用 / idempotent
            sleepQuiet(200);
            scanner.startScan(null, settings, scanCb);
            long deadline = System.currentTimeMillis() + SCAN_TIMEOUT_MS;
            synchronized (lock) {
                while (!scanDone && failReason == null) {
                    // gen22：扫到第一台之后再多收 2.5 秒，看旁边还有没有别的墨水屏；
                    // 只有一台时几乎立刻进入连接（保持原有手感）。
                    // gen22: after the first hit keep listening 2.5 s for neighbours;
                    // with a single panel we still proceed almost immediately.
                    long effective = deadline;
                    if (firstFoundMs > 0) {
                        effective = Math.min(deadline, firstFoundMs + MULTI_SCAN_GRACE_MS);
                    }
                    long rem = effective - System.currentTimeMillis();
                    if (rem <= 0) {
                        break;
                    }
                    try {
                        lock.wait(Math.min(rem, 250));
                    } catch (InterruptedException e) {
                        break;
                    }
                }
            }
            scanner.stopScan(scanCb);
            List<Found> snapshot;
            synchronized (lock) {
                snapshot = new ArrayList<Found>(candidates);
            }
            if (!snapshot.isEmpty()) {
                return chooseDevice(snapshot);
            }
            if (failReason != null) {
                return false; // 硬失败（非 code=1），onScanFailed 已写明原因
            }
            if (scanFailedCode == 1 && attempt < 3) {
                continue; // 可恢复：停一次等一会儿再来 / recoverable: stop, wait, retry
            }
            if (scanFailedCode == 1) {
                failReason = "扫描反复启动失败（code=1），请退出应用重进";
                return false;
            }
            failReason = "12 秒内未发现 PicoPush-XXXX 广播（请先在墨水屏开启「接收推送」）";
            return false;
        }
        failReason = "扫描反复启动失败，请退出应用重进";
        return false;
    }

    private static void sleepQuiet(long ms) {
        try {
            Thread.sleep(ms);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }

    /* ---- 权限 ---- */

    // gen23：位置服务总开关。/ gen23: location services master switch.
    // Android 8~11 的 BLE 扫描不仅需要 ACCESS_FINE_LOCATION，还需要位置服务开着，
    // 否则 onScanResult 一个都不回调（三星 Android 10 真机实证）。12+ 起
    // BLUETOOTH_SCAN 带 neverForLocation，不再要求位置服务。
    // On Android 8-11 a BLE scan needs ACCESS_FINE_LOCATION AND location services
    // enabled, otherwise onScanResult never fires (proven on Samsung Android 10).
    // On 12+ BLUETOOTH_SCAN carries neverForLocation and location is not required.
    private boolean isLocationServiceOn() {
        try {
            LocationManager lm = (LocationManager) getSystemService(Context.LOCATION_SERVICE);
            if (lm == null) {
                return false;
            }
            if (Build.VERSION.SDK_INT >= 28) {
                return lm.isLocationEnabled();
            }
            return lm.isProviderEnabled(LocationManager.GPS_PROVIDER)
                    || lm.isProviderEnabled(LocationManager.NETWORK_PROVIDER);
        } catch (Throwable t) {
            return false;
        }
    }

    // gen23：刷新顶部横幅。红 = 蓝牙/权限/位置服务有缺失（附具体缺项）；
    // 绿 = 定位服务已就绪。事件触发（进入 App / onResume / 发送前），不轮询。
    // gen23: refresh the top banner. Red = something is missing (with details);
    // green = ready. Event-driven (app entry / onResume / before send), never polled.
    private void refreshLocationBanner() {
        if (locBanner == null) {
            return;
        }
        boolean btOk = adapter != null && adapter.isEnabled();
        boolean permOk;
        boolean svcOk;
        if (Build.VERSION.SDK_INT >= 31) {
            permOk = hasBluetoothPermissions();
            svcOk = true; // neverForLocation：12+ 不要求位置服务
        } else {
            permOk = checkSelfPermission("android.permission.ACCESS_FINE_LOCATION")
                    == PackageManager.PERMISSION_GRANTED;
            svcOk = isLocationServiceOn();
        }
        if (btOk && permOk && svcOk) {
            locBanner.setText("✅ 定位服务已就绪");
            locBanner.setTextColor(0xFF166B3A);
            locBanner.setBackgroundColor(0xFFE8F5EC);
        } else {
            StringBuilder why = new StringBuilder();
            if (!btOk) {
                why.append("蓝牙未开 ");
            }
            if (!permOk) {
                why.append("定位权限未授予 ");
            }
            if (!svcOk) {
                why.append("位置服务(GPS)未开 ");
            }
            locBanner.setText("⚠️ 提示：安卓系统需要开启手机定位服务（GPS）及授予定位权限，"
                    + "才能正常扫描墨水屏。（当前：" + why.toString().trim() + "）");
            locBanner.setTextColor(0xFF9A1B1B);
            locBanner.setBackgroundColor(0xFFFDE8E8);
        }
        locBanner.setVisibility(View.VISIBLE);
    }

    private boolean ensurePermissions() {
        List<String> need = new ArrayList<String>();
        if (Build.VERSION.SDK_INT >= 31) {
            addIfMissing(need, "android.permission.BLUETOOTH_SCAN");
            addIfMissing(need, "android.permission.BLUETOOTH_CONNECT");
        } else {
            addIfMissing(need, "android.permission.ACCESS_FINE_LOCATION");
        }
        if (need.isEmpty()) {
            return true;
        }
        requestPermissions(need.toArray(new String[need.size()]), REQ_PERMS);
        uiLog("请先在系统弹窗中授予蓝牙权限，然后再点一次发送");
        return false;
    }

    // 快捷方式用：只判断权限是否已有，不触发请求。/ Shortcut path: report only, never request.
    private boolean hasBluetoothPermissions() {
        if (Build.VERSION.SDK_INT >= 31) {
            return checkSelfPermission("android.permission.BLUETOOTH_SCAN") == PackageManager.PERMISSION_GRANTED
                    && checkSelfPermission("android.permission.BLUETOOTH_CONNECT") == PackageManager.PERMISSION_GRANTED;
        }
        return checkSelfPermission("android.permission.ACCESS_FINE_LOCATION") == PackageManager.PERMISSION_GRANTED;
    }

    private void addIfMissing(List<String> need, String perm) {
        if (checkSelfPermission(perm) != PackageManager.PERMISSION_GRANTED) {
            need.add(perm);
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        if (requestCode != REQ_PERMS) {
            return;
        }
        boolean all = grantResults.length > 0;
        for (int i = 0; i < grantResults.length; i++) {
            if (grantResults[i] != PackageManager.PERMISSION_GRANTED) {
                all = false;
            }
        }
        uiLog(all ? "权限已授予，请再次点击发送" : "权限被拒绝，无法推送（可在系统设置里手动授予）");
    }

    /* ---- 小工具 ---- */

    private void resetState() {
        synchronized (lock) {
            connState = -1;
            mtuValue = 23;
            mtuDone = false;
            svcDone = false;
            svcStatus = -1;
            writeStatus = -1;
            dscDone = false;
            lastStatus = null;
            scanDone = false;
            failReason = null;
        }
        foundAddr = null;
        foundToken = -1;
        foundName = null;
    }

    private void closeQuietly() {
        BluetoothGatt g = gatt;
        gatt = null;
        if (g != null) {
            try {
                g.disconnect();
            } catch (Exception ignored) {
            }
            try {
                g.close();
            } catch (Exception ignored) {
            }
        }
    }

    private void loadClipboard() {
        try {
            ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
            if (cm != null && cm.hasPrimaryClip()
                    && cm.getPrimaryClip().getItemCount() > 0) {
                CharSequence cs = cm.getPrimaryClip().getItemAt(0).coerceToText(this);
                if (cs != null && cs.length() > 0) {
                    input.setText(cs);
                    input.setSelection(cs.length());
                }
            }
        } catch (Exception ignored) {
        }
    }

    /** 标题：取首行并截到 40 字符（协议层另有 63 字节硬截断）。 */
    private static String firstLine(String s) {
        if (s == null) {
            return "";
        }
        String t = s.trim();
        int nl = t.indexOf('\n');
        if (nl >= 0) {
            t = t.substring(0, nl).trim();
        }
        int cr = t.indexOf('\r');
        if (cr >= 0) {
            t = t.substring(0, cr).trim();
        }
        if (t.length() > 40) {
            t = t.substring(0, 40);
        }
        return t;
    }

    private int dp(int v) {
        return (int) (v * getResources().getDisplayMetrics().density + 0.5f);
    }

    private void uiLog(final String s) {
        // gen22 快捷方式（shortcut）模式下不建界面，logView/logScroll 为 null：
        // 日志只进 logcat，不能碰 UI，否则一键推流会空指针崩掉。
        // gen22: in shortcut mode there is no layout, so logView/logScroll are null -
        // send the line to logcat only; touching the UI would crash one-tap push.
        if (sShortcutMode) {
            android.util.Log.d("PicoPush", s);
            return;
        }
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (logView == null) {
                    android.util.Log.d("PicoPush", s);
                    return;
                }
                logView.append(s + "\n");
                if (logScroll != null) {
                    logScroll.post(new Runnable() {
                        @Override
                        public void run() {
                            logScroll.fullScroll(View.FOCUS_DOWN);
                        }
                    });
                }
            }
        });
    }

    private void toast(final String s) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                Toast.makeText(MainActivity.this, s, Toast.LENGTH_LONG).show();
            }
        });
    }
}
