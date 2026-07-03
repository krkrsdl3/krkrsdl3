package org.tvp.krkrsdl3;

import android.Manifest;
import android.annotation.SuppressLint;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.text.Editable;
import android.text.TextWatcher;
import android.util.Log;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.LayoutInflater;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.BaseAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.GridView;
import android.widget.ImageButton;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.PopupWindow;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.InputStream;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Objects;

/**
 * 游戏启动器主界面
 * 功能：游戏库管理、历史记录、单人配置、封面管理
 */
public class MainActivity extends AppCompatActivity {

    private static final int REQUEST_DIR_PICKER = 10;
    private static final int REQUEST_COVER_IMAGE = 20;
    private static final int REQUEST_STARTUP_FILE = 30;
    private static final String COLOR_NOTDIR = "#FF0000";
    private static final String COLOR_INRDIR = "#4CAF50";
    private static final String COLOR_SAFDIR = "#FF26C6DA";
    private static final String COLOR_EXTDIR = "#FFD4E157";
    public static final String SHAREDPREF_NAME = "krkrsdl";
    public static final String SHAREDPREF_GAMEDIR = "gamedir";
    public static final String SHAREDPREF_GAMECONFIG = "gameargs";

    private enum DIR_TYPE {
        NOT_DIR, NORMAL_DIR, SAF_DIR
    }

    // ========== 游戏信息类 ==========

    class GameInfo {
        private boolean m_isIconCached;
        private Bitmap m_icon;
        public String m_path;
        public String m_name = null;

        // 缓存
        private boolean m_validChecked;
        private int m_validResult;
        private String m_pathTypeCache;
        private boolean m_autoCoverChecked;
        private Bitmap m_autoCover;

        public GameInfo(String path) {
            m_path = path;
            m_isIconCached = false;
            m_validChecked = false;
            File file = new File(m_path);
            if (file.isDirectory()) {
                m_name = file.getName();
            }
        }

        public String getPathType() {
            if (m_pathTypeCache == null) {
                m_pathTypeCache = m_path.contains("emulated") ? "inr" : "ext";
            }
            return m_pathTypeCache;
        }

        public String getGameDir() {
            return m_path;
        }

        public String getRealPath() {
            return m_path;
        }

        public Bitmap getIcon() {
            if (m_isIconCached) return m_icon;
            m_isIconCached = true;
            String iconpath;
            if (m_path.charAt(m_path.length() - 1) == '/') iconpath = m_path + "icon.png";
            else iconpath = m_path + "/icon.png";
            m_icon = BitmapFactory.decodeFile(iconpath);
            return m_icon;
        }

        /**
         * 获取封面：优先自动检测（本地 cover.png）→ icon.png
         */
        public Bitmap getCustomCover() {
            // 自动检测目录中的封面图片
            if (!m_autoCoverChecked) {
                m_autoCoverChecked = true;
                m_autoCover = detectCoverFile();
            }
            if (m_autoCover != null) return m_autoCover;

            return null;
        }

        /**
         * 在游戏目录下自动检测封面图片
         * 搜索常见封面文件名
         */
        private Bitmap detectCoverFile() {
            File dir = new File(m_path);
            if (!dir.isDirectory()) return null;
            File[] files = dir.listFiles();
            if (files == null) return null;

            String[] coverNames = {
                "封面.png", "封面.jpg", "封面.jpeg",
                "cover.png", "cover.jpg", "cover.jpeg",
                "folder.png", "folder.jpg",
                "thumb.png", "thumb.jpg",
                "icon.png"
            };
            for (String name : coverNames) {
                File f = new File(dir, name);
                if (f.exists() && f.isFile()) {
                    Bitmap bmp = BitmapFactory.decodeFile(f.getAbsolutePath());
                    if (bmp != null) return bmp;
                }
            }
            for (File f : files) {
                if (f.isFile()) {
                    String fn = f.getName().toLowerCase(Locale.US);
                    if (fn.endsWith(".png") || fn.endsWith(".jpg") || fn.endsWith(".jpeg")) {
                        Bitmap bmp = BitmapFactory.decodeFile(f.getAbsolutePath());
                        if (bmp != null) return bmp;
                    }
                }
            }
            return null;
        }

        public int checkValid(String startfilename) {
            m_validChecked = true;
            int res = 0;
            boolean has_script = false;

            File file = new File(m_path);
            if (file.canRead()) res += 4;
            if (file.canWrite()) res += 2;
            String[] list = file.list();
            if (list != null) {
                for (String name : list) {
                    if (startfilename.equals(name)) {
                        has_script = true;
                        break;
                    }
                }
            }
            if (has_script) res += 1;
            m_validResult = res;
            return res;
        }
    }

    // ========== 游戏库适配器 ==========

    class GamelistAdapter extends BaseAdapter {
        private LayoutInflater m_inflater;

        protected class ItemViewHolder {
            ImageView image_game_cover;
            ImageView image_cover_placeholder;
            TextView text_game_title;
            TextView text_game_path;
            TextView text_pathtype;
            TextView text_valid_badge;
            ImageButton button_play_game;
            TextView text_game_no;
        }

        @Override
        public int getCount() {
            return getVisibleGameCount();
        }

        @Override
        public Object getItem(int position) {
            if (m_gamelist == null) return null;
            int realPos = getVisiblePosition(position);
            if (realPos < 0 || realPos >= m_gamelist.size()) return null;
            return m_gamelist.get(realPos);
        }

        @Override
        public long getItemId(int position) {
            return position;
        }

        @SuppressLint({"InflateParams", "SetTextI18n", "DefaultLocale"})
        @Override
        public View getView(int position, View convertView, ViewGroup parent) {
            LinearLayout rootview;
            ItemViewHolder holder;
            if (convertView == null) {
                holder = new ItemViewHolder();
                if (m_inflater == null) m_inflater = LayoutInflater.from(parent.getContext());
                rootview = (LinearLayout) m_inflater.inflate(R.layout.layout_gameinfo, null);
                holder.image_game_cover = rootview.findViewById(R.id.image_game_cover);
                holder.image_cover_placeholder = rootview.findViewById(R.id.image_cover_placeholder);
                holder.text_game_title = rootview.findViewById(R.id.text_game_title);
                holder.text_game_path = rootview.findViewById(R.id.text_game_path);
                holder.text_pathtype = rootview.findViewById(R.id.text_pathtype);
                holder.text_valid_badge = rootview.findViewById(R.id.text_valid_badge);
                holder.button_play_game = rootview.findViewById(R.id.button_play_game);
                holder.text_game_no = rootview.findViewById(R.id.text_game_no);
                rootview.setTag(holder);
            } else {
                rootview = (LinearLayout) convertView;
                holder = (ItemViewHolder) convertView.getTag();
            }
            if (m_gamelist == null) return rootview;

            int realPos = getVisiblePosition(position);
            if (realPos < 0 || realPos >= m_gamelist.size()) return rootview;
            GameInfo gameinfo = m_gamelist.get(realPos);
            bindGameCard(holder, gameinfo, realPos);
            setupGameCardClick(rootview, holder, gameinfo, realPos);

            return rootview;
        }

        @SuppressLint({"SetTextI18n", "DefaultLocale"})
        private void bindGameCard(ItemViewHolder holder, GameInfo gameinfo, int position) {
            holder.text_game_title.setText(gameinfo.m_name != null ? gameinfo.m_name : "未知游戏");
            holder.text_game_path.setText(gameinfo.getRealPath());

            // 封面 - 优先使用自定义封面
            Bitmap cover = gameinfo.getCustomCover();
            if (cover == null) {
                cover = gameinfo.getIcon();
            }
            if (cover != null) {
                holder.image_game_cover.setImageBitmap(cover);
                holder.image_game_cover.setVisibility(View.VISIBLE);
                holder.image_cover_placeholder.setVisibility(View.GONE);
            } else {
                holder.image_game_cover.setVisibility(View.GONE);
                holder.image_cover_placeholder.setVisibility(View.VISIBLE);
            }

            // 路径类型标签
            String pathtype = gameinfo.getPathType();
            holder.text_pathtype.setText(pathtype);
            switch (pathtype) {
                case "inr":
                    holder.text_pathtype.setTextColor(Color.parseColor(COLOR_INRDIR));
                    break;
                case "ext":
                    holder.text_pathtype.setTextColor(Color.parseColor(COLOR_EXTDIR));
                    break;
                case "saf":
                    holder.text_pathtype.setTextColor(Color.parseColor(COLOR_SAFDIR));
                    break;
                default:
                    holder.text_pathtype.setTextColor(Color.parseColor(COLOR_NOTDIR));
                    break;
            }

            //
            JSONObject gameConfig = m_db != null ? m_db.getGameConfig(gameinfo.m_path) : new JSONObject();

            // 有效性徽章
            int valid = gameinfo.checkValid(gameConfig.optString("startup_file", "data.xp3"));
            if (valid == 7) {
                holder.text_valid_badge.setVisibility(View.VISIBLE);
                holder.text_valid_badge.setText("✓ 就绪");
            } else {
                holder.text_valid_badge.setVisibility(View.GONE);
            }

            holder.text_game_no.setText(String.format("%d/%d", position + 1, getCount()));
        }

        private void setupGameCardClick(LinearLayout rootview, ItemViewHolder holder,
                                        GameInfo gameinfo, int position) {
            holder.button_play_game.setOnClickListener(view -> launchGame(gameinfo));
            rootview.setOnClickListener(view -> showGameDetail(gameinfo, holder));
            rootview.setOnLongClickListener(view -> {
                showCoverMenu(gameinfo);
                return true;
            });
        }

        public void notifyGameLaunch(GameInfo gameinfo) {
            if (m_db != null) {
                m_db.recordGameLaunch(
                        gameinfo.m_name != null ? gameinfo.m_name : "未知游戏",
                        gameinfo.m_path,
                        false);
            }
            refreshHistory();
        }
    }

    // ========== 网格视图适配器 ==========

    class GridAdapter extends BaseAdapter {
        private LayoutInflater m_inflater;

        class GridViewHolder {
            ImageView image_cover;
            ImageView placeholder;
            TextView title;
            TextView tags;
            TextView pathtype;
        }

        @Override
        public int getCount() {
            return getVisibleGameCount();
        }

        @Override
        public Object getItem(int position) {
            if (m_gamelist == null) return null;
            int realPos = getVisiblePosition(position);
            if (realPos < 0 || realPos >= m_gamelist.size()) return null;
            return m_gamelist.get(realPos);
        }

        @Override
        public long getItemId(int position) {
            return position;
        }

        @SuppressLint("InflateParams")
        @Override
        public View getView(int position, View convertView, ViewGroup parent) {
            GridViewHolder holder;
            if (convertView == null) {
                holder = new GridViewHolder();
                if (m_inflater == null) m_inflater = LayoutInflater.from(parent.getContext());
                convertView = m_inflater.inflate(R.layout.layout_game_grid_item, parent, false);
                holder.image_cover = convertView.findViewById(R.id.grid_image_cover);
                holder.placeholder = convertView.findViewById(R.id.grid_cover_placeholder);
                holder.title = convertView.findViewById(R.id.grid_game_title);
                holder.tags = convertView.findViewById(R.id.grid_game_tags);
                holder.pathtype = convertView.findViewById(R.id.grid_pathtype);
                convertView.setTag(holder);
            } else {
                holder = (GridViewHolder) convertView.getTag();
            }

            if (m_gamelist == null) return convertView;
            int realPos = getVisiblePosition(position);
            if (realPos < 0 || realPos >= m_gamelist.size()) return convertView;
            final GameInfo gi = m_gamelist.get(realPos);

            // 标题
            holder.title.setText(gi.m_name != null ? gi.m_name : "未知游戏");

            // 封面
            Bitmap cover = gi.getCustomCover();
            if (cover == null) cover = gi.getIcon();
            if (cover != null) {
                holder.image_cover.setImageBitmap(cover);
                holder.image_cover.setVisibility(View.VISIBLE);
                holder.placeholder.setVisibility(View.GONE);
            } else {
                holder.image_cover.setVisibility(View.GONE);
                holder.placeholder.setVisibility(View.VISIBLE);
            }

            // 标签
            String tags = getGameTags(gi.m_path);
            if (tags != null && !tags.isEmpty()) {
                holder.tags.setText(tags);
                holder.tags.setVisibility(View.VISIBLE);
            } else {
                holder.tags.setVisibility(View.GONE);
            }

            // 路径类型
            holder.pathtype.setText(gi.getPathType());

            // 点击
            convertView.setOnClickListener(v -> {
                // 查找列表适配器中的 holder 以复用 showGameDetail
                // 简化：直接传 null 给 cardHolder
                showGameDetail(gi, null);
            });
            convertView.setOnLongClickListener(v -> {
                showCoverMenu(gi);
                return true;
            });

            return convertView;
        }
    }

    // ========== 历史记录适配器 ==========

    class HistoryAdapter extends BaseAdapter {
        private LayoutInflater m_inflater;
        private List<LauncherDatabase.HistoryEntry> m_historyList;

        public HistoryAdapter() {
            m_historyList = new ArrayList<>();
        }

        public void setData(List<LauncherDatabase.HistoryEntry> data) {
            m_historyList = data;
            notifyDataSetChanged();
        }

        @Override
        public int getCount() {
            return m_historyList.size();
        }

        @Override
        public Object getItem(int position) {
            return m_historyList.get(position);
        }

        @Override
        public long getItemId(int position) {
            return position;
        }

        @SuppressLint("InflateParams")
        @Override
        public View getView(int position, View convertView, ViewGroup parent) {
            LinearLayout rootview;
            if (convertView == null) {
                if (m_inflater == null) m_inflater = LayoutInflater.from(parent.getContext());
                rootview = (LinearLayout) m_inflater.inflate(R.layout.layout_history_item, null);
            } else {
                rootview = (LinearLayout) convertView;
            }

            LauncherDatabase.HistoryEntry entry = m_historyList.get(position);

            ImageView cover = rootview.findViewById(R.id.image_history_cover);
            TextView title = rootview.findViewById(R.id.text_history_title);
            TextView time = rootview.findViewById(R.id.text_history_time);
            TextView count = rootview.findViewById(R.id.text_history_count);
            ImageButton playBtn = rootview.findViewById(R.id.button_history_play);
            ImageButton deleteBtn = rootview.findViewById(R.id.button_history_delete);

            title.setText(entry.gameName);
            SimpleDateFormat sdf = new SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.getDefault());
            time.setText("最后游玩: " + sdf.format(new Date(entry.lastPlayed)));
            count.setText("游玩 " + entry.playCount + " 次");

            // 尝试从游戏目录加载封面
            Bitmap coverBmp = null;
            String gameDir = entry.gamePath;
            if (gameDir != null) {
                String[] coverNames = {
                    "cover.png", "cover.jpg", "cover.jpeg",
                    "封面.png", "封面.jpg",
                    "folder.png", "icon.png"
                };
                // 如果 gamePath 是文件路径，取其目录
                File gf = new File(gameDir);
                File dir = gf.isDirectory() ? gf : gf.getParentFile();
                if (dir != null) {
                    for (String name : coverNames) {
                        File f = new File(dir, name);
                        if (f.exists()) {
                            coverBmp = BitmapFactory.decodeFile(f.getAbsolutePath());
                            if (coverBmp != null) break;
                        }
                    }
                }
                // 也尝试数据库路径（兼容旧数据）
                if (coverBmp == null && entry.coverPath != null) {
                    coverBmp = BitmapFactory.decodeFile(entry.coverPath);
                }
            }
            if (coverBmp != null) {
                cover.setImageBitmap(coverBmp);
                cover.setVisibility(View.VISIBLE);
            } else {
                cover.setVisibility(View.GONE);
            }

            playBtn.setOnClickListener(view -> {
                for (GameInfo gi : m_gamelist) {
                    if (gi.m_path.equals(entry.gamePath)) {
                        launchGame(gi);
                        return;
                    }
                }
                Toast.makeText(MainActivity.this, "游戏目录不在当前列表中", Toast.LENGTH_SHORT).show();
            });

            deleteBtn.setOnClickListener(view -> {
                if (m_db != null) {
                    m_db.deleteHistory(entry.id);
                    refreshHistory();
                    Toast.makeText(MainActivity.this, "已删除记录", Toast.LENGTH_SHORT).show();
                }
            });

            return rootview;
        }
    }

    // ========== 历史记录网格适配器 ==========

    class HistoryGridAdapter extends BaseAdapter {
        private LayoutInflater m_inflater;
        private List<LauncherDatabase.HistoryEntry> m_historyList;

        public HistoryGridAdapter() {
            m_historyList = new ArrayList<>();
        }

        public void setData(List<LauncherDatabase.HistoryEntry> data) {
            m_historyList = data;
            notifyDataSetChanged();
        }

        @Override
        public int getCount() {
            return m_historyList.size();
        }

        @Override
        public Object getItem(int position) {
            return m_historyList.get(position);
        }

        @Override
        public long getItemId(int position) {
            return position;
        }

        @SuppressLint("InflateParams")
        @Override
        public View getView(int position, View convertView, ViewGroup parent) {
            ViewHolder holder;
            if (convertView == null) {
                holder = new ViewHolder();
                if (m_inflater == null) m_inflater = LayoutInflater.from(parent.getContext());
                convertView = m_inflater.inflate(R.layout.layout_history_grid_item, parent, false);
                holder.image_cover = convertView.findViewById(R.id.hgrid_image_cover);
                holder.placeholder = convertView.findViewById(R.id.hgrid_cover_placeholder);
                holder.title = convertView.findViewById(R.id.hgrid_game_title);
                holder.time = convertView.findViewById(R.id.hgrid_game_time);
                holder.count = convertView.findViewById(R.id.hgrid_game_count);
                convertView.setTag(holder);
            } else {
                holder = (ViewHolder) convertView.getTag();
            }

            LauncherDatabase.HistoryEntry entry = m_historyList.get(position);

            holder.title.setText(entry.gameName);
            SimpleDateFormat sdf = new SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.getDefault());
            holder.time.setText("最后游玩: " + sdf.format(new Date(entry.lastPlayed)));
            holder.count.setText("游玩 " + entry.playCount + " 次");

            // 尝试加载封面
            Bitmap coverBmp = null;
            String gameDir = entry.gamePath;
            if (gameDir != null) {
                String[] coverNames = {
                    "cover.png", "cover.jpg", "cover.jpeg",
                    "封面.png", "封面.jpg",
                    "folder.png", "icon.png"
                };
                File gf = new File(gameDir);
                File dir = gf.isDirectory() ? gf : gf.getParentFile();
                if (dir != null) {
                    for (String name : coverNames) {
                        File f = new File(dir, name);
                        if (f.exists()) {
                            coverBmp = BitmapFactory.decodeFile(f.getAbsolutePath());
                            if (coverBmp != null) break;
                        }
                    }
                }
                if (coverBmp == null && entry.coverPath != null) {
                    coverBmp = BitmapFactory.decodeFile(entry.coverPath);
                }
            }
            if (coverBmp != null) {
                holder.image_cover.setImageBitmap(coverBmp);
                holder.image_cover.setVisibility(View.VISIBLE);
                holder.placeholder.setVisibility(View.GONE);
            } else {
                holder.image_cover.setVisibility(View.GONE);
                holder.placeholder.setVisibility(View.VISIBLE);
            }

            // 点击启动
            convertView.setOnClickListener(v -> {
                for (GameInfo gi : m_gamelist) {
                    if (gi.m_path.equals(entry.gamePath)) {
                        launchGame(gi);
                        return;
                    }
                }
                Toast.makeText(MainActivity.this, "游戏目录不在当前列表中", Toast.LENGTH_SHORT).show();
            });

            convertView.setOnLongClickListener(v -> {
                if (m_db != null) {
                    m_db.deleteHistory(entry.id);
                    refreshHistory();
                    Toast.makeText(MainActivity.this, "已删除记录", Toast.LENGTH_SHORT).show();
                }
                return true;
            });

            return convertView;
        }

        class ViewHolder {
            ImageView image_cover;
            ImageView placeholder;
            TextView title;
            TextView time;
            TextView count;
        }
    }

    // ========== 成员变量 ==========

    private String[] m_appdirs;
    private String m_gamedir = null;
    private ArrayList<GameInfo> m_gamelist;
    private SharedPreferences m_sharedpref;
    private JSONObject m_gameargs;
    private LauncherDatabase m_db;

    private EditText m_textgamedir;
    private GamelistAdapter m_listgameadaptor;
    private HistoryAdapter m_historyAdapter;

    private LinearLayout m_panelLibrary;
    private FrameLayout m_panelHistory;
    private LinearLayout m_panelRecent;
    private FrameLayout m_panelSettings;
    private TextView m_tabLibrary;
    private TextView m_tabRecent;
    private TextView m_tabSettings;
    private ListView m_listHistory;
    private HistoryGridAdapter m_historyGridAdapter;
    private TextView m_textEmptyLibrary;
    private TextView m_textEmptyHistory;

    private GameInfo m_currentCoverGame;

    // 网格/列表视图（游戏库）
    private GridView m_gridGame;
    private boolean m_isGridView;
    private ImageButton m_buttonViewToggle;

    // 网格/列表视图（最近游玩）
    private GridView m_gridHistory;
    private boolean m_historyIsGridView;
    private ImageButton m_buttonHistoryViewToggle;

    // 标签筛选
    private LinearLayout m_tagFilterContainer;
    private String m_activeFilterTag; // null = 显示全部

    // 当前详情中正在编辑的游戏（用于启动文件选择）
    private GameInfo m_currentDetailGame;

        /**
     * 判断游戏是否匹配当前标签筛选
     */
    private boolean isGameVisible(GameInfo gi) {
        if (m_activeFilterTag == null || m_activeFilterTag.isEmpty()) return true;
        String tags = getGameTags(gi.m_path);
        if (tags.isEmpty()) return false;
        String[] parts = tags.split(",");
        for (String t : parts) {
            if (t.trim().equalsIgnoreCase(m_activeFilterTag)) return true;
        }
        return false;
    }

    /**
     * 获取符合当前筛选的游戏数量
     */
    private int getVisibleGameCount() {
        if (m_gamelist == null) return 0;
        if (m_activeFilterTag == null || m_activeFilterTag.isEmpty()) return m_gamelist.size();
        int count = 0;
        for (GameInfo gi : m_gamelist) {
            if (isGameVisible(gi)) count++;
        }
        return count;
    }

    /**
     * 获取第 position 个可见游戏的原始索引
     */
    private int getVisiblePosition(int position) {
        if (m_activeFilterTag == null || m_activeFilterTag.isEmpty()) return position;
        int idx = 0;
        for (int i = 0; i < m_gamelist.size(); i++) {
            if (isGameVisible(m_gamelist.get(i))) {
                if (idx == position) return i;
                idx++;
            }
        }
        return position;
    }

    /**
     * 将 ACTION_OPEN_DOCUMENT_TREE 返回的 content URI 转为可读的文件路径
     * 例如: content://.../tree/primary%3Agame%2Fpath → /storage/emulated/0/game/path
     */
    private String uri2Filepath(Uri uri) {
        String str = uri.toString();
        // 提取 /tree/ 后面的路径部分
        int idx = str.indexOf("/tree/");
        if (idx < 0) return null;
        String encoded = str.substring(idx + 6);
        // 解码并替换 primary: 为 /storage/emulated/0/
        String decoded = Uri.decode(encoded);
        // 处理可能存在的 /document/ 二次路径
        int docIdx = decoded.indexOf("/document/");
        if (docIdx >= 0) {
            decoded = decoded.substring(0, docIdx);
        }
        if (decoded.startsWith("primary:")) {
            return "/storage/emulated/0/" + decoded.substring(8);
        } else {
            // 外置 SD 卡: XXXX-XXXX/path
            return "/storage/" + decoded.replace(":", "/");
        }
    }

    // ========== 工具方法 ==========

    public static String argsToCmd(ArrayList<String> args) {
        StringBuilder cmd = new StringBuilder();
        for (String arg : args) {
            cmd.append(arg).append(" ");
        }
        return cmd.toString().trim();
    }

    public static ArrayList<String> cmdToArgs(String cmd) {
        ArrayList<String> args = new ArrayList<>();
        for (String arg : cmd.split(" ")) {
            if (arg == null || arg.length() == 0) continue;
            args.add(arg);
        }
        return args;
    }

    // ========== Activity生命周期 ==========

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
                WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS);
        setContentView(R.layout.activity_main);

        // 跟随系统深色/浅色模式
        androidx.appcompat.app.AppCompatDelegate.setDefaultNightMode(
                androidx.appcompat.app.AppCompatDelegate.MODE_NIGHT_FOLLOW_SYSTEM);

        m_db = LauncherDatabase.getInstance(this);

        initValue();
        initUiTabs();   // 注册点击事件，但不选默认标签（下方根据历史决定）
        initUiGameConfig();
        initUiGameDir();
        initUiHistory();
        updateGamelist();
        refreshHistory();

        // 有历史记录时默认显示最近游玩，否则显示游戏库
        if (m_historyAdapter != null && m_historyAdapter.getCount() > 0) {
            switchTab(0);
        } else {
            switchTab(1);
        }

        if (!checkStoragePermission()) {
            requestStoragePermission();
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent resultData) {
        super.onActivityResult(requestCode, resultCode, resultData);

        if (requestCode == REQUEST_DIR_PICKER && resultData != null) {
            Uri uri = resultData.getData();
            if (uri != null) {
                // 将 content URI 转换为文件路径
                String path = uri2Filepath(uri);
                if (path != null) {
                    m_textgamedir.setText(path);
                    m_gamedir = path;
                    DIR_TYPE dir_type = updateGameDir();
                    if (dir_type != DIR_TYPE.NOT_DIR) updateGamelist();
                }
            }
        } else if (requestCode == REQUEST_COVER_IMAGE) {
            if (resultData != null && resultData.getData() != null && m_currentCoverGame != null) {
                Uri coverUri = resultData.getData();
                try {
                    // 直接保存到游戏目录下的 cover.png，这样自动检测就能找到
                    String gamePath = m_currentCoverGame.m_path;
                    if (gamePath.charAt(gamePath.length() - 1) == '/') {
                        gamePath = gamePath.substring(0, gamePath.length() - 1);
                    }
                    String destPath = gamePath + "/cover.png";
                    File destFile = new File(destPath);
                    File parentDir = destFile.getParentFile();
                    if (parentDir != null && !parentDir.exists()) parentDir.mkdirs();

                    try (InputStream inputStream = getContentResolver().openInputStream(coverUri)) {
                        if (inputStream != null) {
                            Bitmap bitmap = BitmapFactory.decodeStream(inputStream);
                            if (bitmap != null) {
                                try (java.io.FileOutputStream fos = new java.io.FileOutputStream(destFile)) {
                                    bitmap.compress(Bitmap.CompressFormat.PNG, 90, fos);
                                }
                                // 清除缓存，下次 getCustomCover 直接从目录检测
                                m_currentCoverGame.m_autoCoverChecked = false;
                                m_currentCoverGame.m_autoCover = null;
                                // 同时更新数据库记录（兼容旧逻辑）
                                if (m_db != null) {
                                    m_db.saveCoverPath(m_currentCoverGame.m_path, null);
                                }
                                // 刷新所有视图
                                notifyDataChanged();
                                Toast.makeText(this, "封面已保存到游戏目录", Toast.LENGTH_SHORT).show();
                            }
                        }
                    }
                } catch (Exception e) {
                    Log.e("## krkrsdl", "Failed to save cover: " + e.getMessage());
                    Toast.makeText(this, "添加封面失败", Toast.LENGTH_SHORT).show();
                }
                m_currentCoverGame = null;
            }
        }
    }

    @Override
    public void onConfigurationChanged(@NonNull Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        if (m_listgameadaptor != null) m_listgameadaptor.notifyDataSetChanged();
        if (m_gridGame != null && m_gridGame.getAdapter() != null) {
            ((BaseAdapter)m_gridGame.getAdapter()).notifyDataSetChanged();
        }
    }

    // ========== 初始化 ==========

    private void initValue() {
        m_gamelist = new ArrayList<>();
        m_sharedpref = getSharedPreferences(SHAREDPREF_NAME, MODE_PRIVATE);
        m_gamedir = m_sharedpref.getString(SHAREDPREF_GAMEDIR, null);

        m_appdirs = getAppDirectories();
    }

    /**
     * 获取应用文件目录
     */
    private String[] getAppDirectories() {
        java.util.List<String> dirs = new ArrayList<>();
        File file = getFilesDir();
        if (file != null) dirs.add(file.toString());
        File[] files = getExternalFilesDirs(null);
        for (File f : files) {
            if (f != null) dirs.add(f.toString());
        }
        return dirs.toArray(new String[0]);
    }

    private void initUiTabs() {
        m_panelLibrary = (LinearLayout) findViewById(R.id.panel_library);
        m_panelRecent = (LinearLayout) findViewById(R.id.panel_recent);
        m_panelSettings = (FrameLayout) findViewById(R.id.panel_settings);
        m_tabLibrary = findViewById(R.id.tab_library);
        m_tabRecent = findViewById(R.id.tab_recent);
        m_tabSettings = findViewById(R.id.tab_settings);
        m_textEmptyLibrary = findViewById(R.id.text_empty_library);
        m_textEmptyHistory = findViewById(R.id.text_empty_history);
        m_tagFilterContainer = findViewById(R.id.tag_filter_container);

        m_tabRecent.setOnClickListener(v -> switchTab(0));
        m_tabLibrary.setOnClickListener(v -> switchTab(1));
        m_tabSettings.setOnClickListener(v -> switchTab(2));
    }

    private void switchTab(int index) {
        m_panelLibrary.setVisibility(View.GONE);
        if (m_panelRecent != null) m_panelRecent.setVisibility(View.GONE);
        m_panelSettings.setVisibility(View.GONE);

        m_tabLibrary.setBackgroundColor(Color.TRANSPARENT);
        if (m_tabRecent != null) m_tabRecent.setBackgroundColor(Color.TRANSPARENT);
        m_tabSettings.setBackgroundColor(Color.TRANSPARENT);

        switch (index) {
            case 0: // 最近游玩
                if (m_panelRecent != null) {
                    m_panelRecent.setVisibility(View.VISIBLE);
                }
                if (m_tabRecent != null) {
                    m_tabRecent.setBackgroundResource(R.drawable.btn_secondary);
                }
                refreshHistory();
                break;
            case 1: // 游戏库
                m_panelLibrary.setVisibility(View.VISIBLE);
                m_tabLibrary.setBackgroundResource(R.drawable.btn_secondary);
                break;
            case 2: // 设置
                m_panelSettings.setVisibility(View.VISIBLE);
                m_tabSettings.setBackgroundResource(R.drawable.btn_secondary);
                break;
        }
    }

    // ========== 游戏配置 ==========

    private void initUiGameConfig() {
        updateGameConfig(true);
        LinearLayout layout_config = findViewById(R.id.layout_gameconfig);

        for (int i = 0; i < layout_config.getChildCount(); i++) {
            setupConfigListener(layout_config.getChildAt(i));
        }

        Button button_checkupdate = findViewById(R.id.button_checkupdate);
        button_checkupdate.setOnClickListener(view -> {
            Intent intent = new Intent();
            intent.setAction("android.intent.action.VIEW");
            intent.setData(Uri.parse("https://github.com/krkrsdl3/krkrsdl3"));
            startActivity(intent);
        });

        Button button_about = findViewById(R.id.button_about);
        button_about.setOnClickListener(view -> {
            AlertDialog.Builder builder = new AlertDialog.Builder(MainActivity.this);
            builder.setTitle(getResources().getString(R.string.app_name));
            builder.setMessage(getResources().getString(R.string.app_about));
            builder.setCancelable(true);
            builder.setPositiveButton("确定", (dialog, which) -> dialog.dismiss());
            AlertDialog dialog = builder.create();
            dialog.show();
        });
    }

    private void setupConfigListener(View v) {
        if (v instanceof LinearLayout) {
            for (int j = 0; j < ((LinearLayout) v).getChildCount(); j++) {
                setupConfigListener(((LinearLayout) v).getChildAt(j));
            }
        } else if (v instanceof CheckBox) {
            v.setOnClickListener(view -> updateGameConfig(false));
        } else if (v instanceof EditText) {
            ((EditText) v).addTextChangedListener(new TextWatcher() {
                @Override
                public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
                @Override
                public void onTextChanged(CharSequence s, int start, int before, int count) {}
                @Override
                public void afterTextChanged(Editable s) { updateGameConfig(false); }
            });
        } else if (v instanceof RadioGroup) {
            ((RadioGroup) v).setOnCheckedChangeListener((group, checkedId) -> updateGameConfig(false));
        }
    }

    // ========== 游戏目录 ==========

    private void initUiGameDir() {
        ImageButton button_explore = findViewById(R.id.button_explore);
        button_explore.setOnClickListener(view -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            startActivityForResult(intent, REQUEST_DIR_PICKER);
        });

        m_textgamedir = findViewById(R.id.text_gamedir);
        if (m_gamedir != null) {
            updateGameDir();
        }

        m_textgamedir.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
            @Override
            public void onTextChanged(CharSequence s, int start, int before, int count) {}
            @Override
            public void afterTextChanged(Editable s) {
                m_gamedir = s.toString();
                DIR_TYPE dir_type = updateGameDir();
                if (dir_type != DIR_TYPE.NOT_DIR) updateGamelist();
            }
        });

        ListView listgame = findViewById(R.id.list_game);
        m_listgameadaptor = new GamelistAdapter();
        listgame.setAdapter(m_listgameadaptor);

        // 网格视图
        m_gridGame = findViewById(R.id.grid_game);
        m_gridGame.setAdapter(new GridAdapter());
        m_gridGame.setOnItemClickListener((parent, view, position, id) -> {
            int realPos = getVisiblePosition(position);
            if (m_gamelist != null && realPos >= 0 && realPos < m_gamelist.size()) {
                showGameDetail(m_gamelist.get(realPos), null);
            }
        });
        m_gridGame.setOnItemLongClickListener((parent, view, position, id) -> {
            int realPos = getVisiblePosition(position);
            if (m_gamelist != null && realPos >= 0 && realPos < m_gamelist.size()) {
                showCoverMenu(m_gamelist.get(realPos));
                return true;
            }
            return false;
        });

        // 视图切换按钮
        m_buttonViewToggle = findViewById(R.id.button_view_toggle);
        m_isGridView = false;
        m_buttonViewToggle.setOnClickListener(v -> toggleViewMode());
    }

    /**
     * 切换列表/网格视图
     */
    private void toggleViewMode() {
        m_isGridView = !m_isGridView;
        ListView lv = findViewById(R.id.list_game);
        GridView gv = findViewById(R.id.grid_game);
        if (m_isGridView) {
            lv.setVisibility(View.GONE);
            gv.setVisibility(View.VISIBLE);
            m_buttonViewToggle.setImageResource(R.drawable.ic_list);
        } else {
            lv.setVisibility(View.VISIBLE);
            gv.setVisibility(View.GONE);
            m_buttonViewToggle.setImageResource(R.drawable.ic_grid);
        }
    }

    private DIR_TYPE checkDir(String dirpath) {
        if (dirpath == null) return DIR_TYPE.NOT_DIR;
        try {
            File file = new File(dirpath);
            if (file.isDirectory()) {
                return DIR_TYPE.NORMAL_DIR;
            }
            return DIR_TYPE.NOT_DIR;
        } catch (Exception e) {
            return DIR_TYPE.NOT_DIR;
        }
    }

    private DIR_TYPE updateGameDir() {
        DIR_TYPE dir_type = checkDir(m_gamedir);
        switch (dir_type) {
            case NOT_DIR:
                m_sharedpref.edit().remove(SHAREDPREF_GAMEDIR).apply();
                m_textgamedir.setTextColor(Color.parseColor(COLOR_NOTDIR));
                break;
            case NORMAL_DIR:
                m_sharedpref.edit().putString(SHAREDPREF_GAMEDIR, m_gamedir).apply();
                m_textgamedir.setTextColor(Color.parseColor(COLOR_INRDIR));
                break;
            case SAF_DIR:
                m_sharedpref.edit().remove(SHAREDPREF_GAMEDIR).apply();
                m_textgamedir.setTextColor(Color.parseColor(COLOR_SAFDIR));
                break;
        }
        String _gamedir = m_textgamedir.getText().toString();
        if (!_gamedir.equals(m_gamedir)) m_textgamedir.setText(m_gamedir);
        return dir_type;
    }

    // ========== 游戏列表管理 ==========

    private void updateGamelist() {
        m_gamelist.clear();

        if (m_appdirs != null) {
            for (String appdir : m_appdirs) {
                File dir = new File(appdir);
                File[] files = dir.listFiles();
                if (files != null) {
                    for (File file : files) {
                        if (file == null || file.getName().equals("save")) continue;
                        GameInfo gameinfo = new GameInfo(file.getAbsolutePath());
                        if (gameinfo.m_name != null) m_gamelist.add(gameinfo);
                    }
                }
            }
        }

        if (m_gamedir != null) {
            File dir = new File(m_gamedir);
            File[] files = dir.listFiles();
            if (files != null) {
                for (File file : files) {
                    if (file == null) continue;
                    GameInfo gameinfo = new GameInfo(file.getAbsolutePath());
                    if (gameinfo.m_name != null) m_gamelist.add(gameinfo);
                }
            }
        }

        refreshGameUI();
        if (m_textEmptyLibrary != null) {
            m_textEmptyLibrary.setVisibility(m_gamelist.isEmpty() ? View.VISIBLE : View.GONE);
        }
    }

    // ========== 历史记录 ==========

    private void initUiHistory() {
        // 列表
        m_listHistory = findViewById(R.id.list_history);
        m_historyAdapter = new HistoryAdapter();
        m_listHistory.setAdapter(m_historyAdapter);

        // 网格
        m_gridHistory = findViewById(R.id.grid_history);
        m_historyGridAdapter = new HistoryGridAdapter();
        m_gridHistory.setAdapter(m_historyGridAdapter);
        m_gridHistory.setOnItemClickListener((parent, view, position, id) -> {
            LauncherDatabase.HistoryEntry entry = (LauncherDatabase.HistoryEntry) m_historyGridAdapter.getItem(position);
            if (entry != null) {
                for (GameInfo gi : m_gamelist) {
                    if (gi.m_path.equals(entry.gamePath)) {
                        launchGame(gi);
                        return;
                    }
                }
                Toast.makeText(MainActivity.this, "游戏目录不在当前列表中", Toast.LENGTH_SHORT).show();
            }
        });
        m_gridHistory.setOnItemLongClickListener((parent, view, position, id) -> {
            LauncherDatabase.HistoryEntry entry = (LauncherDatabase.HistoryEntry) m_historyGridAdapter.getItem(position);
            if (entry != null && m_db != null) {
                m_db.deleteHistory(entry.id);
                refreshHistory();
                Toast.makeText(MainActivity.this, "已删除记录", Toast.LENGTH_SHORT).show();
                return true;
            }
            return false;
        });

        // 视图切换
        m_historyIsGridView = false;
        m_buttonHistoryViewToggle = findViewById(R.id.button_history_view_toggle);
        m_buttonHistoryViewToggle.setOnClickListener(v -> toggleHistoryViewMode());
    }

    private void toggleHistoryViewMode() {
        m_historyIsGridView = !m_historyIsGridView;
        ListView lv = m_listHistory;
        GridView gv = m_gridHistory;
        if (m_historyIsGridView) {
            lv.setVisibility(View.GONE);
            gv.setVisibility(View.VISIBLE);
            m_buttonHistoryViewToggle.setImageResource(R.drawable.ic_list);
        } else {
            lv.setVisibility(View.VISIBLE);
            gv.setVisibility(View.GONE);
            m_buttonHistoryViewToggle.setImageResource(R.drawable.ic_grid);
        }
    }

    private void refreshHistory() {
        if (m_db != null && m_historyAdapter != null) {
            List<LauncherDatabase.HistoryEntry> history = m_db.getAllHistory();
            m_historyAdapter.setData(history);
            if (m_historyGridAdapter != null) {
                m_historyGridAdapter.setData(history);
            }
            if (m_textEmptyHistory != null) {
                m_textEmptyHistory.setVisibility(history.isEmpty() ? View.VISIBLE : View.GONE);
            }
        }
    }

    // ========== 游戏启动逻辑 ==========

    /**
     * 构建引擎启动参数（从全局配置 + 单人配置合并）
     * 单人配置可覆盖：startup_file（首参数）、render、scopedsavedir
     * 如果设置了 customArgs，则完全替换全局配置生成的其他参数
     */
    private ArrayList<String> buildLaunchArgs(String gamedir, JSONObject gameConfig) {
        ArrayList<String> args = new ArrayList<>();

        // ① 首参数：启动文件
        String startupFile = "data.xp3";
        if (gameConfig != null) {
            String sf = gameConfig.optString("startup_file", "");
            if (!sf.isEmpty()) {
                int si = sf.lastIndexOf('/');
                startupFile = si >= 0 ? sf.substring(si + 1) : sf;
            }
        }
        args.add(startupFile);

        // ② 如果游戏有自定义 args，直接用（跳过下方全局配置）
        if (gameConfig != null) {
            String ca = gameConfig.optString("args", "");
            if (!ca.isEmpty()) {
                args.addAll(cmdToArgs(ca));
                return args;
            }
        }

        // ③ 合并全局配置中的各项参数
        try {
            if (m_gameargs == null) return args;

            // 渲染器（优先取单人配置，否则全局）
            String render;
            if (gameConfig != null && gameConfig.has("render")) {
                render = gameConfig.optString("render", "software");
            } else {
                render = m_gameargs.optString("render", "software");
            }
            args.add("-render=" + render);

            // 隔离存档目录
            if (m_gameargs.optBoolean("scopedsavedir", false)) {
                int idx;
                if (gamedir.charAt(gamedir.length() - 1) != '/')
                    idx = gamedir.lastIndexOf('/', gamedir.length() - 2);
                else idx = gamedir.lastIndexOf('/');
                String gamename = idx >= 0 ? gamedir.substring(idx + 1) : gamedir;
                String savedir = getExternalFilesDir(null).toString();
                if (savedir.charAt(savedir.length() - 1) != '/') savedir += '/';
                savedir += "save/" + gamename;
                File file = new File(savedir);
                if (file.exists() || file.mkdirs()) {
                    args.add("--save-dir");
                    args.add(savedir);
                }
            }
        } catch (Exception e) {
            Log.e("## krkrsdl", "buildLaunchArgs error: " + e.getMessage());
        }
        return args;
    }

    /**
     * 构建详情弹窗中显示的参数字符串
     * 格式: "startup_file [custom_args]"
     * 无自定义时显示默认值
     */
    private String buildDisplayString(JSONObject gameConfig) {
        String sf = gameConfig != null ? gameConfig.optString("startup_file", "") : "";
        String ca = gameConfig != null ? gameConfig.optString("args", "") : "";
        StringBuilder sb = new StringBuilder();
        if (!sf.isEmpty()) {
            int si = sf.lastIndexOf('/');
            sb.append(si >= 0 ? sf.substring(si + 1) : sf);
        } else {
            sb.append("data.xp3");
        }
        if (!ca.isEmpty()) sb.append(" ").append(ca);
        return sb.toString();
    }

    /**
     * 从命令字符串保存到 gameConfig（拆分 startup_file + args）
     */
    private void saveArgsConfig(String gamePath, String cmdString) {
        if (m_db == null) return;
        try {
            String[] parts = cmdString.split(" ", 2);
            String first = parts[0];
            String rest = parts.length > 1 ? parts[1] : "";
            JSONObject config = new JSONObject();
            boolean hasOverride = false;
            if (!first.equals("data.xp3")) {
                config.put("startup_file", first);
                hasOverride = true;
            }
            if (!rest.isEmpty()) {
                config.put("args", rest);
                hasOverride = true;
            }
            m_db.saveGameConfig(gamePath, hasOverride ? config : new JSONObject());
        } catch (JSONException e) {
            Log.e("## krkrsdl", "saveArgsConfig error: " + e.getMessage());
        }
    }

    private void launchGame(GameInfo gameinfo) {
        JSONObject gameConfig = m_db.getGameConfig(gameinfo.m_path);
        int gamemod = gameinfo.checkValid(gameConfig.optString("startup_file", "data.xp3"));
        ArrayList<String> m_gameargs;

        // 加载单人配置并合并
        if (m_db != null) {
            m_gameargs = buildLaunchArgs(gameinfo.getGameDir(), gameConfig);
        } else {
            m_gameargs = buildLaunchArgs(gameinfo.getGameDir(), null);
        }

        startkrkrsdl(gamemod, gameinfo.getRealPath(), m_gameargs, false);

        if (m_listgameadaptor != null) {
            m_listgameadaptor.notifyGameLaunch(gameinfo);
        }
    }

    private void startkrkrsdl(int mode, String gamepath, ArrayList<String> args, boolean usesaf) {
        if (mode == 7) {
            if (args.size() > 0) {
                // 游戏目录 + "/" + 文件名
                args.set(0, gamepath + "/" + args.get(0));
            }
            MainActivity.this.startkrkrsdl(args, usesaf);
            return;
        }
        AlertDialog.Builder builder = new AlertDialog.Builder(MainActivity.this);
        builder.setTitle("错误：游戏目录不完整");
        StringBuilder message = new StringBuilder();
        message.append("路径: ").append(gamepath).append("\n\n");
        if ((mode & 4) == 0) message.append("⚠ 目录不可读\n");
        if ((mode & 2) == 0) message.append("⚠ 目录不可写\n");
        if ((mode & 1) == 0) message.append("⚠ 未找到 " + args.get(0) + "\n\n");
        builder.setMessage(message);
        builder.setCancelable(true);
        builder.setPositiveButton("确认", (dialog, which) -> dialog.dismiss());
        builder.setNegativeButton("取消", (dialog, which) -> dialog.dismiss());
        AlertDialog dialog = builder.create();
        dialog.show();
    }

    private void startkrkrsdl(ArrayList<String> m_gameargs, boolean usesaf) {
        Intent intent = new Intent();
        intent.setClass(this, KRKRActivity.class);
        intent.putStringArrayListExtra(SHAREDPREF_GAMECONFIG, m_gameargs);
        startActivity(intent);
    }

    /**
     * 显示启动文件选择器 — 列出游戏目录中的所有文件供选择
     * 先尝试 gameinfo.m_path，如果目录不存在或为空，再尝试父目录
     */
    private void showStartupFilePicker(GameInfo gameinfo, final TextView targetText,
                                        final Runnable onSelected) {
        final List<String> fileNames = new ArrayList<>();
        final List<String> filePaths = new ArrayList<>();

        // 尝试从目录收集文件
        String[] dirsToTry = {
            gameinfo.m_path,
            gameinfo.getGameDir(),
            gameinfo.m_path != null ? new File(gameinfo.m_path).getParent() : null
        };
        for (String dirPath : dirsToTry) {
            if (dirPath == null || dirPath.isEmpty()) continue;
            if (!fileNames.isEmpty()) break; // 已有文件就不继续了
            File dir = new File(dirPath);
            if (!dir.isDirectory()) continue;
            File[] files = dir.listFiles();
            if (files == null) continue;
            for (File f : files) {
                if (f.isFile()) {
                    fileNames.add(f.getName());
                    filePaths.add(f.getAbsolutePath());
                }
            }
        }

        if (fileNames.isEmpty()) {
            Toast.makeText(this, "目录 \"" + gameinfo.m_path + "\" 中没有找到文件", Toast.LENGTH_LONG).show();
            return;
        }

        String[] items = fileNames.toArray(new String[0]);
        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        builder.setTitle("选择启动文件");
        builder.setItems(items, (dialog, which) -> {
            // 只保存文件名，路径拼接由程序处理
            String selectedName = fileNames.get(which);
            // 以 / 开头表示相对游戏目录的路径
            targetText.setText(selectedName);
            if (onSelected != null) onSelected.run();
        });
        builder.setNegativeButton("取消", null);
        builder.show();
    }

    /**
     * 获取游戏的标签字符串（用于显示）
     */
    private String getGameTags(String gamePath) {
        if (m_db == null) return "";
        JSONObject config = m_db.getGameConfig(gamePath);
        return config.optString("tags", "");
    }

    /**
     * 收集所有游戏的标签并重建筛选栏
     */
    private void rebuildTagFilter() {
        if (m_tagFilterContainer == null) return;
        m_tagFilterContainer.removeAllViews();

        // 收集所有标签
        final HashSet<String> tagSet = new HashSet<>();
        if (m_gamelist != null) {
            for (GameInfo gi : m_gamelist) {
                String tags = getGameTags(gi.m_path);
                if (!tags.isEmpty()) {
                    String[] parts = tags.split(",");
                    for (String t : parts) {
                        String trimmed = t.trim();
                        if (!trimmed.isEmpty()) tagSet.add(trimmed);
                    }
                }
            }
        }

        if (tagSet.isEmpty()) return;

        // 添加"全部"标签
        final TextView allChip = makeTagChip("全部", m_activeFilterTag == null);
        allChip.setOnClickListener(v -> {
            m_activeFilterTag = null;
            applyTagFilter();
            refreshTagChips();
        });
        m_tagFilterContainer.addView(allChip);

        // 添加每个标签
        for (final String tag : tagSet) {
            final TextView chip = makeTagChip(tag, tag.equals(m_activeFilterTag));
            chip.setOnClickListener(v -> {
                m_activeFilterTag = tag.equals(m_activeFilterTag) ? null : tag;
                applyTagFilter();
                refreshTagChips();
            });
            m_tagFilterContainer.addView(chip);
        }
    }

    private TextView makeTagChip(String text, boolean active) {
        TextView tv = new TextView(this);
        tv.setText(text);
        tv.setTextSize(11f);
        tv.setTextColor(active ? Color.WHITE : 0xFFb0b0c0);
        tv.setBackgroundResource(active ? R.drawable.tag_chip_bg_active : R.drawable.tag_chip_bg);
        int padH = dpToPx(10);
        int padV = dpToPx(3);
        tv.setPadding(padH, padV, padH, padV);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        lp.setMargins(0, 0, dpToPx(6), 0);
        tv.setLayoutParams(lp);
        tv.setGravity(Gravity.CENTER);
        return tv;
    }

    private void refreshTagChips() {
        rebuildTagFilter();
    }

    private void applyTagFilter() {
        if (m_gamelist == null) return;
        if (m_activeFilterTag == null || m_activeFilterTag.isEmpty()) {
            // 显示全部
            ((BaseAdapter)m_listgameadaptor).notifyDataSetChanged();
            ((BaseAdapter)m_gridGame.getAdapter()).notifyDataSetChanged();
            return;
        }

        // 过滤 — 我们不用真的移除元素，而是让 Adapter 检查
        // 标记当前过滤标签，Adapter 的 getCount/getView 会检查
        // 但我们用的是 BaseAdapter 直接访问 m_gamelist，所以需要用过滤后的列表
        // 简单做法：直接 notifyDataSetChanged，在 getView 中判断可见性
        ((BaseAdapter)m_listgameadaptor).notifyDataSetChanged();
        ((BaseAdapter)m_gridGame.getAdapter()).notifyDataSetChanged();
    }

    private int dpToPx(int dp) {
        return (int) (dp * getResources().getDisplayMetrics().density + 0.5f);
    }

    /**
     * 刷新所有视图（列表和网格）
     */
    /** 刷新游戏库 + 历史 + 标签筛选栏 */
    private void refreshGameUI() {
        if (m_listgameadaptor != null) m_listgameadaptor.notifyDataSetChanged();
        if (m_gridGame != null && m_gridGame.getAdapter() != null) {
            ((BaseAdapter) m_gridGame.getAdapter()).notifyDataSetChanged();
        }
        if (m_historyAdapter != null) m_historyAdapter.notifyDataSetChanged();
        if (m_historyGridAdapter != null) m_historyGridAdapter.notifyDataSetChanged();
        rebuildTagFilter();
    }

    /**
     * 同 refreshGameUI，但不刷新标签栏（仅刷新列表视图）
     */
    private void notifyDataChanged() {
        if (m_listgameadaptor != null) m_listgameadaptor.notifyDataSetChanged();
        if (m_gridGame != null && m_gridGame.getAdapter() != null) {
            ((BaseAdapter) m_gridGame.getAdapter()).notifyDataSetChanged();
        }
        if (m_historyAdapter != null) m_historyAdapter.notifyDataSetChanged();
        if (m_historyGridAdapter != null) m_historyGridAdapter.notifyDataSetChanged();
    }

    /**
     * 更新详情弹窗中的参数显示（渲染器/启动文件变化后调用）
     * 直接替换 text_gameargs 中对应字段，修改后立即持久化
     */
    private void updateDetailArgsDisplay(GameInfo gameinfo, TextView text_startup,
                                          RadioGroup detail_renderer_group,
                                          EditText text_gameargs) {
        String cmd = text_gameargs.getText().toString();
        String newRender = (detail_renderer_group.getCheckedRadioButtonId() == R.id.detail_render_gl)
                ? "gl" : "software";
        String newStartup = text_startup.getText().toString();

        // 替换 --render=xxx
        String updated = cmd.replaceAll("-render=\\S+", "-render=" + newRender);
        if (updated.equals(cmd) && !cmd.contains("-render=")) {
            updated = cmd + " -render=" + newRender;
        }

        // 替换第一个 token（启动文件）
        String[] parts = updated.split(" ", 2);
        if (!parts[0].equals(newStartup)) {
            updated = newStartup + (parts.length > 1 ? " " + parts[1] : "");
        }

        text_gameargs.setText(updated);
        saveArgsConfig(gameinfo.m_path, updated);
    }

    // ========== 游戏详情弹窗 ==========

    @SuppressLint({"InflateParams", "SetTextI18n"})
    private void showGameDetail(GameInfo gameinfo, GamelistAdapter.ItemViewHolder cardHolder) {
        View view_gamedetail = LayoutInflater.from(MainActivity.this)
                .inflate(R.layout.layout_gamedetail, null);

        TextView text_gamename = view_gamedetail.findViewById(R.id.text_detail_gamename);
        text_gamename.setText(gameinfo.m_name);

        TextView text_gamepath = view_gamedetail.findViewById(R.id.text_detail_gamepath);
        text_gamepath.setText(gameinfo.getRealPath());

        ImageView image_detail_cover = view_gamedetail.findViewById(R.id.image_detail_cover);
        Bitmap cover = gameinfo.getCustomCover();
        if (cover == null) cover = gameinfo.getIcon();
        if (cover != null) {
            image_detail_cover.setImageBitmap(cover);
            image_detail_cover.setVisibility(View.VISIBLE);
        }

        ImageButton button_add_cover = view_gamedetail.findViewById(R.id.button_add_cover);
        button_add_cover.setOnClickListener(v -> {
            m_currentCoverGame = gameinfo;
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("image/*");
            startActivityForResult(intent, REQUEST_COVER_IMAGE);
        });

        TextView badge_read = view_gamedetail.findViewById(R.id.badge_read);
        TextView badge_write = view_gamedetail.findViewById(R.id.badge_write);
        TextView badge_exec = view_gamedetail.findViewById(R.id.badge_exec);

        // 加载单人配置（如果有），并合并全局配置显示
        m_currentDetailGame = gameinfo;
        JSONObject gameConfig = m_db != null ? m_db.getGameConfig(gameinfo.m_path) : new JSONObject();
        final boolean hadCustomArgs = gameConfig.has("args");

        int gamemod = gameinfo.checkValid(gameConfig.optString("startup_file", "data.xp3"));
        badge_read.setVisibility((gamemod & 4) != 0 ? View.VISIBLE : View.GONE);
        badge_write.setVisibility((gamemod & 2) != 0 ? View.VISIBLE : View.GONE);
        badge_exec.setVisibility((gamemod & 1) != 0 ? View.VISIBLE : View.GONE);

        // 获取所有详情弹窗中的控件引用
        EditText text_gameargs = view_gamedetail.findViewById(R.id.text_detail_gameargs);
        TextView text_startup = view_gamedetail.findViewById(R.id.text_detail_startup);
        RadioGroup detail_renderer_group = view_gamedetail.findViewById(R.id.detail_renderer_group);
        ImageButton btn_browse = view_gamedetail.findViewById(R.id.button_browse_startup);
        EditText text_tags = view_gamedetail.findViewById(R.id.text_detail_tags);

        // 初始化参数显示
        String initialMergedCmd = buildDisplayString(gameConfig);
        text_gameargs.setText(initialMergedCmd);
        boolean canEditArgs = m_gameargs != null && m_gameargs.optBoolean("alloweditargs", false);
        text_gameargs.setEnabled(canEditArgs);

        // 初始化启动文件（仅保留文件名，兼容旧版保存的全路径）
        String startupFile = gameConfig.optString("startup_file", "data.xp3");
        if (!startupFile.isEmpty()) {
            // 如果保存的是全路径，提取文件名
            int slashIdx = startupFile.lastIndexOf('/');
            if (slashIdx >= 0) startupFile = startupFile.substring(slashIdx + 1);
        }
        text_startup.setText(startupFile.isEmpty() ? "data.xp3" : startupFile);

        // 初始化渲染器
        String globalRender = m_gameargs != null ? m_gameargs.optString("render", "software") : "software";
        String gameRender = gameConfig.optString("render", globalRender);
        detail_renderer_group.check("gl".equals(gameRender) ? R.id.detail_render_gl : R.id.detail_render_software);

        // 初始化标签
        String tagsStr = gameConfig.optString("tags", "");
        text_tags.setText(tagsStr);
        text_tags.setOnEditorActionListener((v, actionId, event) -> {
            if (actionId == EditorInfo.IME_ACTION_DONE
                    || actionId == EditorInfo.IME_NULL
                    || (event != null && event.getKeyCode() == KeyEvent.KEYCODE_ENTER
                            && event.getAction() == KeyEvent.ACTION_UP)) {
                // 回车即保存标签
                String newTags = text_tags.getText().toString().trim();
                String oldTags = gameConfig.optString("tags", "");
                if (!newTags.equals(oldTags) && m_db != null) {
                    try {
                        JSONObject cfg = new JSONObject();
                        cfg.put("tags", newTags);
                        m_db.saveGameConfig(gameinfo.m_path, cfg);
                        refreshGameUI();
                        Toast.makeText(MainActivity.this, "标签已保存", Toast.LENGTH_SHORT).show();
                    } catch (JSONException ignored) {}
                }
                return true;
            }
            return false;
        });

        // --- 事件监听 ---

        // 渲染器切换 → 更新参数显示
        detail_renderer_group.setOnCheckedChangeListener((g, id) ->
                updateDetailArgsDisplay(gameinfo, text_startup,
                        detail_renderer_group, text_gameargs));

        // 启动文件浏览 → 更新参数显示
        btn_browse.setOnClickListener(v -> showStartupFilePicker(gameinfo, text_startup,
                () -> updateDetailArgsDisplay(gameinfo, text_startup,
                        detail_renderer_group, text_gameargs)));

        Button button_run = view_gamedetail.findViewById(R.id.button_detail_run);
        button_run.setOnClickListener(view -> {
            String currentCmd = text_gameargs.getText().toString();
            boolean argsEdited = !currentCmd.equals(initialMergedCmd);

            // 构建当前 UI 状态的覆盖配置（保存 + 运行）
            JSONObject saveCfg = new JSONObject();
            JSONObject launchCfg = new JSONObject();
            boolean hasOverride = false;
            try {
                String su = text_startup.getText().toString();
                int renderId = detail_renderer_group.getCheckedRadioButtonId();
                String renderVal = (renderId == R.id.detail_render_gl) ? "gl" : "software";

                // 启动文件
                if (!su.equals("data.xp3")) {
                    saveCfg.put("startup_file", su);
                    hasOverride = true;
                }
                // 渲染器
                if (!renderVal.equals(globalRender)) {
                    saveCfg.put("render", renderVal);
                    hasOverride = true;
                }
                // 标签
                String curTags = text_tags.getText().toString().trim();
                String prevTags = gameConfig.optString("tags", "");
                if (!curTags.equals(prevTags)) {
                    saveCfg.put("tags", curTags);
                    hasOverride = true;
                }
                // 参数字符串
                if (argsEdited) {
                    String[] cp = currentCmd.split(" ", 2);
                    String ft = cp[0];
                    String ra = cp.length > 1 ? cp[1] : "";
                    if (!ft.equals("data.xp3")) { saveCfg.put("startup_file", ft); hasOverride = true; }
                    if (!ra.isEmpty())          { saveCfg.put("args", ra);        hasOverride = true; }
                }

                // 持久化到数据库
                if (m_db != null && hasOverride) {
                    m_db.saveGameConfig(gameinfo.m_path, saveCfg);
                }

                // 构建启动参数（始终使用 UI 当前值）
                launchCfg.put("render", renderVal);
                if (!su.equals("data.xp3")) launchCfg.put("startup_file", su);
                if (argsEdited) {
                    String[] cp = currentCmd.split(" ", 2);
                    String ft = cp[0];
                    String ra = cp.length > 1 ? cp[1] : "";
                    if (!ft.equals("data.xp3")) launchCfg.put("startup_file", ft);
                    if (!ra.isEmpty())         launchCfg.put("args", ra);
                }
            } catch (JSONException e) {
                Log.e("## krkrsdl", "save config error: " + e.getMessage());
            }
            ArrayList<String> args = buildLaunchArgs(gameinfo.getGameDir(), launchCfg);
            startkrkrsdl(gamemod, gameinfo.getRealPath(), args, false);
            if (m_listgameadaptor != null) m_listgameadaptor.notifyGameLaunch(gameinfo);
        });

        Button button_config = view_gamedetail.findViewById(R.id.button_detail_config);
        button_config.setOnClickListener(view -> switchTab(2));

        PopupWindow window_config = new PopupWindow(MainActivity.this);
        window_config.setContentView(view_gamedetail);
        window_config.setFocusable(true);
        window_config.setAnimationStyle(android.R.style.Animation_Toast);

        // 计算弹窗最大高度（屏幕高度的 65%），超出时可滚动
        android.util.DisplayMetrics dm = new android.util.DisplayMetrics();
        getWindowManager().getDefaultDisplay().getMetrics(dm);
        int maxHeight = (int)(dm.heightPixels * 0.65);

        Configuration config = MainActivity.this.getResources().getConfiguration();
        if (config.orientation == Configuration.ORIENTATION_LANDSCAPE) {
            window_config.setWidth(LinearLayout.LayoutParams.WRAP_CONTENT);
            window_config.setHeight(maxHeight);
            window_config.showAtLocation(view_gamedetail, Gravity.RIGHT | Gravity.BOTTOM, 0, 0);
        } else {
            window_config.setWidth(LinearLayout.LayoutParams.MATCH_PARENT);
            window_config.setHeight(maxHeight);
            window_config.showAtLocation(view_gamedetail, Gravity.BOTTOM, 0, 0);
        }
    }

    // ========== 封面管理菜单 ==========

    private void showCoverMenu(GameInfo gameinfo) {
        String[] options = {"选择图片作为封面", "清除封面", "取消"};
        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        builder.setTitle("管理封面 - " + gameinfo.m_name);
        builder.setItems(options, (dialog, which) -> {
            switch (which) {
                case 0:
                    m_currentCoverGame = gameinfo;
                    Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                    intent.addCategory(Intent.CATEGORY_OPENABLE);
                    intent.setType("image/*");
                    startActivityForResult(intent, REQUEST_COVER_IMAGE);
                    break;
                case 1:
                    // 删除游戏目录下的 cover.png
                    String coverPath = gameinfo.m_path + "/cover.png";
                    File coverFile = new File(coverPath);
                    if (coverFile.exists()) coverFile.delete();
                    // 也尝试 icon.png 等
                    String iconPath = gameinfo.m_path + "/icon.png";
                    File iconFile = new File(iconPath);
                    if (iconFile.exists()) iconFile.delete();
                    // 清除缓存
                    gameinfo.m_autoCoverChecked = false;
                    gameinfo.m_autoCover = null;
                    if (m_db != null) {
                        m_db.saveCoverPath(gameinfo.m_path, null);
                    }
                    notifyDataChanged();
                    Toast.makeText(this, "封面已清除", Toast.LENGTH_SHORT).show();
                    break;
                case 2:
                    break;
            }
        });
        builder.show();
    }

    // ========== 全局配置读写 ==========

    private void updateGameConfig(boolean init) {
        RadioGroup group_renderer = findViewById(R.id.group_renderer);
        CheckBox button_scopedsavedir = findViewById(R.id.button_scopedsavedir);
        CheckBox button_alloweditargs = findViewById(R.id.button_alloweditargs);

        try {
            if (init) {
                String jsonstr = m_sharedpref.getString(SHAREDPREF_GAMECONFIG, null);
                if (jsonstr == null) m_gameargs = new JSONObject();
                else m_gameargs = new JSONObject(jsonstr);

                // 渲染器
                String render = m_gameargs.optString("render", "software");
                if ("gl".equals(render)) {
                    group_renderer.check(R.id.button_render_gl);
                } else {
                    group_renderer.check(R.id.button_render_software);
                }

                button_scopedsavedir.setChecked(m_gameargs.optBoolean("scopedsavedir", false));
                button_alloweditargs.setChecked(m_gameargs.optBoolean("alloweditargs", false));
            } else {
                if (m_gameargs == null) m_gameargs = new JSONObject();

                // 渲染器
                int renderId = group_renderer.getCheckedRadioButtonId();
                m_gameargs.put("render", (renderId == R.id.button_render_gl) ? "gl" : "software");

                m_gameargs.put("scopedsavedir", button_scopedsavedir.isChecked());
                m_gameargs.put("alloweditargs", button_alloweditargs.isChecked());

                m_sharedpref.edit().putString(SHAREDPREF_GAMECONFIG, m_gameargs.toString()).apply();
            }
        } catch (JSONException e) {
            Log.e("## krkrsdl_android", e.toString());
        }

        EditText text_gamedir = findViewById(R.id.text_gamedir);
        text_gamedir.setEnabled(button_alloweditargs.isChecked());
    }

    // ========== 权限管理 ==========

    public boolean checkStoragePermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            return Environment.isExternalStorageManager();
        } else {
            return ContextCompat.checkSelfPermission(
                    this,
                    Manifest.permission.WRITE_EXTERNAL_STORAGE
            ) == PackageManager.PERMISSION_GRANTED;
        }
    }

    public void requestStoragePermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            Intent intent = new Intent(
                    Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                    Uri.parse("package:" + getPackageName())
            );
            startActivity(intent);
        } else {
            ActivityCompat.requestPermissions(
                    this,
                    new String[]{Manifest.permission.WRITE_EXTERNAL_STORAGE},
                    100
            );
        }
    }
}