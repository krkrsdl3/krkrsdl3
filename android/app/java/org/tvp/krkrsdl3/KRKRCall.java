package org.tvp.krkrsdl3;

import android.app.Activity;
import android.view.MenuItem;
import android.view.Menu;
import android.widget.PopupMenu;
import android.view.View;

import org.libsdl.app.SDLActivity;

public class KRKRCall {
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

                popup.setOnMenuItemClickListener(new PopupMenu.OnMenuItemClickListener() {
                    @Override
                    public boolean onMenuItemClick(MenuItem menuItem) {
                        nativeOnMenuItemClick(menuItem.getItemId(), menuItem.getTitle().toString());
                        return true;
                    }
                });

                popup.show();
            }
        });
    }
}
