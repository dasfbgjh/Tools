#ifndef SYSTEM_TRAY_H
#define SYSTEM_TRAY_H

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "tray/tray.h"

#define TRAY_EXIT_CODE -1

class SystemTray {
private:
    struct TrayMenuContext {
        int id;
        std::string name;
        std::vector<struct tray_menu> subMenu;
        SystemTray *instance;
    };

    struct tray m_tray;
    std::vector<std::shared_ptr<TrayMenuContext>> m_trayMenus;
    std::vector<struct tray_menu> m_trayRoot;
    std::function<void( struct tray_menu *item, int id )> m_clickCallback;

public:
    using Menu = std::shared_ptr<struct tray_menu>;
    using SubMenu = std::vector<Menu>;

    SystemTray( std::function<void( struct tray_menu *item, int id )> clickCallback );
    ~SystemTray();

    void setIconData( const unsigned char *data, size_t size );
    void setIconPath( const std::string &path );

    void update( const SystemTray::SubMenu &menus );
    void update();
    int run();
    int poll();
    void exit();

    Menu createMenu( const std::string &name, int id, bool disabled, bool checked, const SubMenu &submenu = {} );
    Menu createMenu( const std::string &name, int id, const SubMenu &submenu = {} );
    Menu createSeparator( int id = -1 );

private:
    static void menuCallback( struct tray_menu *item );
};

#endif