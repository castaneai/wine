/*
 * Window related functions
 *
 * Copyright 1993, 1994 Alexandre Julliard
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <X11/Xatom.h>

#include "options.h"
#include "class.h"
#include "win.h"
#include "heap.h"
#include "user.h"
#include "dce.h"
#include "sysmetrics.h"
#include "cursoricon.h"
#include "heap.h"
#include "hook.h"
#include "menu.h"
#include "message.h"
#include "nonclient.h"
#include "string32.h"
#include "queue.h"
#include "winpos.h"
#include "color.h"
#include "shm_main_blk.h"
#include "dde_proc.h"
#include "clipboard.h"
#include "winproc.h"
#include "stddebug.h"
/* #define DEBUG_WIN  */ 
/* #define DEBUG_MENU */
#include "debug.h"

/* Desktop window */
static WND *pWndDesktop = NULL;

static HWND hwndSysModal = 0;

static WORD wDragWidth = 4;
static WORD wDragHeight= 3;

extern HCURSOR16 CURSORICON_IconToCursor(HICON16, BOOL32);
extern HWND32 CARET_GetHwnd(void);
extern BOOL32 WINPOS_ActivateOtherWindow(WND* pWnd);
extern void   WINPOS_CheckActive(HWND32);

/***********************************************************************
 *           WIN_FindWndPtr
 *
 * Return a pointer to the WND structure corresponding to a HWND.
 */
WND * WIN_FindWndPtr( HWND32 hwnd )
{
    WND * ptr;
    
    if (!hwnd || HIWORD(hwnd)) return NULL;
    ptr = (WND *) USER_HEAP_LIN_ADDR( hwnd );
    if (ptr->dwMagic != WND_MAGIC) return NULL;
    if (ptr->hwndSelf != hwnd)
    {
        fprintf( stderr, "Can't happen: hwnd %04x self pointer is %04x\n",
                 hwnd, ptr->hwndSelf );
        return NULL;
    }
    return ptr;
}


/***********************************************************************
 *           WIN_DumpWindow
 *
 * Dump the content of a window structure to stderr.
 */
void WIN_DumpWindow( HWND32 hwnd )
{
    WND *ptr;
    char className[80];
    int i;

    if (!(ptr = WIN_FindWndPtr( hwnd )))
    {
        fprintf( stderr, "%04x is not a window handle\n", hwnd );
        return;
    }

    if (!GetClassName32A( hwnd, className, sizeof(className ) ))
        strcpy( className, "#NULL#" );

    fprintf( stderr, "Window %04x (%p):\n", hwnd, ptr );
    fprintf( stderr,
             "next=%p  child=%p  parent=%p  owner=%p  class=%p '%s'\n"
             "inst=%04x  taskQ=%04x  updRgn=%04x  active=%04x dce=%p  idmenu=%04x\n"
             "style=%08lx  exstyle=%08lx  wndproc=%08x  text='%s'\n"
             "client=%d,%d-%d,%d  window=%d,%d-%d,%d  iconpos=%d,%d  maxpos=%d,%d\n"
             "sysmenu=%04x  flags=%04x  props=%p  vscroll=%p  hscroll=%p\n",
             ptr->next, ptr->child, ptr->parent, ptr->owner,
             ptr->class, className, ptr->hInstance, ptr->hmemTaskQ,
             ptr->hrgnUpdate, ptr->hwndLastActive, ptr->dce, ptr->wIDmenu,
             ptr->dwStyle, ptr->dwExStyle, (UINT32)ptr->winproc,
             ptr->text ? ptr->text : "",
             ptr->rectClient.left, ptr->rectClient.top, ptr->rectClient.right,
             ptr->rectClient.bottom, ptr->rectWindow.left, ptr->rectWindow.top,
             ptr->rectWindow.right, ptr->rectWindow.bottom, ptr->ptIconPos.x,
             ptr->ptIconPos.y, ptr->ptMaxPos.x, ptr->ptMaxPos.y, ptr->hSysMenu,
             ptr->flags, ptr->pProp, ptr->pVScroll, ptr->pHScroll );

    if (ptr->class->cbWndExtra)
    {
        fprintf( stderr, "extra bytes:" );
        for (i = 0; i < ptr->class->cbWndExtra; i++)
            fprintf( stderr, " %02x", *((BYTE*)ptr->wExtra+i) );
        fprintf( stderr, "\n" );
    }
    fprintf( stderr, "\n" );
}


/***********************************************************************
 *           WIN_WalkWindows
 *
 * Walk the windows tree and print each window on stderr.
 */
void WIN_WalkWindows( HWND32 hwnd, int indent )
{
    WND *ptr;
    char className[80];

    ptr = hwnd ? WIN_FindWndPtr( hwnd ) : pWndDesktop;
    if (!ptr)
    {
        fprintf( stderr, "*** Invalid window handle %04x\n", hwnd );
        return;
    }

    if (!indent)  /* first time around */
        fprintf( stderr, "%-16.16s %-8.8s %-6.6s %-17.17s %-8.8s %s\n",
                 "hwnd", " wndPtr", "queue", "Class Name", " Style", " WndProc");

    while (ptr)
    {
        fprintf(stderr, "%*s%04x%*s", indent, "", ptr->hwndSelf, 13-indent,"");

        GlobalGetAtomName16(ptr->class->atomName,className,sizeof(className));
        
        fprintf( stderr, "%08lx %-6.4x %-17.17s %08x %08x\n",
                 (DWORD)ptr, ptr->hmemTaskQ, className,
                 (UINT32)ptr->dwStyle, (UINT32)ptr->winproc );
        
        if (ptr->child) WIN_WalkWindows( ptr->child->hwndSelf, indent+1 );
        ptr = ptr->next;
    }
}


/***********************************************************************
 *           WIN_GetXWindow
 *
 * Return the X window associated to a window.
 */
Window WIN_GetXWindow( HWND32 hwnd )
{
    WND *wndPtr = WIN_FindWndPtr( hwnd );
    while (wndPtr && !wndPtr->window) wndPtr = wndPtr->parent;
    return wndPtr ? wndPtr->window : 0;
}


/***********************************************************************
 *           WIN_UnlinkWindow
 *
 * Remove a window from the siblings linked list.
 */
BOOL32 WIN_UnlinkWindow( HWND32 hwnd )
{    
    WND *wndPtr, **ppWnd;

    if (!(wndPtr = WIN_FindWndPtr( hwnd )) || !wndPtr->parent) return FALSE;
    ppWnd = &wndPtr->parent->child;
    while (*ppWnd != wndPtr) ppWnd = &(*ppWnd)->next;
    *ppWnd = wndPtr->next;
    return TRUE;
}


/***********************************************************************
 *           WIN_LinkWindow
 *
 * Insert a window into the siblings linked list.
 * The window is inserted after the specified window, which can also
 * be specified as HWND_TOP or HWND_BOTTOM.
 */
BOOL32 WIN_LinkWindow( HWND32 hwnd, HWND32 hwndInsertAfter )
{    
    WND *wndPtr, **ppWnd;

    if (!(wndPtr = WIN_FindWndPtr( hwnd )) || !wndPtr->parent) return FALSE;

    if ((hwndInsertAfter == HWND_TOP) || (hwndInsertAfter == HWND_BOTTOM))
    {
        ppWnd = &wndPtr->parent->child;  /* Point to first sibling hwnd */
	if (hwndInsertAfter == HWND_BOTTOM)  /* Find last sibling hwnd */
	    while (*ppWnd) ppWnd = &(*ppWnd)->next;
    }
    else  /* Normal case */
    {
	WND * afterPtr = WIN_FindWndPtr( hwndInsertAfter );
	if (!afterPtr) return FALSE;
        ppWnd = &afterPtr->next;
    }
    wndPtr->next = *ppWnd;
    *ppWnd = wndPtr;
    return TRUE;
}


/***********************************************************************
 *           WIN_FindWinToRepaint
 *
 * Find a window that needs repaint.
 */
HWND32 WIN_FindWinToRepaint( HWND32 hwnd, HQUEUE16 hQueue )
{
    HWND hwndRet;
    WND *pWnd = pWndDesktop;

    /* Note: the desktop window never gets WM_PAINT messages 
     * The real reason why is because Windows DesktopWndProc
     * does ValidateRgn inside WM_ERASEBKGND handler.
     */

    pWnd = hwnd ? WIN_FindWndPtr( hwnd ) : pWndDesktop->child;

    for ( ; pWnd ; pWnd = pWnd->next )
    {
        if (!(pWnd->dwStyle & WS_VISIBLE) || (pWnd->flags & WIN_NO_REDRAW))
        {
            dprintf_win( stddeb, "FindWinToRepaint: skipping window %04x\n",
                         pWnd->hwndSelf );
            continue;
        }
        if ((pWnd->hmemTaskQ == hQueue) &&
            (pWnd->hrgnUpdate || (pWnd->flags & WIN_INTERNAL_PAINT))) break;
        
        if (pWnd->child )
            if ((hwndRet = WIN_FindWinToRepaint( pWnd->child->hwndSelf, hQueue )) )
                return hwndRet;
    }
    
    if (!pWnd) return 0;
    
    hwndRet = pWnd->hwndSelf;

    /* look among siblings if we got a transparent window */
    while (pWnd && ((pWnd->dwExStyle & WS_EX_TRANSPARENT) ||
                    !(pWnd->hrgnUpdate || (pWnd->flags & WIN_INTERNAL_PAINT))))
    {
        pWnd = pWnd->next;
    }
    if (pWnd) hwndRet = pWnd->hwndSelf;
    dprintf_win(stddeb,"FindWinToRepaint: found %04x\n",hwndRet);
    return hwndRet;
}


/***********************************************************************
 *           WIN_SendParentNotify
 *
 * Send a WM_PARENTNOTIFY to all ancestors of the given window, unless
 * the window has the WS_EX_NOPARENTNOTIFY style.
 */
void WIN_SendParentNotify(HWND32 hwnd, WORD event, WORD idChild, LPARAM lValue)
{
    LPPOINT16 lppt = (LPPOINT16)&lValue;
    WND     *wndPtr = WIN_FindWndPtr( hwnd );
    BOOL     bMouse = ((event <= WM_MOUSELAST) && (event >= WM_MOUSEFIRST));

    /* if lValue contains cursor coordinates they have to be
     * mapped to the client area of parent window */

    if (bMouse) MapWindowPoints16( 0, hwnd, lppt, 1 );

    while (wndPtr)
    {
        if ((wndPtr->dwExStyle & WS_EX_NOPARENTNOTIFY) ||
	   !(wndPtr->dwStyle & WS_CHILD)) break;

        if (bMouse)
        {
	    lppt->x += wndPtr->rectClient.left;
	    lppt->y += wndPtr->rectClient.top;
        }

        wndPtr = wndPtr->parent;
	SendMessage32A( wndPtr->hwndSelf, WM_PARENTNOTIFY, 
                        MAKEWPARAM( event, idChild ), lValue );
    }
}


/***********************************************************************
 *           WIN_DestroyWindow
 *
 * Destroy storage associated to a window. "Internals" p.358
 */
static void WIN_DestroyWindow( WND* wndPtr )
{
    HWND hwnd = wndPtr->hwndSelf;
    WND* pWnd,*pNext;

    dprintf_win( stddeb, "WIN_DestroyWindow: %04x\n", wndPtr->hwndSelf );

#ifdef CONFIG_IPC
    if (main_block)
	DDE_DestroyWindow(wndPtr->hwndSelf);
#endif  /* CONFIG_IPC */
	
    /* free child windows */

    pNext = wndPtr->child;
    while( (pWnd = pNext) )
    {
        pNext = pWnd->next;
        WIN_DestroyWindow( pWnd );
    }

    SendMessage32A( wndPtr->hwndSelf, WM_NCDESTROY, 0, 0);

    /* FIXME: do we need to fake QS_MOUSEMOVE wakebit? */

    WINPOS_CheckActive( hwnd );
    if( hwnd == GetCapture32()) ReleaseCapture();

    /* free resources associated with the window */

    TIMER_RemoveWindowTimers( wndPtr->hwndSelf );
    PROPERTY_RemoveWindowProps( wndPtr );

    wndPtr->dwMagic = 0;  /* Mark it as invalid */
    wndPtr->hwndSelf = 0;

    if ((wndPtr->hrgnUpdate) || (wndPtr->flags & WIN_INTERNAL_PAINT))
    {
        if (wndPtr->hrgnUpdate) DeleteObject32( wndPtr->hrgnUpdate );
        QUEUE_DecPaintCount( wndPtr->hmemTaskQ );
    }

    /* toss stale messages from the queue */

    if( wndPtr->hmemTaskQ )
    {
      int           pos;
      MESSAGEQUEUE* msgQ = (MESSAGEQUEUE*) GlobalLock16(wndPtr->hmemTaskQ);

      while( (pos = QUEUE_FindMsg(msgQ, hwnd, 0, 0)) != -1 ) 
	      QUEUE_RemoveMsg(msgQ, pos);
      wndPtr->hmemTaskQ = 0;
    }

    if (!(wndPtr->dwStyle & WS_CHILD))
       if (wndPtr->wIDmenu) DestroyMenu( (HMENU16)wndPtr->wIDmenu );
    if (wndPtr->hSysMenu) DestroyMenu( wndPtr->hSysMenu );
    if (wndPtr->window) XDestroyWindow( display, wndPtr->window );
    if (wndPtr->class->style & CS_OWNDC) DCE_FreeDCE( wndPtr->dce );

    WINPROC_FreeProc( wndPtr->winproc );

    wndPtr->class->cWindows--;
    wndPtr->class = NULL;

    USER_HEAP_FREE( hwnd );
}

/***********************************************************************
 *           WIN_ResetQueueWindows
 */
void WIN_ResetQueueWindows( WND* wnd, HQUEUE16 hQueue, HQUEUE16 hNew )
{
    WND* next;

    while (wnd)
    {
        next = wnd->next;
        if (wnd->hmemTaskQ == hQueue)
	   if( hNew ) wnd->hmemTaskQ = hNew;
	   else DestroyWindow( wnd->hwndSelf );
        else WIN_ResetQueueWindows( wnd->child, hQueue, hNew );
        wnd = next;
    }
}

/***********************************************************************
 *           WIN_CreateDesktopWindow
 *
 * Create the desktop window.
 */
BOOL32 WIN_CreateDesktopWindow(void)
{
    CLASS *class;
    HWND hwndDesktop;

    dprintf_win(stddeb,"Creating desktop window\n");

    if (!(class = CLASS_FindClassByAtom( DESKTOP_CLASS_ATOM, 0 )))
	return FALSE;

    hwndDesktop = USER_HEAP_ALLOC( sizeof(WND)+class->cbWndExtra );
    if (!hwndDesktop) return FALSE;
    pWndDesktop = (WND *) USER_HEAP_LIN_ADDR( hwndDesktop );

    pWndDesktop->next              = NULL;
    pWndDesktop->child             = NULL;
    pWndDesktop->parent            = NULL;
    pWndDesktop->owner             = NULL;
    pWndDesktop->class             = class;
    pWndDesktop->winproc           = NULL;
    pWndDesktop->dwMagic           = WND_MAGIC;
    pWndDesktop->hwndSelf          = hwndDesktop;
    pWndDesktop->hInstance         = 0;
    pWndDesktop->rectWindow.left   = 0;
    pWndDesktop->rectWindow.top    = 0;
    pWndDesktop->rectWindow.right  = SYSMETRICS_CXSCREEN;
    pWndDesktop->rectWindow.bottom = SYSMETRICS_CYSCREEN;
    pWndDesktop->rectClient        = pWndDesktop->rectWindow;
    pWndDesktop->rectNormal        = pWndDesktop->rectWindow;
    pWndDesktop->ptIconPos.x       = -1;
    pWndDesktop->ptIconPos.y       = -1;
    pWndDesktop->ptMaxPos.x        = -1;
    pWndDesktop->ptMaxPos.y        = -1;
    pWndDesktop->text              = NULL;
    pWndDesktop->hmemTaskQ         = 0; /* Desktop does not belong to a task */
    pWndDesktop->hrgnUpdate        = 0;
    pWndDesktop->hwndLastActive    = hwndDesktop;
    pWndDesktop->dwStyle           = WS_VISIBLE | WS_CLIPCHILDREN |
                                     WS_CLIPSIBLINGS;
    pWndDesktop->dwExStyle         = 0;
    pWndDesktop->dce               = NULL;
    pWndDesktop->pVScroll          = NULL;
    pWndDesktop->pHScroll          = NULL;
    pWndDesktop->pProp             = NULL;
    pWndDesktop->wIDmenu           = 0;
    pWndDesktop->flags             = 0;
    pWndDesktop->window            = rootWindow;
    pWndDesktop->hSysMenu          = 0;
    pWndDesktop->userdata          = 0;

    WINPROC_SetProc( &pWndDesktop->winproc, (WNDPROC16)class->winproc, 0 );
    EVENT_RegisterWindow( pWndDesktop );
    SendMessage32A( hwndDesktop, WM_NCCREATE, 0, 0 );
    pWndDesktop->flags |= WIN_NEEDS_ERASEBKGND;
    return TRUE;
}


/***********************************************************************
 *           WIN_CreateWindowEx
 *
 * Implementation of CreateWindowEx().
 */
static HWND WIN_CreateWindowEx( CREATESTRUCT32A *cs, ATOM classAtom,
                                BOOL unicode )
{
    CLASS *classPtr;
    WND *wndPtr;
    HWND16 hwnd, hwndLinkAfter;
    POINT16 maxSize, maxPos, minTrack, maxTrack;
    LRESULT wmcreate;

    dprintf_win( stddeb, "CreateWindowEx: " );
    if (HIWORD(cs->lpszName)) dprintf_win( stddeb, "'%s' ", cs->lpszName );
    else dprintf_win( stddeb, "#%04x ", LOWORD(cs->lpszName) );
    if (HIWORD(cs->lpszClass)) dprintf_win( stddeb, "'%s' ", cs->lpszClass );
    else dprintf_win( stddeb, "#%04x ", LOWORD(cs->lpszClass) );

    dprintf_win( stddeb, "%08lx %08lx %d,%d %dx%d %04x %04x %04x %p\n",
		 cs->dwExStyle, cs->style, cs->x, cs->y, cs->cx, cs->cy,
		 cs->hwndParent, cs->hMenu, cs->hInstance, cs->lpCreateParams);

    /* Find the parent window */

    if (cs->hwndParent)
    {
	/* Make sure parent is valid */
        if (!IsWindow( cs->hwndParent ))
        {
            fprintf( stderr, "CreateWindowEx: bad parent %04x\n", cs->hwndParent );
	    return 0;
	}
    }
    else if (cs->style & WS_CHILD)
    {
        fprintf( stderr, "CreateWindowEx: no parent for child window\n" );
        return 0;  /* WS_CHILD needs a parent */
    }

    /* Find the window class */

    if (!(classPtr = CLASS_FindClassByAtom( classAtom,
                                            GetExePtr(cs->hInstance) )))
    {
        char buffer[256];
        GlobalGetAtomName32A( classAtom, buffer, sizeof(buffer) );
        fprintf( stderr, "CreateWindowEx: bad class '%s'\n", buffer );
        return 0;
    }

    /* Fix the coordinates */

    if (cs->x == CW_USEDEFAULT32) cs->x = cs->y = 0;
    if (cs->cx == CW_USEDEFAULT32)
    {
/*        if (!(cs->style & (WS_CHILD | WS_POPUP))) cs->cx = cs->cy = 0;
        else */
        {
            cs->cx = 600;
            cs->cy = 400;
        }
    }

    /* Create the window structure */

    if (!(hwnd = USER_HEAP_ALLOC( sizeof(*wndPtr) + classPtr->cbWndExtra
                                  - sizeof(wndPtr->wExtra) )))
    {
	dprintf_win( stddeb, "CreateWindowEx: out of memory\n" );
	return 0;
    }

    /* Fill the window structure */

    wndPtr = (WND *) USER_HEAP_LIN_ADDR( hwnd );
    wndPtr->next  = NULL;
    wndPtr->child = NULL;

    if (cs->style & WS_CHILD)
    {
        wndPtr->parent = WIN_FindWndPtr( cs->hwndParent );
        wndPtr->owner  = NULL;
    }
    else
    {
        wndPtr->parent = pWndDesktop;
        if (!cs->hwndParent || (cs->hwndParent == pWndDesktop->hwndSelf))
            wndPtr->owner = NULL;
        else
            wndPtr->owner = WIN_FindWndPtr(WIN_GetTopParent(cs->hwndParent));
    }

    wndPtr->window         = 0;
    wndPtr->class          = classPtr;
    wndPtr->winproc        = NULL;
    wndPtr->dwMagic        = WND_MAGIC;
    wndPtr->hwndSelf       = hwnd;
    wndPtr->hInstance      = cs->hInstance;
    wndPtr->ptIconPos.x    = -1;
    wndPtr->ptIconPos.y    = -1;
    wndPtr->ptMaxPos.x     = -1;
    wndPtr->ptMaxPos.y     = -1;
    wndPtr->text           = NULL;
    wndPtr->hmemTaskQ      = GetTaskQueue(0);
    wndPtr->hrgnUpdate     = 0;
    wndPtr->hwndLastActive = hwnd;
    wndPtr->dwStyle        = cs->style & ~WS_VISIBLE;
    wndPtr->dwExStyle      = cs->dwExStyle;
    wndPtr->wIDmenu        = 0;
    wndPtr->flags          = 0;
    wndPtr->pVScroll       = NULL;
    wndPtr->pHScroll       = NULL;
    wndPtr->pProp          = NULL;
    wndPtr->hSysMenu       = MENU_GetDefSysMenu();
    wndPtr->userdata       = 0;

    if (classPtr->cbWndExtra) memset( wndPtr->wExtra, 0, classPtr->cbWndExtra);

    /* Call the WH_CBT hook */

    hwndLinkAfter = (cs->style & WS_CHILD) ? HWND_BOTTOM : HWND_TOP;

    if (HOOK_IsHooked( WH_CBT ))
    {
	CBT_CREATEWND32A cbtc;

	cbtc.lpcs = cs;
	cbtc.hwndInsertAfter = hwndLinkAfter;
	wmcreate = !HOOK_CallHooks32A( WH_CBT, HCBT_CREATEWND, hwnd,
				       (LPARAM)&cbtc );
	if (!wmcreate)
	{
	    dprintf_win(stddeb, "CreateWindowEx: CBT-hook returned 0\n");
	    USER_HEAP_FREE( hwnd );
	    return 0;
	}
    }

    /* Set the window procedure */

    classPtr->cWindows++;
    WINPROC_SetProc( &wndPtr->winproc, (WNDPROC16)classPtr->winproc, 0 );

    /* Correct the window style */

    if (!(cs->style & (WS_POPUP | WS_CHILD)))  /* Overlapped window */
    {
        wndPtr->dwStyle |= WS_CAPTION | WS_CLIPSIBLINGS;
        wndPtr->flags |= WIN_NEED_SIZE;
    }
    if (cs->dwExStyle & WS_EX_DLGMODALFRAME) wndPtr->dwStyle &= ~WS_THICKFRAME;

    /* Get class or window DC if needed */

    if (classPtr->style & CS_OWNDC) wndPtr->dce = DCE_AllocDCE(hwnd,DCE_WINDOW_DC);
    else if (classPtr->style & CS_CLASSDC) wndPtr->dce = classPtr->dce;
    else wndPtr->dce = NULL;

    /* Insert the window in the linked list */

    WIN_LinkWindow( hwnd, hwndLinkAfter );

    /* Send the WM_GETMINMAXINFO message and fix the size if needed */

    if ((cs->style & WS_THICKFRAME) || !(cs->style & (WS_POPUP | WS_CHILD)))
    {
        NC_GetMinMaxInfo( wndPtr, &maxSize, &maxPos, &minTrack, &maxTrack );
        if (maxSize.x < cs->cx) cs->cx = maxSize.x;
        if (maxSize.y < cs->cy) cs->cy = maxSize.y;
        if (cs->cx < minTrack.x ) cs->cx = minTrack.x;
        if (cs->cy < minTrack.y ) cs->cy = minTrack.y;
    }
    if (cs->cx <= 0) cs->cx = 1;
    if (cs->cy <= 0) cs->cy = 1;

    wndPtr->rectWindow.left   = cs->x;
    wndPtr->rectWindow.top    = cs->y;
    wndPtr->rectWindow.right  = cs->x + cs->cx;
    wndPtr->rectWindow.bottom = cs->y + cs->cy;
    wndPtr->rectClient        = wndPtr->rectWindow;
    wndPtr->rectNormal        = wndPtr->rectWindow;

    /* Create the X window (only for top-level windows, and then only */
    /* when there's no desktop window) */

    if (!(cs->style & WS_CHILD) && (rootWindow == DefaultRootWindow(display)))
    {
        XSetWindowAttributes win_attr;

	if (Options.managed && ((cs->style & (WS_DLGFRAME | WS_THICKFRAME)) ||
            (cs->dwExStyle & WS_EX_DLGMODALFRAME)))
        {
	    win_attr.event_mask = ExposureMask | KeyPressMask |
	                          KeyReleaseMask | PointerMotionMask |
	                          ButtonPressMask | ButtonReleaseMask |
	                          FocusChangeMask | StructureNotifyMask;
	    win_attr.override_redirect = FALSE;
            wndPtr->flags |= WIN_MANAGED;
	}
	else
        {
	    win_attr.event_mask = ExposureMask | KeyPressMask |
	                          KeyReleaseMask | PointerMotionMask |
	                          ButtonPressMask | ButtonReleaseMask |
	                          FocusChangeMask | StructureNotifyMask;
            win_attr.override_redirect = TRUE;
	}
        win_attr.colormap      = COLOR_GetColormap();
        win_attr.backing_store = Options.backingstore ? WhenMapped : NotUseful;
        win_attr.save_under    = ((classPtr->style & CS_SAVEBITS) != 0);
        win_attr.cursor        = CURSORICON_XCursor;
        wndPtr->window = XCreateWindow( display, rootWindow, cs->x, cs->y,
                                        cs->cx, cs->cy, 0, CopyFromParent,
                                        InputOutput, CopyFromParent,
                                        CWEventMask | CWOverrideRedirect |
                                        CWColormap | CWCursor | CWSaveUnder |
                                        CWBackingStore, &win_attr );

        if ((wndPtr->flags & WIN_MANAGED) &&
            (cs->dwExStyle & WS_EX_DLGMODALFRAME))
        {
            XSizeHints* size_hints = XAllocSizeHints();

            if (size_hints)
            {
                size_hints->min_width = size_hints->max_width = cs->cx;
                size_hints->min_height = size_hints->max_height = cs->cy;
                size_hints->flags = (PSize | PMinSize | PMaxSize);
                XSetWMSizeHints( display, wndPtr->window, size_hints,
                                 XA_WM_NORMAL_HINTS );
                XFree(size_hints);
            }
        }

	if (cs->hwndParent)  /* Get window owner */
	{
            Window win = WIN_GetXWindow( cs->hwndParent );
            if (win) XSetTransientForHint( display, wndPtr->window, win );
	}
        EVENT_RegisterWindow( wndPtr );
    }

    /* Set the window menu */

    if ((cs->style & WS_CAPTION) && !(cs->style & WS_CHILD))
    {
        if (cs->hMenu) SetMenu(hwnd, cs->hMenu);
        else
        {
#if 0  /* FIXME: should check if classPtr->menuNameW can be used as is */
            if (classPtr->menuNameA)
                cs->hMenu = HIWORD(classPtr->menuNameA) ?
                       LoadMenu(cs->hInstance,SEGPTR_GET(classPtr->menuNameA)):
                       LoadMenu(cs->hInstance,(SEGPTR)classPtr->menuNameA);
#else
            SEGPTR menuName = (SEGPTR)GetClassLong16( hwnd, GCL_MENUNAME );
            if (menuName) cs->hMenu = LoadMenu16( cs->hInstance, menuName );
#endif
        }
        if (cs->hMenu) SetMenu( hwnd, cs->hMenu );
    }
    else wndPtr->wIDmenu = (UINT)cs->hMenu;

    /* Send the WM_CREATE message 
     * Perhaps we shouldn't allow width/height changes as well. 
     * See p327 in "Internals". 
     */

    maxPos.x = wndPtr->rectWindow.left; maxPos.y = wndPtr->rectWindow.top;
    if (unicode)
    {
        if (!SendMessage32W( hwnd, WM_NCCREATE, 0, (LPARAM)cs)) wmcreate = -1;
        else
        {
            WINPOS_SendNCCalcSize( hwnd, FALSE, &wndPtr->rectWindow,
                                   NULL, NULL, 0, &wndPtr->rectClient );
	    OffsetRect16(&wndPtr->rectWindow, maxPos.x - wndPtr->rectWindow.left,
					    maxPos.y - wndPtr->rectWindow.top);
            wmcreate = SendMessage32W( hwnd, WM_CREATE, 0, (LPARAM)cs );
        }
    }
    else
    {
        if (!SendMessage32A( hwnd, WM_NCCREATE, 0, (LPARAM)cs)) wmcreate = -1;
        else
        {
            WINPOS_SendNCCalcSize( hwnd, FALSE, &wndPtr->rectWindow,
                                   NULL, NULL, 0, &wndPtr->rectClient );
            OffsetRect16(&wndPtr->rectWindow, maxPos.x - wndPtr->rectWindow.left,
                                            maxPos.y - wndPtr->rectWindow.top);
            wmcreate = SendMessage32A( hwnd, WM_CREATE, 0, (LPARAM)cs );
        }
    }
    
    if (wmcreate == -1)
    {
	  /* Abort window creation */
	dprintf_win(stddeb,"CreateWindowEx: wmcreate==-1, aborting\n");
        WIN_UnlinkWindow( hwnd );
        WIN_DestroyWindow( wndPtr );
	return 0;
    }

    /* Send the size messages */

    if (!(wndPtr->flags & WIN_NEED_SIZE))
    {
	/* send it anyway */
	SendMessage16( hwnd, WM_SIZE, SIZE_RESTORED,
                   MAKELONG(wndPtr->rectClient.right-wndPtr->rectClient.left,
                            wndPtr->rectClient.bottom-wndPtr->rectClient.top));
        SendMessage16( hwnd, WM_MOVE, 0, MAKELONG( wndPtr->rectClient.left,
                                                   wndPtr->rectClient.top ));
    } 

    WIN_SendParentNotify( hwnd, WM_CREATE, wndPtr->wIDmenu, (LPARAM)hwnd );
    if (!IsWindow(hwnd)) return 0;

    /* Show the window, maximizing or minimizing if needed */

    if (wndPtr->dwStyle & WS_MINIMIZE)
    {
	/* MinMaximize(hwnd, SW_SHOWMINNOACTIVE, 1) in "Internals" */

        wndPtr->dwStyle &= ~WS_MAXIMIZE;
        WINPOS_FindIconPos( hwnd );
        SetWindowPos( hwnd, 0, wndPtr->ptIconPos.x, wndPtr->ptIconPos.y,
                      SYSMETRICS_CXICON, SYSMETRICS_CYICON, 
                      SWP_FRAMECHANGED | ((GetActiveWindow())? SWP_NOACTIVATE : 0)  );
    }
    else if (wndPtr->dwStyle & WS_MAXIMIZE)
    {
	/* MinMaximize(hwnd, SW_SHOWMAXIMIZED, 1) */

        NC_GetMinMaxInfo( wndPtr, &maxSize, &maxPos, &minTrack, &maxTrack );
        SetWindowPos( hwnd, 0, maxPos.x, maxPos.y, maxSize.x, maxSize.y,
            ((GetActiveWindow())? SWP_NOACTIVATE : 0) | SWP_FRAMECHANGED );
    }
    
    if (cs->style & WS_VISIBLE) ShowWindow( hwnd, SW_SHOW );

    /* Call WH_SHELL hook */

    if (!(wndPtr->dwStyle & WS_CHILD) && !wndPtr->owner)
        HOOK_CallHooks16( WH_SHELL, HSHELL_WINDOWCREATED, hwnd, 0 );

    dprintf_win(stddeb, "CreateWindowEx: returning %04x\n", hwnd);
    return hwnd;
}


/***********************************************************************
 *           CreateWindow16   (USER.41)
 */
HWND16 CreateWindow16( LPCSTR className, LPCSTR windowName,
                       DWORD style, INT16 x, INT16 y, INT16 width,
                       INT16 height, HWND16 parent, HMENU16 menu,
                       HINSTANCE16 instance, LPVOID data ) 
{
    return CreateWindowEx16( 0, className, windowName, style,
			   x, y, width, height, parent, menu, instance, data );
}


/***********************************************************************
 *           CreateWindowEx16   (USER.452)
 */
HWND16 CreateWindowEx16( DWORD exStyle, LPCSTR className, LPCSTR windowName,
                         DWORD style, INT16 x, INT16 y, INT16 width,
                         INT16 height, HWND16 parent, HMENU16 menu,
                         HINSTANCE16 instance, LPVOID data ) 
{
    ATOM classAtom;
    CREATESTRUCT32A cs;

    /* Find the class atom */

    if (!(classAtom = GlobalFindAtom32A( className )))
    {
        fprintf( stderr, "CreateWindowEx16: bad class name " );
        if (!HIWORD(className)) fprintf( stderr, "%04x\n", LOWORD(className) );
        else fprintf( stderr, "'%s'\n", className );
        return 0;
    }

    /* Fix the coordinates */

    cs.x  = (x == CW_USEDEFAULT16) ? CW_USEDEFAULT32 : (INT32)x;
    cs.y  = (y == CW_USEDEFAULT16) ? CW_USEDEFAULT32 : (INT32)y;
    cs.cx = (width == CW_USEDEFAULT16) ? CW_USEDEFAULT32 : (INT32)width;
    cs.cy = (height == CW_USEDEFAULT16) ? CW_USEDEFAULT32 : (INT32)height;

    /* Create the window */

    cs.lpCreateParams = data;
    cs.hInstance      = (HINSTANCE32)instance;
    cs.hMenu          = (HMENU32)menu;
    cs.hwndParent     = (HWND32)parent;
    cs.style          = style;
    cs.lpszName       = windowName;
    cs.lpszClass      = className;
    cs.dwExStyle      = exStyle;
    return WIN_CreateWindowEx( &cs, classAtom, FALSE );
}


/***********************************************************************
 *           CreateWindowEx32A   (USER32.82)
 */
HWND32 CreateWindowEx32A( DWORD exStyle, LPCSTR className, LPCSTR windowName,
                          DWORD style, INT32 x, INT32 y, INT32 width,
                          INT32 height, HWND32 parent, HMENU32 menu,
                          HINSTANCE32 instance, LPVOID data )
{
    ATOM classAtom;
    CREATESTRUCT32A cs;

    /* Find the class atom */

    if (!(classAtom = GlobalFindAtom32A( className )))
    {
        fprintf( stderr, "CreateWindowEx32A: bad class name " );
        if (!HIWORD(className)) fprintf( stderr, "%04x\n", LOWORD(className) );
        else fprintf( stderr, "'%s'\n", className );
        return 0;
    }

    /* Create the window */

    cs.lpCreateParams = data;
    cs.hInstance      = instance;
    cs.hMenu          = menu;
    cs.hwndParent     = parent;
    cs.x              = x;
    cs.y              = y;
    cs.cx             = width;
    cs.cy             = height;
    cs.style          = style;
    cs.lpszName       = windowName;
    cs.lpszClass      = className;
    cs.dwExStyle      = exStyle;
    return WIN_CreateWindowEx( &cs, classAtom, FALSE );
}


/***********************************************************************
 *           CreateWindowEx32W   (USER32.83)
 */
HWND32 CreateWindowEx32W( DWORD exStyle, LPCWSTR className, LPCWSTR windowName,
                          DWORD style, INT32 x, INT32 y, INT32 width,
                          INT32 height, HWND32 parent, HMENU32 menu,
                          HINSTANCE32 instance, LPVOID data )
{
    ATOM classAtom;
    CREATESTRUCT32W cs;

    /* Find the class atom */

    if (!(classAtom = GlobalFindAtom32W( className )))
    {
    	if (HIWORD(className))
        {
            LPSTR cn = STRING32_DupUniToAnsi(className);
            fprintf( stderr, "CreateWindowEx32W: bad class name '%s'\n",cn);
            free(cn);
	}
        else
            fprintf( stderr, "CreateWindowEx32W: bad class name %p\n", className );
        return 0;
    }

    /* Create the window */

    cs.lpCreateParams = data;
    cs.hInstance      = instance;
    cs.hMenu          = menu;
    cs.hwndParent     = parent;
    cs.x              = x;
    cs.y              = y;
    cs.cx             = width;
    cs.cy             = height;
    cs.style          = style;
    cs.lpszName       = windowName;
    cs.lpszClass      = className;
    cs.dwExStyle      = exStyle;
    /* Note: we rely on the fact that CREATESTRUCT32A and */
    /* CREATESTRUCT32W have the same layout. */
    return WIN_CreateWindowEx( (CREATESTRUCT32A *)&cs, classAtom, TRUE );
}


/***********************************************************************
 *           WIN_CheckFocus
 */
static void WIN_CheckFocus( WND* pWnd )
{
  if( GetFocus16() == pWnd->hwndSelf )
      SetFocus16( (pWnd->dwStyle & WS_CHILD) ? pWnd->parent->hwndSelf : 0 ); 
}

/***********************************************************************
 *           WIN_SendDestroyMsg
 */
static void WIN_SendDestroyMsg( WND* pWnd )
{
  WND*	pChild;

  WIN_CheckFocus(pWnd);

  if( CARET_GetHwnd() == pWnd->hwndSelf ) DestroyCaret();
  if( !pWnd->window ) CLIPBOARD_DisOwn( pWnd ); 
  
  SendMessage32A( pWnd->hwndSelf, WM_DESTROY, 0, 0);

  if( !IsWindow(pWnd->hwndSelf) )
  {
    dprintf_win(stddeb,"\tdestroyed itself while in WM_DESTROY!\n");
    return;
  }

  pChild = pWnd->child;
  while( pChild )
  { 
    WIN_SendDestroyMsg( pChild );
    pChild = pChild->next;
  }
  WIN_CheckFocus(pWnd);
}


/***********************************************************************
 *           DestroyWindow   (USER.53)
 */
BOOL DestroyWindow( HWND hwnd )
{
    WND * wndPtr;

    dprintf_win(stddeb, "DestroyWindow(%04x)\n", hwnd);
    
      /* Initialization */

    if (!(wndPtr = WIN_FindWndPtr( hwnd ))) return FALSE;
    if (wndPtr == pWndDesktop) return FALSE; /* Can't destroy desktop */

      /* Call hooks */

    if( HOOK_CallHooks16( WH_CBT, HCBT_DESTROYWND, hwnd, 0L) )
        return FALSE;

    if (!(wndPtr->dwStyle & WS_CHILD) && !wndPtr->owner)
    {
        HOOK_CallHooks16( WH_SHELL, HSHELL_WINDOWDESTROYED, hwnd, 0L );
        /* FIXME: clean up palette - see "Internals" p.352 */
    }

    if( !QUEUE_IsDoomedQueue(wndPtr->hmemTaskQ) )
	 WIN_SendParentNotify( hwnd, WM_DESTROY, wndPtr->wIDmenu, (LPARAM)hwnd );
    if( !IsWindow(hwnd) ) return TRUE;

    if( wndPtr->window ) CLIPBOARD_DisOwn( wndPtr ); /* before window is unmapped */

      /* Hide the window */

    if (wndPtr->dwStyle & WS_VISIBLE)
    {
        SetWindowPos( hwnd, 0, 0, 0, 0, 0, SWP_HIDEWINDOW |
		      SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE |
		    ((QUEUE_IsDoomedQueue(wndPtr->hmemTaskQ))?SWP_DEFERERASE:0) );
	if( !IsWindow(hwnd) ) return TRUE;
    }

      /* Recursively destroy owned windows */

    if( !(wndPtr->dwStyle & WS_CHILD) )
    {
      for (;;)
      {
        WND *siblingPtr = wndPtr->parent->child;  /* First sibling */
        while (siblingPtr)
        {
            if (siblingPtr->owner == wndPtr)
               if (siblingPtr->hmemTaskQ == wndPtr->hmemTaskQ)
                   break;
               else 
                   siblingPtr->owner = NULL;
            siblingPtr = siblingPtr->next;
        }
        if (siblingPtr) DestroyWindow( siblingPtr->hwndSelf );
        else break;
      }

      WINPOS_ActivateOtherWindow(wndPtr);

      if( wndPtr->owner &&
	  wndPtr->owner->hwndLastActive == wndPtr->hwndSelf )
	  wndPtr->owner->hwndLastActive = wndPtr->owner->hwndSelf;
    }

      /* Send destroy messages */

    WIN_SendDestroyMsg( wndPtr );
    if( !IsWindow(hwnd) ) return TRUE;

      /* Unlink now so we won't bother with the children later on */

    if( wndPtr->parent ) WIN_UnlinkWindow(hwnd);

      /* Destroy the window storage */

    WIN_DestroyWindow( wndPtr );
    return TRUE;
}


/***********************************************************************
 *           CloseWindow   (USER.43)
 */
BOOL CloseWindow(HWND hWnd)
{
    WND * wndPtr = WIN_FindWndPtr(hWnd);
    if (!wndPtr || (wndPtr->dwStyle & WS_CHILD)) return TRUE;
    ShowWindow(hWnd, SW_MINIMIZE);
    return TRUE;
}

 
/***********************************************************************
 *           OpenIcon   (USER.44)
 */
BOOL OpenIcon(HWND hWnd)
{
    if (!IsIconic(hWnd)) return FALSE;
    ShowWindow(hWnd, SW_SHOWNORMAL);
    return(TRUE);
}


/***********************************************************************
 *           WIN_FindWindow
 *
 * Implementation of FindWindow() and FindWindowEx().
 */
static HWND WIN_FindWindow( HWND parent, HWND child, ATOM className,
                            LPCSTR title )
{
    WND *pWnd;
    CLASS *pClass = NULL;

    if (child)
    {
        if (!(pWnd = WIN_FindWndPtr( child ))) return 0;
        if (parent)
        {
            if (!pWnd->parent || (pWnd->parent->hwndSelf != parent)) return 0;
        }
        else if (pWnd->parent != pWndDesktop) return 0;
        pWnd = pWnd->next;
    }
    else
    {
        if (!(pWnd = parent ? WIN_FindWndPtr(parent) : pWndDesktop)) return 0;
        pWnd = pWnd->child;
    }
    if (!pWnd) return 0;

    /* For a child window, all siblings will have the same hInstance, */
    /* so we can look for the class once and for all. */

    if (className && (pWnd->dwStyle & WS_CHILD))
    {
        if (!(pClass = CLASS_FindClassByAtom( className, pWnd->hInstance )))
            return 0;
    }

    for ( ; pWnd; pWnd = pWnd->next)
    {
        if (className && !(pWnd->dwStyle & WS_CHILD))
        {
            if (!(pClass = CLASS_FindClassByAtom( className, pWnd->hInstance)))
                continue;  /* Skip this window */
        }
        if (pClass && (pWnd->class != pClass))
            continue;  /* Not the right class */

        /* Now check the title */

        if (!title) return pWnd->hwndSelf;
        if (pWnd->text && !strcmp( pWnd->text, title )) return pWnd->hwndSelf;
    }
    return 0;
}



/***********************************************************************
 *           FindWindow16   (USER.50)
 */
HWND16 FindWindow16( SEGPTR className, LPCSTR title )
{
    return FindWindowEx16( 0, 0, className, title );
}


/***********************************************************************
 *           FindWindowEx16   (USER.427)
 */
HWND16 FindWindowEx16( HWND16 parent, HWND16 child,
                       SEGPTR className, LPCSTR title )
{
    ATOM atom = 0;

    if (className)
    {
        /* If the atom doesn't exist, then no class */
        /* with this name exists either. */
        if (!(atom = GlobalFindAtom16( className ))) return 0;
    }
    return WIN_FindWindow( parent, child, atom, title );
}


/***********************************************************************
 *           FindWindow32A   (USER32.197)
 */
HWND32 FindWindow32A( LPCSTR className, LPCSTR title )
{
    return FindWindowEx32A( 0, 0, className, title );
}


/***********************************************************************
 *           FindWindowEx32A   (USER32.198)
 */
HWND32 FindWindowEx32A( HWND32 parent, HWND32 child,
                        LPCSTR className, LPCSTR title )
{
    ATOM atom = 0;

    if (className)
    {
        /* If the atom doesn't exist, then no class */
        /* with this name exists either. */
        if (!(atom = GlobalFindAtom32A( className ))) return 0;
    }
    return WIN_FindWindow( 0, 0, atom, title );
}


/***********************************************************************
 *           FindWindowEx32W   (USER32.199)
 */
HWND32 FindWindowEx32W( HWND32 parent, HWND32 child,
                        LPCWSTR className, LPCWSTR title )
{
    ATOM atom = 0;
    char *buffer;
    HWND hwnd;

    if (className)
    {
        /* If the atom doesn't exist, then no class */
        /* with this name exists either. */
        if (!(atom = GlobalFindAtom32W( className ))) return 0;
    }
    buffer = title ? STRING32_DupUniToAnsi( title ) : NULL;
    hwnd = WIN_FindWindow( 0, 0, atom, buffer );
    if (buffer) free( buffer );
    return hwnd;
}


/***********************************************************************
 *           FindWindow32W   (USER32.200)
 */
HWND32 FindWindow32W( LPCWSTR className, LPCWSTR title )
{
    return FindWindowEx32W( 0, 0, className, title );
}


/**********************************************************************
 *           WIN_GetDesktop
 */
WND *WIN_GetDesktop(void)
{
    return pWndDesktop;
}


/**********************************************************************
 *           GetDesktopWindow16   (USER.286)
 */
HWND16 GetDesktopWindow16(void)
{
    return (HWND16)pWndDesktop->hwndSelf;
}


/**********************************************************************
 *           GetDesktopWindow32   (USER32.231)
 */
HWND32 GetDesktopWindow32(void)
{
    return pWndDesktop->hwndSelf;
}


/**********************************************************************
 *           GetDesktopHwnd   (USER.278)
 *
 * Exactly the same thing as GetDesktopWindow(), but not documented.
 * Don't ask me why...
 */
HWND16 GetDesktopHwnd(void)
{
    return (HWND16)pWndDesktop->hwndSelf;
}


/*******************************************************************
 *           EnableWindow   (USER.34)
 */
BOOL EnableWindow( HWND hwnd, BOOL enable )
{
    WND *wndPtr;

    if (!(wndPtr = WIN_FindWndPtr( hwnd ))) return FALSE;
    if (enable && (wndPtr->dwStyle & WS_DISABLED))
    {
	  /* Enable window */
	wndPtr->dwStyle &= ~WS_DISABLED;
	SendMessage16( hwnd, WM_ENABLE, TRUE, 0 );
	return TRUE;
    }
    else if (!enable && !(wndPtr->dwStyle & WS_DISABLED))
    {
	  /* Disable window */
	wndPtr->dwStyle |= WS_DISABLED;
	if ((hwnd == GetFocus32()) || IsChild( hwnd, GetFocus32() ))
	    SetFocus32( 0 );  /* A disabled window can't have the focus */
	if ((hwnd == GetCapture32()) || IsChild( hwnd, GetCapture32() ))
	    ReleaseCapture();  /* A disabled window can't capture the mouse */
	SendMessage16( hwnd, WM_ENABLE, FALSE, 0 );
	return FALSE;
    }
    return ((wndPtr->dwStyle & WS_DISABLED) != 0);
}


/***********************************************************************
 *           IsWindowEnabled   (USER.35) (USER32.348)
 */ 
BOOL IsWindowEnabled(HWND hWnd)
{
    WND * wndPtr; 

    if (!(wndPtr = WIN_FindWndPtr(hWnd))) return FALSE;
    return !(wndPtr->dwStyle & WS_DISABLED);
}


/***********************************************************************
 *           IsWindowUnicode   (USER32.349)
 */
BOOL32 IsWindowUnicode( HWND32 hwnd )
{
    WND * wndPtr; 

    if (!(wndPtr = WIN_FindWndPtr(hwnd))) return FALSE;
    return (WINPROC_GetProcType( wndPtr->winproc ) == WIN_PROC_32W);
}


/**********************************************************************
 *	     GetWindowWord    (USER.133) (USER32.313)
 */
WORD GetWindowWord( HWND32 hwnd, INT32 offset )
{
    WND * wndPtr = WIN_FindWndPtr( hwnd );
    if (!wndPtr) return 0;
    if (offset >= 0)
    {
        if (offset + sizeof(WORD) > wndPtr->class->cbWndExtra)
        {
            fprintf( stderr, "SetWindowWord: invalid offset %d\n", offset );
            return 0;
        }
        return *(WORD *)(((char *)wndPtr->wExtra) + offset);
    }
    switch(offset)
    {
    case GWW_ID:         return wndPtr->wIDmenu;
    case GWW_HWNDPARENT: return wndPtr->parent ? wndPtr->parent->hwndSelf : 0;
    case GWW_HINSTANCE:  return (WORD)wndPtr->hInstance;
    default:
        fprintf( stderr, "GetWindowWord: invalid offset %d\n", offset );
        return 0;
    }
}


/**********************************************************************
 *	     WIN_GetWindowInstance
 */
HINSTANCE16 WIN_GetWindowInstance( HWND32 hwnd )
{
    WND * wndPtr = WIN_FindWndPtr( hwnd );
    if (!wndPtr) return (HINSTANCE16)0;
    return wndPtr->hInstance;
}


/**********************************************************************
 *	     SetWindowWord    (USER.134) (USER32.523)
 */
WORD SetWindowWord( HWND32 hwnd, INT32 offset, WORD newval )
{
    WORD *ptr, retval;
    WND * wndPtr = WIN_FindWndPtr( hwnd );
    if (!wndPtr) return 0;
    if (offset >= 0)
    {
        if (offset + sizeof(WORD) > wndPtr->class->cbWndExtra)
        {
            fprintf( stderr, "SetWindowWord: invalid offset %d\n", offset );
            return 0;
        }
        ptr = (WORD *)(((char *)wndPtr->wExtra) + offset);
    }
    else switch(offset)
    {
	case GWW_ID:        ptr = (WORD *)&wndPtr->wIDmenu; break;
	case GWW_HINSTANCE: ptr = (WORD *)&wndPtr->hInstance; break;
	case GWW_HWNDPARENT: return SetParent( hwnd, newval );
	default:
            fprintf( stderr, "SetWindowWord: invalid offset %d\n", offset );
            return 0;
    }
    retval = *ptr;
    *ptr = newval;
    return retval;
}


/**********************************************************************
 *	     WIN_GetWindowLong
 *
 * Helper function for GetWindowLong().
 */
static LONG WIN_GetWindowLong( HWND32 hwnd, INT32 offset, WINDOWPROCTYPE type )
{
    LONG retval;
    WND * wndPtr = WIN_FindWndPtr( hwnd );
    if (!wndPtr) return 0;
    if (offset >= 0)
    {
        if (offset + sizeof(LONG) > wndPtr->class->cbWndExtra)
        {
            fprintf( stderr, "GetWindowLong: invalid offset %d\n", offset );
            return 0;
        }
        retval = *(LONG *)(((char *)wndPtr->wExtra) + offset);
        /* Special case for dialog window procedure */
        if ((offset == DWL_DLGPROC) && (wndPtr->flags & WIN_ISDIALOG))
            return (LONG)WINPROC_GetProc( (HWINDOWPROC)retval, type );
        return retval;
    }
    switch(offset)
    {
        case GWL_USERDATA:   return wndPtr->userdata;
        case GWL_STYLE:      return wndPtr->dwStyle;
        case GWL_EXSTYLE:    return wndPtr->dwExStyle;
        case GWL_ID:         return wndPtr->wIDmenu;
        case GWL_WNDPROC:    return (LONG)WINPROC_GetProc( wndPtr->winproc,
                                                           type );
        case GWL_HWNDPARENT: return wndPtr->parent ?
                                        (HWND32)wndPtr->parent->hwndSelf : 0;
        case GWL_HINSTANCE:  return (HINSTANCE32)wndPtr->hInstance;
        default:
            fprintf( stderr, "GetWindowLong: unknown offset %d\n", offset );
    }
    return 0;
}


/**********************************************************************
 *	     WIN_SetWindowLong
 *
 * Helper function for SetWindowLong().
 */
static LONG WIN_SetWindowLong( HWND32 hwnd, INT32 offset, LONG newval,
                               WINDOWPROCTYPE type )
{
    LONG *ptr, retval;
    WND * wndPtr = WIN_FindWndPtr( hwnd );
    if (!wndPtr) return 0;
    if (offset >= 0)
    {
        if (offset + sizeof(LONG) > wndPtr->class->cbWndExtra)
        {
            fprintf( stderr, "SetWindowLong: invalid offset %d\n", offset );
            return 0;
        }
        ptr = (LONG *)(((char *)wndPtr->wExtra) + offset);
        /* Special case for dialog window procedure */
        if ((offset == DWL_DLGPROC) && (wndPtr->flags & WIN_ISDIALOG))
        {
            retval = (LONG)WINPROC_GetProc( (HWINDOWPROC)*ptr, type );
            WINPROC_SetProc( (HWINDOWPROC *)ptr, (WNDPROC16)newval, type );
            return retval;
        }
    }
    else switch(offset)
    {
        case GWL_ID:
        case GWL_HINSTANCE:
            return SetWindowWord( hwnd, offset, (WORD)newval );
	case GWL_WNDPROC:
            retval = (LONG)WINPROC_GetProc( wndPtr->winproc, type );
            WINPROC_SetProc( &wndPtr->winproc, (WNDPROC16)newval, type );
            return retval;
	case GWL_STYLE:
            ptr = &wndPtr->dwStyle;
            /* Some bits can't be changed this way */
            newval &= ~(WS_VISIBLE | WS_CHILD);
            newval |= (*ptr & (WS_VISIBLE | WS_CHILD));
            break;
        case GWL_USERDATA: ptr = &wndPtr->userdata; break;
        case GWL_EXSTYLE:  ptr = &wndPtr->dwExStyle; break;
	default:
            fprintf( stderr, "SetWindowLong: invalid offset %d\n", offset );
            return 0;
    }
    retval = *ptr;
    *ptr = newval;
    return retval;
}


/**********************************************************************
 *	     GetWindowLong16    (USER.135)
 */
LONG GetWindowLong16( HWND16 hwnd, INT16 offset )
{
    return WIN_GetWindowLong( (HWND32)hwnd, offset, WIN_PROC_16 );
}


/**********************************************************************
 *	     GetWindowLong32A    (USER32.304)
 */
LONG GetWindowLong32A( HWND32 hwnd, INT32 offset )
{
    return WIN_GetWindowLong( hwnd, offset, WIN_PROC_32A );
}


/**********************************************************************
 *	     GetWindowLong32W    (USER32.305)
 */
LONG GetWindowLong32W( HWND32 hwnd, INT32 offset )
{
    return WIN_GetWindowLong( hwnd, offset, WIN_PROC_32W );
}


/**********************************************************************
 *	     SetWindowLong16    (USER.136)
 */
LONG SetWindowLong16( HWND16 hwnd, INT16 offset, LONG newval )
{
    return WIN_SetWindowLong( hwnd, offset, newval, WIN_PROC_16 );
}


/**********************************************************************
 *	     SetWindowLong32A    (USER32.516)
 */
LONG SetWindowLong32A( HWND32 hwnd, INT32 offset, LONG newval )
{
    return WIN_SetWindowLong( hwnd, offset, newval, WIN_PROC_32A );
}


/**********************************************************************
 *	     SetWindowLong32W    (USER32.517)
 */
LONG SetWindowLong32W( HWND32 hwnd, INT32 offset, LONG newval )
{
    return WIN_SetWindowLong( hwnd, offset, newval, WIN_PROC_32W );
}


/*******************************************************************
 *	     GetWindowText16    (USER.36)
 */
INT16 GetWindowText16( HWND16 hwnd, SEGPTR lpString, INT16 nMaxCount )
{
    return (INT16)SendMessage16(hwnd, WM_GETTEXT, nMaxCount, (LPARAM)lpString);
}


/*******************************************************************
 *	     GetWindowText32A    (USER32.308)
 */
INT32 GetWindowText32A( HWND32 hwnd, LPSTR lpString, INT32 nMaxCount )
{
    return (INT32)SendMessage32A( hwnd, WM_GETTEXT, nMaxCount,
                                  (LPARAM)lpString );
}


/*******************************************************************
 *	     GetWindowText32W    (USER32.311)
 */
INT32 GetWindowText32W( HWND32 hwnd, LPWSTR lpString, INT32 nMaxCount )
{
    return (INT32)SendMessage32W( hwnd, WM_GETTEXT, nMaxCount,
                                  (LPARAM)lpString );
}


/*******************************************************************
 *	     SetWindowText16    (USER.37)
 */
void SetWindowText16( HWND16 hwnd, SEGPTR lpString )
{
    SendMessage16( hwnd, WM_SETTEXT, 0, (LPARAM)lpString );
}


/*******************************************************************
 *	     SetWindowText32A    (USER32.)
 */
void SetWindowText32A( HWND32 hwnd, LPCSTR lpString )
{
    SendMessage32A( hwnd, WM_SETTEXT, 0, (LPARAM)lpString );
}


/*******************************************************************
 *	     SetWindowText32W    (USER32.)
 */
void SetWindowText32W( HWND32 hwnd, LPCWSTR lpString )
{
    SendMessage32W( hwnd, WM_SETTEXT, 0, (LPARAM)lpString );
}


/*******************************************************************
 *         GetWindowTextLength    (USER.38)
 */
int GetWindowTextLength(HWND hwnd)
{
    return (int)SendMessage16(hwnd, WM_GETTEXTLENGTH, 0, 0 );
}


/*******************************************************************
 *         IsWindow   (USER.47) (USER32.347)
 */
BOOL16 IsWindow( HWND32 hwnd )
{
    WND * wndPtr = WIN_FindWndPtr( hwnd );
    return ((wndPtr != NULL) && (wndPtr->dwMagic == WND_MAGIC));
}


/*****************************************************************
 *         GetParent16   (USER.46)
 */
HWND16 GetParent16( HWND16 hwnd )
{
    return (HWND16)GetParent32( hwnd );
}


/*****************************************************************
 *         GetParent32   (USER32.277)
 */
HWND32 GetParent32( HWND32 hwnd )
{
    WND *wndPtr = WIN_FindWndPtr(hwnd);
    if (!wndPtr) return 0;
    wndPtr = (wndPtr->dwStyle & WS_CHILD) ? wndPtr->parent : wndPtr->owner;
    return wndPtr ? wndPtr->hwndSelf : 0;
}


/*****************************************************************
 *         WIN_GetTopParent
 *
 * Get the top-level parent for a child window.
 */
HWND32 WIN_GetTopParent( HWND32 hwnd )
{
    WND *wndPtr = WIN_FindWndPtr( hwnd );
    while (wndPtr && (wndPtr->dwStyle & WS_CHILD)) wndPtr = wndPtr->parent;
    return wndPtr ? wndPtr->hwndSelf : 0;
}


/*****************************************************************
 *         SetParent              (USER.233)
 */
HWND SetParent(HWND hwndChild, HWND hwndNewParent)
{
    HWND oldParent;

    WND *wndPtr = WIN_FindWndPtr(hwndChild);
    WND *pWndParent = WIN_FindWndPtr( hwndNewParent );
    if (!wndPtr || !pWndParent || !(wndPtr->dwStyle & WS_CHILD)) return 0;

    oldParent = wndPtr->parent->hwndSelf;

    WIN_UnlinkWindow(hwndChild);
    if (hwndNewParent) wndPtr->parent = pWndParent;
    WIN_LinkWindow(hwndChild, HWND_BOTTOM);
    
    if (IsWindowVisible(hwndChild)) UpdateWindow(hwndChild);
    
    return oldParent;
}



/*******************************************************************
 *         IsChild    (USER.48)
 */
BOOL IsChild( HWND parent, HWND child )
{
    WND * wndPtr = WIN_FindWndPtr( child );
    while (wndPtr && (wndPtr->dwStyle & WS_CHILD))
    {
        wndPtr = wndPtr->parent;
	if (wndPtr->hwndSelf == parent) return TRUE;
    }
    return FALSE;
}


/***********************************************************************
 *           IsWindowVisible   (USER.49) (USER32.350)
 */
BOOL IsWindowVisible( HWND hwnd )
{
    WND *wndPtr = WIN_FindWndPtr( hwnd );
    while (wndPtr && (wndPtr->dwStyle & WS_CHILD))
    {
        if (!(wndPtr->dwStyle & WS_VISIBLE)) return FALSE;
        wndPtr = wndPtr->parent;
    }
    return (wndPtr && (wndPtr->dwStyle & WS_VISIBLE));
}

/***********************************************************************
 *           WIN_IsWindowDrawable
 * 
 * hwnd is drawable when it is visible, all parents are not 
 * minimized, and it is itself not minimized unless we are 
 * trying to draw icon and the default class icon is set.
 */
BOOL32 WIN_IsWindowDrawable( WND* wnd , BOOL32 icon )
{
  HWND   hwnd= wnd->hwndSelf;
  BOOL32 yes = TRUE;

  while(wnd && yes)
  { 
    if( wnd->dwStyle & WS_MINIMIZE )
	if( wnd->hwndSelf != hwnd ) break;
	else if( icon && wnd->class->hIcon ) break;

    yes = yes && (wnd->dwStyle & WS_VISIBLE);
    wnd = wnd->parent; }      
  return (!wnd && yes);
}

/*******************************************************************
 *         GetTopWindow    (USER.229)
 */
HWND GetTopWindow( HWND hwnd )
{
    WND * wndPtr = WIN_FindWndPtr( hwnd );
    if (wndPtr && wndPtr->child) return wndPtr->child->hwndSelf;
    else return 0;
}


/*******************************************************************
 *         GetWindow    (USER.262)
 */
HWND GetWindow( HWND hwnd, WORD rel )
{
    WND * wndPtr = WIN_FindWndPtr( hwnd );
    if (!wndPtr) return 0;
    switch(rel)
    {
    case GW_HWNDFIRST:
        if (wndPtr->parent) return wndPtr->parent->child->hwndSelf;
	else return 0;
	
    case GW_HWNDLAST:
	if (!wndPtr->parent) return 0;  /* Desktop window */
	while (wndPtr->next) wndPtr = wndPtr->next;
        return wndPtr->hwndSelf;
	
    case GW_HWNDNEXT:
        if (!wndPtr->next) return 0;
	return wndPtr->next->hwndSelf;
	
    case GW_HWNDPREV:
        if (!wndPtr->parent) return 0;  /* Desktop window */
        wndPtr = wndPtr->parent->child;  /* First sibling */
        if (wndPtr->hwndSelf == hwnd) return 0;  /* First in list */
        while (wndPtr->next)
        {
            if (wndPtr->next->hwndSelf == hwnd) return wndPtr->hwndSelf;
            wndPtr = wndPtr->next;
        }
        return 0;
	
    case GW_OWNER:
	return wndPtr->owner ? wndPtr->owner->hwndSelf : 0;

    case GW_CHILD:
	return wndPtr->child ? wndPtr->child->hwndSelf : 0;
    }
    return 0;
}


/*******************************************************************
 *         GetNextWindow    (USER.230)
 */
HWND GetNextWindow( HWND hwnd, WORD flag )
{
    if ((flag != GW_HWNDNEXT) && (flag != GW_HWNDPREV)) return 0;
    return GetWindow( hwnd, flag );
}

/*******************************************************************
 *         ShowOwnedPopups  (USER.265)
 */
void ShowOwnedPopups( HWND owner, BOOL fShow )
{
    WND *pWnd = pWndDesktop->child;
    while (pWnd)
    {
        if (pWnd->owner && (pWnd->owner->hwndSelf == owner) &&
            (pWnd->dwStyle & WS_POPUP))
            ShowWindow( pWnd->hwndSelf, fShow ? SW_SHOW : SW_HIDE );
        pWnd = pWnd->next;
    }
}


/*******************************************************************
 *         GetLastActivePopup    (USER.287)
 */
HWND GetLastActivePopup(HWND hwnd)
{
    WND *wndPtr;
    wndPtr = WIN_FindWndPtr(hwnd);
    if (wndPtr == NULL) return hwnd;
    return wndPtr->hwndLastActive;
}


/*******************************************************************
 *           WIN_BuildWinArray
 *
 * Build an array of pointers to all children of a given window.
 * The array must be freed with HeapFree(SystemHeap).
 */
WND **WIN_BuildWinArray( WND *wndPtr )
{
    WND **list, **ppWnd;
    WND *pWnd;
    INT32 count;

    /* First count the windows */

    if (!wndPtr) wndPtr = pWndDesktop;
    for (pWnd = wndPtr->child, count = 0; pWnd; pWnd = pWnd->next) count++;
    count++;  /* For the terminating NULL */

    /* Now build the list of all windows */

    if (!(list = (WND **)HeapAlloc( SystemHeap, 0, sizeof(WND *) * count )))
        return NULL;
    for (pWnd = wndPtr->child, ppWnd = list; pWnd; pWnd = pWnd->next)
        *ppWnd++ = pWnd;
    *ppWnd = NULL;
    return list;
}


/*******************************************************************
 *           EnumWindows16   (USER.54)
 */
BOOL16 EnumWindows16( WNDENUMPROC16 lpEnumFunc, LPARAM lParam )
{
    WND **list, **ppWnd;

    /* We have to build a list of all windows first, to avoid */
    /* unpleasant side-effects, for instance if the callback  */
    /* function changes the Z-order of the windows.           */

    if (!(list = WIN_BuildWinArray( pWndDesktop ))) return FALSE;

    /* Now call the callback function for every window */

    for (ppWnd = list; *ppWnd; ppWnd++)
    {
        /* Make sure that the window still exists */
        if (!IsWindow((*ppWnd)->hwndSelf)) continue;
        if (!lpEnumFunc( (*ppWnd)->hwndSelf, lParam )) break;
    }
    HeapFree( SystemHeap, 0, list );
    return TRUE;
}


/*******************************************************************
 *           EnumWindows32   (USER32.192)
 */
BOOL32 EnumWindows32( WNDENUMPROC32 lpEnumFunc, LPARAM lParam )
{
    return (BOOL32)EnumWindows16( (WNDENUMPROC16)lpEnumFunc, lParam );
}


/**********************************************************************
 *           EnumTaskWindows16   (USER.225)
 */
BOOL16 EnumTaskWindows16( HTASK16 hTask, WNDENUMPROC16 func, LPARAM lParam )
{
    WND **list, **ppWnd;
    HQUEUE16 hQueue = GetTaskQueue( hTask );

    /* This function is the same as EnumWindows(),    */
    /* except for an added check on the window queue. */

    if (!(list = WIN_BuildWinArray( pWndDesktop ))) return FALSE;

    /* Now call the callback function for every window */

    for (ppWnd = list; *ppWnd; ppWnd++)
    {
        /* Make sure that the window still exists */
        if (!IsWindow((*ppWnd)->hwndSelf)) continue;
        if ((*ppWnd)->hmemTaskQ != hQueue) continue;  /* Check the queue */
        if (!func( (*ppWnd)->hwndSelf, lParam )) break;
    }
    HeapFree( SystemHeap, 0, list );
    return TRUE;
}


/**********************************************************************
 *           EnumThreadWindows   (USER32.189)
 */
BOOL32 EnumThreadWindows( DWORD id, WNDENUMPROC32 func, LPARAM lParam )
{
    return (BOOL16)EnumTaskWindows16((HTASK16)id, (WNDENUMPROC16)func, lParam);
}


/**********************************************************************
 *           WIN_EnumChildWindows
 *
 * Helper function for EnumChildWindows().
 */
static BOOL16 WIN_EnumChildWindows( WND **ppWnd, WNDENUMPROC16 func,
                                    LPARAM lParam )
{
    WND **childList;
    BOOL16 ret = FALSE;

    while (*ppWnd)
    {
        /* Make sure that the window still exists */
        if (!IsWindow((*ppWnd)->hwndSelf)) continue;
        /* Build children list first */
        if (!(childList = WIN_BuildWinArray( *ppWnd ))) return FALSE;
        if (!func( (*ppWnd)->hwndSelf, lParam )) return FALSE;
        ret = WIN_EnumChildWindows( childList, func, lParam );
        HeapFree( SystemHeap, 0, childList );
        if (!ret) return FALSE;
        ppWnd++;
    }
    return TRUE;
}


/**********************************************************************
 *           EnumChildWindows16   (USER.55)
 */
BOOL16 EnumChildWindows16( HWND16 parent, WNDENUMPROC16 func, LPARAM lParam )
{
    WND **list, *pParent;

    if (!(pParent = WIN_FindWndPtr( parent ))) return FALSE;
    if (!(list = WIN_BuildWinArray( pParent ))) return FALSE;
    WIN_EnumChildWindows( list, func, lParam );
    HeapFree( SystemHeap, 0, list );
    return TRUE;
}


/**********************************************************************
 *           EnumChildWindows32   (USER32.177)
 */
BOOL32 EnumChildWindows32( HWND32 parent, WNDENUMPROC32 func, LPARAM lParam )
{
    return (BOOL32)EnumChildWindows16( (HWND16)parent, (WNDENUMPROC16)func,
                                       lParam );
}


/*******************************************************************
 *           AnyPopup   (USER.52)
 */
BOOL AnyPopup(void)
{
    WND *wndPtr;
    for (wndPtr = pWndDesktop->child; wndPtr; wndPtr = wndPtr->next)
        if (wndPtr->owner && (wndPtr->dwStyle & WS_VISIBLE)) return TRUE;
    return FALSE;
}

/*******************************************************************
 *			FlashWindow		[USER.105]
 */
BOOL FlashWindow(HWND hWnd, BOOL bInvert)
{
    WND *wndPtr = WIN_FindWndPtr(hWnd);

    dprintf_win(stddeb,"FlashWindow: %04x\n", hWnd);

    if (!wndPtr) return FALSE;

    if (wndPtr->dwStyle & WS_MINIMIZE)
    {
        if (bInvert && !(wndPtr->flags & WIN_NCACTIVATED))
        {
            HDC32 hDC = GetDC32(hWnd);
            
            if (!SendMessage16( hWnd, WM_ERASEBKGND, (WPARAM16)hDC, 0 ))
                wndPtr->flags |= WIN_NEEDS_ERASEBKGND;
            
            ReleaseDC32( hWnd, hDC );
            wndPtr->flags |= WIN_NCACTIVATED;
        }
        else
        {
            PAINT_RedrawWindow( hWnd, 0, 0, RDW_INVALIDATE | RDW_ERASE |
					  RDW_UPDATENOW | RDW_FRAME, 0 );
            wndPtr->flags &= ~WIN_NCACTIVATED;
        }
        return TRUE;
    }
    else
    {
        WPARAM16 wparam;
        if (bInvert) wparam = !(wndPtr->flags & WIN_NCACTIVATED);
        else wparam = (hWnd == GetActiveWindow());

        SendMessage16( hWnd, WM_NCACTIVATE, wparam, (LPARAM)0 );
        return wparam;
    }
}


/*******************************************************************
 *           SetSysModalWindow16   (USER.188)
 */
HWND16 SetSysModalWindow16( HWND16 hWnd )
{
    HWND hWndOldModal = hwndSysModal;
    hwndSysModal = hWnd;
    dprintf_win(stdnimp,"EMPTY STUB !! SetSysModalWindow(%04x) !\n", hWnd);
    return hWndOldModal;
}


/*******************************************************************
 *           GetSysModalWindow16   (USER.52)
 */
HWND16 GetSysModalWindow16(void)
{
    return hwndSysModal;
}


/*******************************************************************
 *			DRAG_QueryUpdate
 *
 * recursively find a child that contains spDragInfo->pt point 
 * and send WM_QUERYDROPOBJECT
 */
BOOL16 DRAG_QueryUpdate( HWND hQueryWnd, SEGPTR spDragInfo, BOOL32 bNoSend )
{
 BOOL16		wParam,bResult = 0;
 POINT16        pt;
 LPDRAGINFO	ptrDragInfo = (LPDRAGINFO) PTR_SEG_TO_LIN(spDragInfo);
 WND 	       *ptrQueryWnd = WIN_FindWndPtr(hQueryWnd),*ptrWnd;
 RECT16		tempRect;

 if( !ptrQueryWnd || !ptrDragInfo ) return 0;

 pt 		= ptrDragInfo->pt;

 GetWindowRect16(hQueryWnd,&tempRect); 

 if( !PtInRect16(&tempRect,pt) ||
     (ptrQueryWnd->dwStyle & WS_DISABLED) )
	return 0;

 if( !(ptrQueryWnd->dwStyle & WS_MINIMIZE) ) 
   {
     tempRect = ptrQueryWnd->rectClient;
     if(ptrQueryWnd->dwStyle & WS_CHILD)
        MapWindowPoints16(ptrQueryWnd->parent->hwndSelf,0,(LPPOINT16)&tempRect,2);

     if( PtInRect16(&tempRect,pt) )
	{
	 wParam = 0;
         
	 for (ptrWnd = ptrQueryWnd->child; ptrWnd ;ptrWnd = ptrWnd->next)
             if( ptrWnd->dwStyle & WS_VISIBLE )
	     {
                 GetWindowRect16(ptrWnd->hwndSelf,&tempRect);

                 if( PtInRect16(&tempRect,pt) ) 
                     break;
	     }

	 if(ptrWnd)
         {
	    dprintf_msg(stddeb,"DragQueryUpdate: hwnd = %04x, %d %d - %d %d\n",
                        ptrWnd->hwndSelf, ptrWnd->rectWindow.left, ptrWnd->rectWindow.top,
			ptrWnd->rectWindow.right, ptrWnd->rectWindow.bottom );
            if( !(ptrWnd->dwStyle & WS_DISABLED) )
	        bResult = DRAG_QueryUpdate(ptrWnd->hwndSelf, spDragInfo, bNoSend);
         }

	 if(bResult) return bResult;
	}
     else wParam = 1;
   }
 else wParam = 1;

 ScreenToClient16(hQueryWnd,&ptrDragInfo->pt);

 ptrDragInfo->hScope = hQueryWnd;

 bResult = ( bNoSend ) 
	   ? ptrQueryWnd->dwExStyle & WS_EX_ACCEPTFILES
	   : SendMessage16( hQueryWnd ,WM_QUERYDROPOBJECT ,
                          (WPARAM16)wParam ,(LPARAM) spDragInfo );
 if( !bResult ) 
      ptrDragInfo->pt = pt;

 return bResult;
}

/*******************************************************************
 *                      DragDetect ( USER.465 )
 *
 */
BOOL16 DragDetect(HWND16 hWnd, POINT16 pt)
{
  MSG16 msg;
  RECT16  rect;

  rect.left = pt.x - wDragWidth;
  rect.right = pt.x + wDragWidth;

  rect.top = pt.y - wDragHeight;
  rect.bottom = pt.y + wDragHeight;

  SetCapture32(hWnd);

  while(1)
   {
        while(PeekMessage16(&msg ,0 ,WM_MOUSEFIRST ,WM_MOUSELAST ,PM_REMOVE))
         {
           if( msg.message == WM_LBUTTONUP )
                {
                  ReleaseCapture();
                  return 0;
                }
           if( msg.message == WM_MOUSEMOVE )
		{
                  if( !PtInRect16( &rect, MAKEPOINT16(msg.lParam) ) )
                    {
                      ReleaseCapture();
                      return 1;
                    }
		}
         }
        WaitMessage();
   }

  return 0;
}

/******************************************************************************
 *                              DragObject ( USER.464 )
 *
 */
DWORD DragObject(HWND hwndScope, HWND hWnd, WORD wObj, HANDLE16 hOfStruct,
                WORD szList , HCURSOR16 hCursor)
{
 MSG16	 	msg;
 LPDRAGINFO	lpDragInfo;
 SEGPTR		spDragInfo;
 HCURSOR16 	hDragCursor=0, hOldCursor=0, hBummer=0;
 HGLOBAL16	hDragInfo  = GlobalAlloc16( GMEM_SHARE | GMEM_ZEROINIT, 2*sizeof(DRAGINFO));
 WND           *wndPtr = WIN_FindWndPtr(hWnd);
 DWORD		dwRet = 0;
 short	 	dragDone = 0;
 HCURSOR16	hCurrentCursor = 0;
 HWND		hCurrentWnd = 0;
 BOOL16		b;

 lpDragInfo = (LPDRAGINFO) GlobalLock16(hDragInfo);
 spDragInfo = (SEGPTR) WIN16_GlobalLock16(hDragInfo);

 if( !lpDragInfo || !spDragInfo ) return 0L;

 hBummer = LoadCursor16(0,IDC_BUMMER);

 if( !hBummer || !wndPtr )
   {
        GlobalFree16(hDragInfo);
        return 0L;
   }

 if(hCursor)
   {
	if( !(hDragCursor = CURSORICON_IconToCursor(hCursor, FALSE)) )
	  {
	   GlobalFree16(hDragInfo);
	   return 0L;
	  }

	if( hDragCursor == hCursor ) hDragCursor = 0;
	else hCursor = hDragCursor;

	hOldCursor = SetCursor(hDragCursor);
   }

 lpDragInfo->hWnd   = hWnd;
 lpDragInfo->hScope = 0;
 lpDragInfo->wFlags = wObj;
 lpDragInfo->hList  = szList; /* near pointer! */
 lpDragInfo->hOfStruct = hOfStruct;
 lpDragInfo->l = 0L; 

 SetCapture32(hWnd);
 ShowCursor(1);

 while( !dragDone )
  {
    WaitMessage();

    if( !PeekMessage16(&msg,0,WM_MOUSEFIRST,WM_MOUSELAST,PM_REMOVE) )
	 continue;

   *(lpDragInfo+1) = *lpDragInfo;

    lpDragInfo->pt = msg.pt;

    /* update DRAGINFO struct */
    dprintf_msg(stddeb,"drag: lpDI->hScope = %04x\n",lpDragInfo->hScope);

    if( (b = DRAG_QueryUpdate(hwndScope, spDragInfo, FALSE)) > 0 )
	 hCurrentCursor = hCursor;
    else
        {
         hCurrentCursor = hBummer;
         lpDragInfo->hScope = 0;
	}
    if( hCurrentCursor )
        SetCursor(hCurrentCursor);

    dprintf_msg(stddeb,"drag: got %04x\n", b);

    /* send WM_DRAGLOOP */
    SendMessage16( hWnd, WM_DRAGLOOP, (WPARAM16)(hCurrentCursor != hBummer) , 
	                            (LPARAM) spDragInfo );
    /* send WM_DRAGSELECT or WM_DRAGMOVE */
    if( hCurrentWnd != lpDragInfo->hScope )
	{
	 if( hCurrentWnd )
	   SendMessage16( hCurrentWnd, WM_DRAGSELECT, 0, 
		       (LPARAM)MAKELONG(LOWORD(spDragInfo)+sizeof(DRAGINFO),
				        HIWORD(spDragInfo)) );
	 hCurrentWnd = lpDragInfo->hScope;
	 if( hCurrentWnd )
           SendMessage16( hCurrentWnd, WM_DRAGSELECT, 1, (LPARAM)spDragInfo); 
	}
    else
	if( hCurrentWnd )
	   SendMessage16( hCurrentWnd, WM_DRAGMOVE, 0, (LPARAM)spDragInfo);


    /* check if we're done */
    if( msg.message == WM_LBUTTONUP || msg.message == WM_NCLBUTTONUP )
	dragDone = TRUE;
  }

 ReleaseCapture();
 ShowCursor(0);

 if( hCursor )
   {
     SetCursor(hOldCursor);
     if( hDragCursor )
	 DestroyCursor(hDragCursor);
   }

 if( hCurrentCursor != hBummer ) 
	dwRet = SendMessage16( lpDragInfo->hScope, WM_DROPOBJECT, 
                               (WPARAM16)hWnd, (LPARAM)spDragInfo );
 GlobalFree16(hDragInfo);

 return dwRet;
}

