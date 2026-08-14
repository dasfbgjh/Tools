#ifndef TRAY_H
#define TRAY_H

struct tray_menu;

struct tray {
    char *icon;
    void *icon_data;
    size_t icon_data_size;
    struct tray_menu *menu;
};

struct tray_menu {
    char *text;
    int disabled;
    int checked;

    void ( *cb )( struct tray_menu * );
    void *context;

    struct tray_menu *submenu;
};

static void tray_update( struct tray *tray );

#if defined( TRAY_APPINDICATOR )

#include <gtk/gtk.h>
#include <libappindicator/app-indicator.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#define TRAY_APPINDICATOR_ID "tray-id"

static AppIndicator *indicator = NULL;
static int loop_result = 0;
static char *temp_icon_path = NULL;

static void _tray_menu_cb( GtkMenuItem *item, gpointer data ) {
    (void)item;
    struct tray_menu *m = (struct tray_menu *)data;
    m->cb( m );
}

static GtkMenuShell *_tray_menu( struct tray_menu *m ) {
    GtkMenuShell *menu = (GtkMenuShell *)gtk_menu_new();
    for ( ; m != NULL && m->text != NULL; m++ ) {
        GtkWidget *item;
        if ( strcmp( m->text, "-" ) == 0 ) {
            item = gtk_separator_menu_item_new();
        } else {
            if ( m->submenu != NULL ) {
                item = gtk_menu_item_new_with_label( m->text );
                gtk_menu_item_set_submenu( GTK_MENU_ITEM( item ),
                                           GTK_WIDGET( _tray_menu( m->submenu ) ) );
            } else {
                item = gtk_check_menu_item_new_with_label( m->text );
                gtk_check_menu_item_set_active( GTK_CHECK_MENU_ITEM( item ), !!m->checked );
            }
            gtk_widget_set_sensitive( item, !m->disabled );
            if ( m->cb != NULL ) {
                g_signal_connect( item, "activate", G_CALLBACK( _tray_menu_cb ), m );
            }
        }
        gtk_widget_show( item );
        gtk_menu_shell_append( menu, item );
    }
    return menu;
}

static int tray_init( struct tray *tray ) {
    if ( gtk_init_check( 0, NULL ) == FALSE ) {
        return -1;
    }
    indicator = app_indicator_new( TRAY_APPINDICATOR_ID, tray->icon,
                                   APP_INDICATOR_CATEGORY_APPLICATION_STATUS );
    app_indicator_set_status( indicator, APP_INDICATOR_STATUS_ACTIVE );
    tray_update( tray );
    return 0;
}

static int tray_loop( int blocking ) {
    gtk_main_iteration_do( blocking );
    return loop_result;
}

static void tray_update( struct tray *tray ) {
    const char *icon_path = tray->icon;

    if ( tray->icon_data != NULL && tray->icon_data_size > 0 ) {
        GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
        GError *error = NULL;
        if ( gdk_pixbuf_loader_write( loader, tray->icon_data, tray->icon_data_size, &error ) ) {
            gdk_pixbuf_loader_close( loader, &error );
            GdkPixbuf *pixbuf = gdk_pixbuf_loader_get_pixbuf( loader );
            if ( pixbuf != NULL ) {
                if ( temp_icon_path != NULL ) {
                    g_free( temp_icon_path );
                }
                temp_icon_path = g_build_filename( g_get_tmp_dir(), "tray_icon.png", NULL );
                gdk_pixbuf_save( pixbuf, temp_icon_path, "png", &error, NULL );
                icon_path = temp_icon_path;
            }
        }
        g_object_unref( loader );
    }

    app_indicator_set_icon( indicator, icon_path );
    app_indicator_set_menu( indicator, GTK_MENU( _tray_menu( tray->menu ) ) );
}

static void tray_exit() {
    loop_result = -1;
}

#elif defined( TRAY_APPKIT )

#include <objc/objc-runtime.h>
#include <limits.h>

static id app;
static id pool;
static id statusBar;
static id statusItem;
static id statusBarButton;

static id _tray_menu( struct tray_menu *m ) {
    id menu = objc_msgSend( (id)objc_getClass( "NSMenu" ), sel_registerName( "new" ) );
    objc_msgSend( menu, sel_registerName( "autorelease" ) );
    objc_msgSend( menu, sel_registerName( "setAutoenablesItems:" ), false );

    for ( ; m != NULL && m->text != NULL; m++ ) {
        if ( strcmp( m->text, "-" ) == 0 ) {
            objc_msgSend( menu, sel_registerName( "addItem:" ),
                          objc_msgSend( (id)objc_getClass( "NSMenuItem" ), sel_registerName( "separatorItem" ) ) );
        } else {
            id menuItem = objc_msgSend( (id)objc_getClass( "NSMenuItem" ), sel_registerName( "alloc" ) );
            objc_msgSend( menuItem, sel_registerName( "autorelease" ) );
            objc_msgSend( menuItem, sel_registerName( "initWithTitle:action:keyEquivalent:" ),
                          objc_msgSend( (id)objc_getClass( "NSString" ), sel_registerName( "stringWithUTF8String:" ), m->text ),
                          sel_registerName( "menuCallback:" ),
                          objc_msgSend( (id)objc_getClass( "NSString" ), sel_registerName( "stringWithUTF8String:" ), "" ) );

            objc_msgSend( menuItem, sel_registerName( "setEnabled:" ), ( m->disabled ? false : true ) );
            objc_msgSend( menuItem, sel_registerName( "setState:" ), ( m->checked ? 1 : 0 ) );
            objc_msgSend( menuItem, sel_registerName( "setRepresentedObject:" ),
                          objc_msgSend( (id)objc_getClass( "NSValue" ), sel_registerName( "valueWithPointer:" ), m ) );

            objc_msgSend( menu, sel_registerName( "addItem:" ), menuItem );

            if ( m->submenu != NULL ) {
                objc_msgSend( menu, sel_registerName( "setSubmenu:forItem:" ), _tray_menu( m->submenu ), menuItem );
            }
        }
    }

    return menu;
}

static void menu_callback( id self, SEL cmd, id sender ) {
    struct tray_menu *m =
        (struct tray_menu *)objc_msgSend( objc_msgSend( sender, sel_registerName( "representedObject" ) ),
                                          sel_registerName( "pointerValue" ) );

    if ( m != NULL && m->cb != NULL ) {
        m->cb( m );
    }
}

static int tray_init( struct tray *tray ) {
    pool = objc_msgSend( (id)objc_getClass( "NSAutoreleasePool" ),
                         sel_registerName( "new" ) );

    objc_msgSend( (id)objc_getClass( "NSApplication" ),
                  sel_registerName( "sharedApplication" ) );

    Class trayDelegateClass = objc_allocateClassPair( objc_getClass( "NSObject" ), "Tray", 0 );
    class_addProtocol( trayDelegateClass, objc_getProtocol( "NSApplicationDelegate" ) );
    class_addMethod( trayDelegateClass, sel_registerName( "menuCallback:" ), (IMP)menu_callback, "v@:@" );
    objc_registerClassPair( trayDelegateClass );

    id trayDelegate = objc_msgSend( (id)trayDelegateClass,
                                    sel_registerName( "new" ) );

    app = objc_msgSend( (id)objc_getClass( "NSApplication" ),
                        sel_registerName( "sharedApplication" ) );

    objc_msgSend( app, sel_registerName( "setDelegate:" ), trayDelegate );

    statusBar = objc_msgSend( (id)objc_getClass( "NSStatusBar" ),
                              sel_registerName( "systemStatusBar" ) );

    statusItem = objc_msgSend( statusBar, sel_registerName( "statusItemWithLength:" ), -1.0 );

    objc_msgSend( statusItem, sel_registerName( "retain" ) );
    objc_msgSend( statusItem, sel_registerName( "setHighlightMode:" ), true );
    statusBarButton = objc_msgSend( statusItem, sel_registerName( "button" ) );
    tray_update( tray );
    objc_msgSend( app, sel_registerName( "activateIgnoringOtherApps:" ), true );
    return 0;
}

static int tray_loop( int blocking ) {
    id until = ( blocking ? objc_msgSend( (id)objc_getClass( "NSDate" ), sel_registerName( "distantFuture" ) ) : objc_msgSend( (id)objc_getClass( "NSDate" ), sel_registerName( "distantPast" ) ) );

    id event = objc_msgSend( app, sel_registerName( "nextEventMatchingMask:untilDate:inMode:dequeue:" ),
                             ULONG_MAX,
                             until,
                             objc_msgSend( (id)objc_getClass( "NSString" ),
                                           sel_registerName( "stringWithUTF8String:" ),
                                           "kCFRunLoopDefaultMode" ),
                             true );
    if ( event ) {
        objc_msgSend( app, sel_registerName( "sendEvent:" ), event );
    }
    return 0;
}

static void tray_update( struct tray *tray ) {
    id image = nil;

    if ( tray->icon_data != NULL && tray->icon_data_size > 0 ) {
        id data = objc_msgSend( (id)objc_getClass( "NSData" ),
                                sel_registerName( "dataWithBytes:length:" ),
                                tray->icon_data, tray->icon_data_size );
        image = objc_msgSend( (id)objc_getClass( "NSImage" ),
                              sel_registerName( "initWithData:" ), data );
        objc_msgSend( image, sel_registerName( "autorelease" ) );
    } else if ( tray->icon != NULL ) {
        image = objc_msgSend( (id)objc_getClass( "NSImage" ), sel_registerName( "imageNamed:" ),
                              objc_msgSend( (id)objc_getClass( "NSString" ), sel_registerName( "stringWithUTF8String:" ), tray->icon ) );
    }

    if ( image != nil ) {
        objc_msgSend( statusBarButton, sel_registerName( "setImage:" ), image );
    }

    objc_msgSend( statusItem, sel_registerName( "setMenu:" ), _tray_menu( tray->menu ) );
}

static void tray_exit() {
    objc_msgSend( app, sel_registerName( "terminate:" ), app );
}

#elif defined( TRAY_WINAPI )
#include <windows.h>
#include <shellapi.h>
#include <wincodec.h>
#include <objbase.h>
#include <stdlib.h>

#define WM_TRAY_CALLBACK_MESSAGE ( WM_USER + 1 )
#define WC_TRAY_CLASS_NAME "TRAY"
#define ID_TRAY_FIRST 1000

static WNDCLASSEX wc;
static NOTIFYICONDATA nid;
static HWND hwnd;
static HMENU hmenu = NULL;

static LRESULT CALLBACK _tray_wnd_proc( HWND hwnd, UINT msg, WPARAM wparam,
                                        LPARAM lparam ) {
    switch ( msg ) {
    case WM_CLOSE:
        DestroyWindow( hwnd );
        return 0;
    case WM_DESTROY:
        PostQuitMessage( 0 );
        return 0;
    case WM_TRAY_CALLBACK_MESSAGE:
        if ( lparam == WM_LBUTTONUP || lparam == WM_RBUTTONUP ) {
            POINT p;
            GetCursorPos( &p );
            SetForegroundWindow( hwnd );
            WORD cmd = TrackPopupMenu( hmenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                       p.x, p.y, 0, hwnd, NULL );
            SendMessage( hwnd, WM_COMMAND, cmd, 0 );
            return 0;
        }
        break;
    case WM_COMMAND:
        if ( wparam >= ID_TRAY_FIRST ) {
            MENUITEMINFO item = {
                .cbSize = sizeof( MENUITEMINFO ),
                .fMask = MIIM_ID | MIIM_DATA,
            };
            if ( GetMenuItemInfo( hmenu, wparam, FALSE, &item ) ) {
                struct tray_menu *menu = (struct tray_menu *)item.dwItemData;
                if ( menu != NULL && menu->cb != NULL ) {
                    menu->cb( menu );
                }
            }
            return 0;
        }
        break;
    }
    return DefWindowProc( hwnd, msg, wparam, lparam );
}

static HMENU _tray_menu( struct tray_menu *m, UINT *id ) {
    HMENU hmenu = CreatePopupMenu();
    for ( ; m != NULL && m->text != NULL; m++, ( *id )++ ) {
        if ( strcmp( m->text, "-" ) == 0 ) {
            InsertMenu( hmenu, *id, MF_SEPARATOR, TRUE, "" );
        } else {
            MENUITEMINFO item;
            memset( &item, 0, sizeof( item ) );
            item.cbSize = sizeof( MENUITEMINFO );
            item.fMask = MIIM_ID | MIIM_TYPE | MIIM_STATE | MIIM_DATA;
            item.fType = 0;
            item.fState = 0;
            if ( m->submenu != NULL ) {
                item.fMask = item.fMask | MIIM_SUBMENU;
                item.hSubMenu = _tray_menu( m->submenu, id );
            }
            if ( m->disabled ) {
                item.fState |= MFS_DISABLED;
            }
            if ( m->checked ) {
                item.fState |= MFS_CHECKED;
            }
            item.wID = *id;
            item.dwTypeData = m->text;
            item.dwItemData = (ULONG_PTR)m;

            InsertMenuItem( hmenu, *id, TRUE, &item );
        }
    }
    return hmenu;
}

static int tray_init( struct tray *tray ) {
    memset( &wc, 0, sizeof( wc ) );
    wc.cbSize = sizeof( WNDCLASSEX );
    wc.lpfnWndProc = _tray_wnd_proc;
    wc.hInstance = GetModuleHandle( NULL );
    wc.lpszClassName = WC_TRAY_CLASS_NAME;
    if ( !RegisterClassEx( &wc ) ) {
        return -1;
    }

    hwnd = CreateWindowEx( 0, WC_TRAY_CLASS_NAME, NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0 );
    if ( hwnd == NULL ) {
        return -1;
    }
    UpdateWindow( hwnd );

    memset( &nid, 0, sizeof( nid ) );
    nid.cbSize = sizeof( NOTIFYICONDATA );
    nid.hWnd = hwnd;
    nid.uID = 0;
    nid.uFlags = NIF_ICON | NIF_MESSAGE;
    nid.uCallbackMessage = WM_TRAY_CALLBACK_MESSAGE;
    Shell_NotifyIcon( NIM_ADD, &nid );

    tray_update( tray );
    return 0;
}

static int tray_loop( int blocking ) {
    MSG msg;
    if ( blocking ) {
        GetMessage( &msg, NULL, 0, 0 );
    } else {
        PeekMessage( &msg, NULL, 0, 0, PM_REMOVE );
    }
    if ( msg.message == WM_QUIT ) {
        return -1;
    }
    TranslateMessage( &msg );
    DispatchMessage( &msg );
    return 0;
}

static HICON _tray_icon_from_memory( void *data, size_t size ) {
    const BYTE *p = (const BYTE *)data;
    if ( size < 4 )
        return NULL;

    BOOL is_ico = ( p[0] == 0x00 && p[1] == 0x00 &&
                    p[2] == 0x01 && p[3] == 0x00 );

    if ( is_ico ) {
        if ( size < 6 )
            return NULL;
        WORD count = *(WORD *)( p + 4 );
        if ( count == 0 || size < 22 )
            return NULL;

        BYTE *dir_entry = (BYTE *)p + 6;
        DWORD offset = *(DWORD *)( dir_entry + 12 );
        DWORD img_size = *(DWORD *)( dir_entry + 8 );

        if ( offset + img_size > size )
            return NULL;

        BYTE *img_data = (BYTE *)p + offset;
        return CreateIconFromResourceEx( img_data, img_size,
                                         TRUE, 0x00030000, 0, 0,
                                         LR_DEFAULTCOLOR | LR_DEFAULTSIZE );
    }

    const BYTE png_sig[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
    if ( size >= 8 && memcmp( p, png_sig, 8 ) == 0 ) {
        IWICImagingFactory *factory = NULL;
        HRESULT hr = CoCreateInstance(
#ifdef __cplusplus
            CLSID_WICImagingFactory,
#else
            &CLSID_WICImagingFactory,
#endif
            NULL, CLSCTX_INPROC_SERVER,
#ifdef __cplusplus
            IID_IWICImagingFactory,
#else
            &IID_IWICImagingFactory,
#endif
            (void **)&factory );
        if ( FAILED( hr ) || !factory )
            return NULL;

        IWICStream *stream = NULL;
        hr = factory->CreateStream( &stream );
        if ( FAILED( hr ) || !stream ) {
            factory->Release();
            return NULL;
        }

        hr = stream->InitializeFromMemory( (BYTE *)data, (DWORD)size );
        if ( FAILED( hr ) ) {
            stream->Release();
            factory->Release();
            return NULL;
        }

        IWICBitmapDecoder *decoder = NULL;
        hr = factory->CreateDecoderFromStream( (IStream *)stream,
                                               NULL, (WICDecodeOptions)0, &decoder );
        if ( FAILED( hr ) || !decoder ) {
            stream->Release();
            factory->Release();
            return NULL;
        }

        IWICBitmapFrameDecode *frame = NULL;
        hr = decoder->GetFrame( 0, &frame );
        if ( FAILED( hr ) || !frame ) {
            decoder->Release();
            stream->Release();
            factory->Release();
            return NULL;
        }

        IWICFormatConverter *converter = NULL;
        hr = factory->CreateFormatConverter( &converter );
        if ( FAILED( hr ) || !converter ) {
            frame->Release();
            decoder->Release();
            stream->Release();
            factory->Release();
            return NULL;
        }

        hr = converter->Initialize( (IWICBitmapSource *)frame,
#ifdef __cplusplus
                                    GUID_WICPixelFormat32bppBGRA,
#else
                                    &GUID_WICPixelFormat32bppBGRA,
#endif
                                    WICBitmapDitherTypeNone, NULL, 0,
                                    WICBitmapPaletteTypeCustom );
        if ( FAILED( hr ) ) {
            converter->Release();
            frame->Release();
            decoder->Release();
            stream->Release();
            factory->Release();
            return NULL;
        }

        UINT w = 0, h = 0;
        converter->GetSize( &w, &h );
        if ( w == 0 || h == 0 ) {
            converter->Release();
            frame->Release();
            decoder->Release();
            stream->Release();
            factory->Release();
            return NULL;
        }

        BITMAPV5HEADER bi = { 0 };
        bi.bV5Size = sizeof( BITMAPV5HEADER );
        bi.bV5Width = w;
        bi.bV5Height = -( (LONG)h );
        bi.bV5Planes = 1;
        bi.bV5BitCount = 32;
        bi.bV5Compression = BI_BITFIELDS;
        bi.bV5RedMask = 0x00FF0000;
        bi.bV5GreenMask = 0x0000FF00;
        bi.bV5BlueMask = 0x000000FF;
        bi.bV5AlphaMask = 0xFF000000;

        HDC hdc = GetDC( NULL );
        void *bmp_data = NULL;
        HBITMAP hbmp = CreateDIBSection( hdc, (BITMAPINFO *)&bi,
                                         DIB_RGB_COLORS, &bmp_data, NULL, 0 );
        ReleaseDC( NULL, hdc );

        if ( !hbmp || !bmp_data ) {
            converter->Release();
            frame->Release();
            decoder->Release();
            stream->Release();
            factory->Release();
            return NULL;
        }

        hr = converter->CopyPixels( NULL, w * 4, w * h * 4, (BYTE *)bmp_data );
        if ( FAILED( hr ) ) {
            DeleteObject( hbmp );
            converter->Release();
            frame->Release();
            decoder->Release();
            stream->Release();
            factory->Release();
            return NULL;
        }

        HBITMAP hmask = CreateBitmap( w, h, 1, 1, NULL );
        ICONINFO ii = { TRUE, 0, 0, hbmp, hmask };
        HICON icon = CreateIconIndirect( &ii );

        DeleteObject( hbmp );
        DeleteObject( hmask );
        converter->Release();
        frame->Release();
        decoder->Release();
        stream->Release();
        factory->Release();

        return icon;
    }

    return NULL;
}

static void tray_update( struct tray *tray ) {
    HMENU prevmenu = hmenu;
    UINT id = ID_TRAY_FIRST;
    hmenu = _tray_menu( tray->menu, &id );
    SendMessage( hwnd, WM_INITMENUPOPUP, (WPARAM)hmenu, 0 );

    HICON icon = NULL;
    if ( tray->icon_data != NULL && tray->icon_data_size > 0 ) {
        icon = _tray_icon_from_memory( tray->icon_data, tray->icon_data_size );
    } else if ( tray->icon != NULL ) {
        ExtractIconEx( tray->icon, 0, NULL, &icon, 1 );
    }

    if ( nid.hIcon ) {
        DestroyIcon( nid.hIcon );
    }
    nid.hIcon = icon;
    Shell_NotifyIcon( NIM_MODIFY, &nid );

    if ( prevmenu != NULL ) {
        DestroyMenu( prevmenu );
    }
}

static void tray_exit() {
    Shell_NotifyIcon( NIM_DELETE, &nid );
    if ( nid.hIcon != 0 ) {
        DestroyIcon( nid.hIcon );
    }
    if ( hmenu != 0 ) {
        DestroyMenu( hmenu );
    }
    PostQuitMessage( 0 );
    UnregisterClass( WC_TRAY_CLASS_NAME, GetModuleHandle( NULL ) );
}
#else
static int tray_init( struct tray *tray ) {
    return -1;
}
static int tray_loop( int blocking ) {
    return -1;
}
static void tray_update( struct tray *tray ) {
}
static void tray_exit();
#endif

#endif /* TRAY_H */