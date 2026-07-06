package org.tvp.krkrsdl3;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.DialogInterface;
import android.view.MenuItem;
import android.view.Menu;
import android.view.inputmethod.InputMethodManager;
import android.widget.LinearLayout;
import android.widget.PopupMenu;
import android.widget.EditText;
import android.view.View;

import org.libsdl.app.SDLActivity;

public class KRKRCall {
    /**
     * 输入对话框
     */
    private static AlertDialog mInputDialog = null;
    private static String mInputResult = "";
    private static int mInputResultCode = -1;
    private static boolean mInputDone = false;
    public static void ShowInputBox(String title, String prompt, String text, String[] buttons) {
        final Activity act = SDLActivity.getContext();
        if (act == null) return;

        mInputDone = false;
        mInputResult = "";
        mInputResultCode = -1;

        act.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                AlertDialog.Builder builder = new AlertDialog.Builder(act);
                builder.setTitle(title);
                builder.setMessage(prompt);
                builder.setCancelable(false);

                final EditText editText = new EditText(act);
                editText.setText(text);
                editText.selectAll();
                LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.MATCH_PARENT,
                        LinearLayout.LayoutParams.WRAP_CONTENT
                );
                editText.setLayoutParams(lp);
                builder.setView(editText);

                if (buttons.length >= 1) {
                    builder.setPositiveButton(buttons[0], new DialogInterface.OnClickListener() {
                        @Override
                        public void onClick(DialogInterface dialog, int which) {
                            mInputResult = editText.getText().toString();
                            mInputResultCode = 0;
                            mInputDone = true;
                            mInputDialog = null;
                        }
                    });
                }
                if (buttons.length >= 2) {
                    builder.setNegativeButton(buttons[1], new DialogInterface.OnClickListener() {
                        @Override
                        public void onClick(DialogInterface dialog, int which) {
                            mInputResult = "";
                            mInputResultCode = -1;
                            mInputDone = true;
                            mInputDialog = null;
                        }
                    });
                }

                mInputDialog = builder.create();
                mInputDialog.show();

                editText.requestFocus();
                InputMethodManager imm = (InputMethodManager) act.getSystemService(Context.INPUT_METHOD_SERVICE);
                if (imm != null) {
                    imm.showSoftInput(editText, InputMethodManager.SHOW_IMPLICIT);
                }
            }
        });
    }
    // 阻塞等待对话框关闭
    public static int WaitInputResult() {
        while (!mInputDone) {
            try {
                Thread.sleep(50);
            } catch (InterruptedException e) {}
        }
        return mInputResultCode;
    }
    // 获取结果
    public static String GetInputResult() {
        return mInputResult;
    }

    /**
     * 菜单栏
     */
    public enum MenuItemType {
        NORMAL,
        CHECKBOX,
        SUBMENU,
        SEPARATOR
    }
    public static class MenuItemData {
        public int id;
        public String caption;
        public MenuItemType type;
        public boolean checked;
        public int order;
        public MenuItemData[] children;
        public MenuItemData(int id, String caption) {
            this.id = id;
            this.caption = caption;
            this.type = MenuItemType.NORMAL;
        }
        public MenuItemData asCheckbox(boolean checked) {
            this.type = MenuItemType.CHECKBOX;
            this.checked = checked;
            return this;
        }
        public MenuItemData asSeparator() {
            this.type = MenuItemType.SEPARATOR;
            return this;
        }
        public MenuItemData withChildren(MenuItemData... children) {
            this.type = MenuItemType.SUBMENU;
            this.children = children;
            return this;
        }
    }

    private static void buildMenu(Menu menu, MenuItemData[] items) {
        if (items == null) return;

        for (MenuItemData item : items) {
            if (item == null) continue;
            if (item.type == MenuItemType.SEPARATOR ||
                    (item.caption != null && item.caption.equals("-"))) {
                menu.add(0, item.id, item.order, "────────────────").setEnabled(false);
                continue;
            }
            if (item.children != null && item.children.length > 0) {
                android.view.SubMenu subMenu = menu.addSubMenu(0, item.id, item.order, item.caption);
                buildMenu(subMenu, item.children);
            } else {
                MenuItem menuItem = menu.add(0, item.id, item.order, item.caption);
                menuItem.setCheckable(item.type == MenuItemType.CHECKBOX);
                menuItem.setChecked(item.checked);
            }
        }
    }

    // 按钮事件回调
    static native void nativeOnMenuItemClick(int itemId, String itemCaption);
    static native void nativeOnMenuDismiss();
    static boolean s_menuItemClicked = false;

    // 显示
    public static void showDynamicMenu(final int x, final int y, final MenuItemData[] items) {
        final Activity act = SDLActivity.getContext();
        if (act == null) return;
        act.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                View anchor = new View(act);
                anchor.setX(x);
                anchor.setY(y);

                PopupMenu popup = new PopupMenu(act, anchor);
                buildMenu(popup.getMenu(), items);

                s_menuItemClicked = false;
                popup.setOnMenuItemClickListener(new PopupMenu.OnMenuItemClickListener() {
                    @Override
                    public boolean onMenuItemClick(MenuItem menuItem) {
                        s_menuItemClicked = true;
                        nativeOnMenuItemClick(menuItem.getItemId(), menuItem.getTitle().toString());
                        return true;
                    }
                });

                popup.setOnDismissListener(new PopupMenu.OnDismissListener() {
                    @Override
                    public void onDismiss(PopupMenu menu) {
                        if (!s_menuItemClicked) {
                            nativeOnMenuDismiss();
                        }
                        s_menuItemClicked = false;
                    }
                });

                popup.show();
            }
        });
    }
}
