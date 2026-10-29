#include "terminal_view_windows.h"
#include <windowsx.h>
#include <imm.h>
#include <stdlib.h>
#include <string.h>
#define TERMINAL_CHANGED (WM_APP + 201)
typedef struct TerminalView {
    AxyneTerminal *terminal;
    HWND volatile window;
    LONG generation;
    volatile LONG pending;
    HFONT fonts[8];
    int width, height, first, last;
    uint32_t foreground, background;
    wchar_t high_surrogate;
} TerminalView;
static volatile LONG next_generation;
static COLORREF color(uint32_t value) { return RGB(value>>16, (value>>8)&255, value&255); }
static TerminalView *view(HWND w) { return (TerminalView *)GetWindowLongPtrW(w,GWLP_USERDATA); }
static void changed(AxyneTerminal *terminal, void *context) {
    TerminalView *v=(TerminalView *)context; (void)terminal;
    if(InterlockedExchange(&v->pending,1))return;
    HWND w=(HWND)InterlockedCompareExchangePointer((PVOID volatile *)&v->window,NULL,NULL);
    if(!w||!PostMessageW(w,TERMINAL_CHANGED,(WPARAM)v->generation,0))InterlockedExchange(&v->pending,0);
}
static void exited(AxyneTerminal *terminal,int code,void *context) { (void)code;changed(terminal,context); }
static int cell_text(const AxyneTerminalCell *cell,wchar_t *text) {
    int n=0;for(int i=0;i<6 && cell->chars[i];++i) { uint32_t c=cell->chars[i]; if(c<=0xffff) text[n++]=(wchar_t)c; else if(c<=0x10ffff) {text[n++]=(wchar_t)(0xd800+((c-0x10000)>>10));text[n++]=(wchar_t)(0xdc00+((c-0x10000)&1023));} }
    if(!n) text[n++]=L' ';return n;
}
static void resize(HWND w,TerminalView *v) {
    RECT rect;GetClientRect(w,&rect);int rows=(rect.bottom-28)/v->height,cols=(rect.right-36)/v->width;
    rows=max(1,min(512,rows));cols=max(1,min(1024,cols));
    AxyneTerminalSnapshot s={0};if(axyne_terminal_snapshot(v->terminal,&s,NULL)==AXYNE_STATUS_OK) {
        int different=s.rows!=rows||s.columns!=cols;axyne_terminal_snapshot_free(&s);if(different)axyne_terminal_resize(v->terminal,rows,cols,NULL);
    }
}
static int index_at(HWND w,TerminalView *v,int x,int y) {
    (void)w;AxyneTerminalSnapshot s={0};if(axyne_terminal_snapshot(v->terminal,&s,NULL)!=AXYNE_STATUS_OK)return -1;
    int index=max(0,min(s.rows-1,(y-14)/v->height))*s.columns+max(0,min(s.columns-1,(x-18)/v->width));axyne_terminal_snapshot_free(&s);return index;
}
static void copy(HWND w,TerminalView *v) {
    if(v->first<0)return;AxyneTerminalSnapshot s={0};if(axyne_terminal_snapshot(v->terminal,&s,NULL)!=AXYNE_STATUS_OK)return;
    int first=min(v->first,v->last),last=min(max(v->first,v->last),s.rows*s.columns-1);
    size_t capacity=((size_t)(last-first+1)*14+1)*sizeof(wchar_t);
    HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,capacity);wchar_t *text=memory?GlobalLock(memory):NULL;
    if(text) {int at=0;for(int i=first;i<=last;++i) {if(i>first&&i%s.columns==0){text[at++]=L'\r';text[at++]=L'\n';}if(s.cells[i].width)at+=cell_text(&s.cells[i],text+at);}text[at]=0;GlobalUnlock(memory);
        if(OpenClipboard(w)){EmptyClipboard();if(SetClipboardData(CF_UNICODETEXT,memory))memory=NULL;CloseClipboard();}}
    if(memory)GlobalFree(memory);axyne_terminal_snapshot_free(&s);
}
static void paste(HWND w,TerminalView *v) {
    if(!OpenClipboard(w))return;HANDLE memory=GetClipboardData(CF_UNICODETEXT);const wchar_t *text=memory?GlobalLock(memory):NULL;
    if(text) {int n=WideCharToMultiByte(CP_UTF8,0,text,-1,NULL,0,NULL,NULL);char *utf8=n>0?malloc((size_t)n):NULL;
        if(utf8){WideCharToMultiByte(CP_UTF8,0,text,-1,utf8,n,NULL,NULL);size_t out=0;for(int i=0;i<n-1;++i){if(utf8[i]=='\r'&&utf8[i+1]=='\n')++i;utf8[out++]=utf8[i]=='\n'?'\r':utf8[i];}axyne_terminal_paste(v->terminal,utf8,out,NULL);free(utf8);}GlobalUnlock(memory);}
    CloseClipboard();
}
static void unicode(TerminalView *v,wchar_t ch,unsigned mods) {
    if(ch>=0xd800&&ch<=0xdbff){v->high_surrogate=ch;return;}uint32_t code=ch;
    if(ch>=0xdc00&&ch<=0xdfff&&v->high_surrogate)code=0x10000+((v->high_surrogate-0xd800)<<10)+ch-0xdc00;
    v->high_surrogate=0;axyne_terminal_unichar(v->terminal,code,mods,NULL);
}
static LRESULT CALLBACK proc(HWND w,UINT message,WPARAM wp,LPARAM lp) {
    TerminalView *v=view(w);
    if(message==WM_NCCREATE) {v=calloc(1,sizeof *v);if(!v)return FALSE;v->window=w;v->generation=InterlockedIncrement(&next_generation);v->first=v->last=-1;v->width=8;v->height=16;
        if(axyne_terminal_create(24,80,2000,&v->terminal,NULL)!=AXYNE_STATUS_OK){free(v);return FALSE;}SetWindowLongPtrW(w,GWLP_USERDATA,(LONG_PTR)v);return TRUE;}
    if(!v)return DefWindowProcW(w,message,wp,lp);
    switch(message) {
    case WM_GETDLGCODE:return DLGC_WANTALLKEYS|DLGC_WANTCHARS|DLGC_WANTARROWS;
    case TERMINAL_CHANGED:if((LONG)wp==v->generation) {InterlockedExchange(&v->pending,0);InvalidateRect(w,NULL,FALSE);PostMessageW(GetParent(w),WM_APP+202,0,0);}return 0;
    case WM_SIZE:resize(w,v);return 0;
    case WM_SETFOCUS:case WM_KILLFOCUS:InvalidateRect(w,NULL,FALSE);return 0;
    case WM_MOUSEWHEEL:axyne_terminal_scroll(v->terminal,GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*3);v->first=v->last=-1;InvalidateRect(w,NULL,FALSE);return 0;
    case WM_LBUTTONDOWN:SetFocus(w);SetCapture(w);v->first=v->last=index_at(w,v,GET_X_LPARAM(lp),GET_Y_LPARAM(lp));InvalidateRect(w,NULL,FALSE);return 0;
    case WM_MOUSEMOVE:if(wp&MK_LBUTTON){v->last=index_at(w,v,GET_X_LPARAM(lp),GET_Y_LPARAM(lp));InvalidateRect(w,NULL,FALSE);}return 0;
    case WM_LBUTTONUP:ReleaseCapture();return 0;
    case WM_COPY:copy(w,v);return 0;case WM_PASTE:paste(w,v);return 0;
    case EM_SETSEL: { AxyneTerminalSnapshot s={0}; if(axyne_terminal_snapshot(v->terminal,&s,NULL)==AXYNE_STATUS_OK) { v->first=0;v->last=s.rows*s.columns-1;axyne_terminal_snapshot_free(&s);InvalidateRect(w,NULL,FALSE); }return 0; }
    case WM_KEYDOWN:case WM_SYSKEYDOWN: {
        int ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0,shift=(GetKeyState(VK_SHIFT)&0x8000)!=0,alt=(GetKeyState(VK_MENU)&0x8000)!=0;
        int altgr=ctrl&&(GetKeyState(VK_RMENU)&0x8000);unsigned mods=(ctrl?4:0)|(shift?1:0)|(alt?2:0);
        if(alt&&wp==VK_F4)return DefWindowProcW(w,message,wp,lp);
        if(ctrl&&shift&&!alt&&(wp=='C'||wp=='V')){if(wp=='C')copy(w,v);else paste(w,v);return 0;}
        AxyneTerminalKey key=0;switch(wp){case VK_RETURN:key=AXYNE_TERMINAL_ENTER;break;case VK_TAB:key=AXYNE_TERMINAL_TAB;break;case VK_BACK:key=AXYNE_TERMINAL_BACKSPACE;break;case VK_ESCAPE:key=AXYNE_TERMINAL_ESCAPE;break;case VK_UP:key=AXYNE_TERMINAL_UP;break;case VK_DOWN:key=AXYNE_TERMINAL_DOWN;break;case VK_LEFT:key=AXYNE_TERMINAL_LEFT;break;case VK_RIGHT:key=AXYNE_TERMINAL_RIGHT;break;case VK_INSERT:key=AXYNE_TERMINAL_INSERT;break;case VK_DELETE:key=AXYNE_TERMINAL_DELETE;break;case VK_HOME:key=AXYNE_TERMINAL_HOME;break;case VK_END:key=AXYNE_TERMINAL_END;break;case VK_PRIOR:key=AXYNE_TERMINAL_PAGEUP;break;case VK_NEXT:key=AXYNE_TERMINAL_PAGEDOWN;break;}
        if(wp>=VK_F1&&wp<=VK_F24)key=(AxyneTerminalKey)(AXYNE_TERMINAL_F1+wp-VK_F1);
        if(key)axyne_terminal_key(v->terminal,key,mods,NULL);
        else if(ctrl&&!altgr&&wp>='A'&&wp<='Z')axyne_terminal_unichar(v->terminal,(uint32_t)(wp-'A'+'a'),mods,NULL);
        else return DefWindowProcW(w,message,wp,lp);
        v->first=v->last=-1;InvalidateRect(w,NULL,FALSE);return 0;
    }
    case WM_CHAR:case WM_SYSCHAR:if(wp>=32) {unsigned mods=message==WM_SYSCHAR&&!(GetKeyState(VK_RMENU)&0x8000)?AXYNE_TERMINAL_ALT:0;unicode(v,(wchar_t)wp,mods);}return 0;
    case WM_UNICHAR:if(wp==UNICODE_NOCHAR)return TRUE;axyne_terminal_unichar(v->terminal,(uint32_t)wp,0,NULL);return 0;
    case WM_IME_COMPOSITION:if(lp&GCS_RESULTSTR){HIMC imc=ImmGetContext(w);LONG bytes=ImmGetCompositionStringW(imc,GCS_RESULTSTR,NULL,0);wchar_t *text=bytes>0?malloc((size_t)bytes):NULL;if(text){ImmGetCompositionStringW(imc,GCS_RESULTSTR,text,(DWORD)bytes);for(LONG i=0;i<bytes/(LONG)sizeof(wchar_t);++i)unicode(v,text[i],0);free(text);}ImmReleaseContext(w,imc);return 0;}break;
    case WM_PAINT: {
        PAINTSTRUCT paint;HDC dc=BeginPaint(w,&paint);RECT bounds;GetClientRect(w,&bounds);HBRUSH base=CreateSolidBrush(color(v->background));FillRect(dc,&bounds,base);DeleteObject(base);
        AxyneTerminalSnapshot s={0};if(axyne_terminal_snapshot(v->terminal,&s,NULL)==AXYNE_STATUS_OK){int first=min(v->first,v->last),last=max(v->first,v->last);
            for(int r=0;r<s.rows;++r)for(int c=0;c<s.columns;++c){int index=r*s.columns+c;AxyneTerminalCell *cell=&s.cells[index];if(!cell->width)continue;
                uint32_t fg=cell->foreground,bg=cell->background;if(cell->attributes&AXYNE_TERMINAL_REVERSE){uint32_t temp=fg;fg=bg;bg=temp;}
                int selected=first>=0&&index>=first&&index<=last;SetTextColor(dc,selected?GetSysColor(COLOR_HIGHLIGHTTEXT):color(fg));SetBkColor(dc,selected?GetSysColor(COLOR_HIGHLIGHT):color(bg));
                unsigned font=(cell->attributes&AXYNE_TERMINAL_BOLD?1:0)|(cell->attributes&AXYNE_TERMINAL_ITALIC?2:0)|(cell->attributes&AXYNE_TERMINAL_UNDERLINE?4:0);HGDIOBJ old=SelectObject(dc,v->fonts[font]);wchar_t text[12];int n=cell_text(cell,text);RECT rect={18+c*v->width,14+r*v->height,18+(c+cell->width)*v->width,14+(r+1)*v->height};ExtTextOutW(dc,rect.left,rect.top,ETO_OPAQUE|ETO_CLIPPED,&rect,text,(UINT)n,NULL);SelectObject(dc,old);
            }
            if(s.cursor_visible&&GetFocus()==w){RECT cursor={18+s.cursor_column*v->width,14+(s.cursor_row+1)*v->height-2,18+(s.cursor_column+1)*v->width,14+(s.cursor_row+1)*v->height};HBRUSH b=CreateSolidBrush(color(v->foreground));FillRect(dc,&cursor,b);DeleteObject(b);}
            HIMC imc=ImmGetContext(w);COMPOSITIONFORM form={0};form.dwStyle=CFS_POINT;form.ptCurrentPos.x=18+s.cursor_column*v->width;form.ptCurrentPos.y=14+s.cursor_row*v->height;ImmSetCompositionWindow(imc,&form);ImmReleaseContext(w,imc);
            axyne_terminal_snapshot_free(&s);}
        EndPaint(w,&paint);return 0;
    }
    case WM_NCDESTROY:InterlockedExchangePointer((PVOID volatile *)&v->window,NULL);axyne_terminal_release(v->terminal);for(int i=0;i<8;++i)if(v->fonts[i])DeleteObject(v->fonts[i]);SetWindowLongPtrW(w,GWLP_USERDATA,0);free(v);return DefWindowProcW(w,message,wp,lp);
    }
    return DefWindowProcW(w,message,wp,lp);
}
HWND axyne_terminal_view_create(HWND parent,HINSTANCE instance) {
    WNDCLASSW cls={0};cls.lpfnWndProc=proc;cls.hInstance=instance;cls.lpszClassName=L"AxyneTerminalGrid";cls.hCursor=LoadCursor(NULL,IDC_IBEAM);RegisterClassW(&cls);
    HWND w=CreateWindowExW(0,cls.lpszClassName,L"",WS_CHILD|WS_TABSTOP,0,0,0,0,parent,NULL,instance,NULL);
    if(w)axyne_terminal_view_theme(w,(HFONT)GetStockObject(SYSTEM_FIXED_FONT),0xd4d4d4,0x181a1f);return w;
}
AxyneStatus axyne_terminal_view_start(HWND w,const AxyneTerminalSpec *spec,AxyneError *error) {TerminalView *v=view(w);if(!v)return AXYNE_STATUS_INVALID_ARGUMENT;AxyneTerminalSpec owned=*spec;owned.on_changed=changed;owned.on_exit=exited;owned.user_data=v;return axyne_terminal_start(v->terminal,&owned,error);}
void axyne_terminal_view_stop(HWND w){TerminalView *v=view(w);if(v)axyne_terminal_terminate(v->terminal,NULL);}
void axyne_terminal_view_clear(HWND w){TerminalView *v=view(w);if(!v)return;v->first=v->last=-1;axyne_terminal_clear(v->terminal);InvalidateRect(w,NULL,FALSE);}
int axyne_terminal_view_running(HWND w){TerminalView *v=view(w);return v&&axyne_terminal_running(v->terminal);}
AxyneTerminal *axyne_terminal_view_model(HWND w){TerminalView *v=view(w);return v?v->terminal:NULL;}
void axyne_terminal_view_theme(HWND w,HFONT font,uint32_t fg,uint32_t bg){TerminalView *v=view(w);if(!v)return;LOGFONTW lf={0};GetObjectW(font,sizeof lf,&lf);for(int i=0;i<8;++i){if(v->fonts[i])DeleteObject(v->fonts[i]);lf.lfWeight=i&1?FW_BOLD:FW_NORMAL;lf.lfItalic=(BYTE)((i&2)!=0);lf.lfUnderline=(BYTE)((i&4)!=0);v->fonts[i]=CreateFontIndirectW(&lf);}HDC dc=GetDC(w);HGDIOBJ old=SelectObject(dc,v->fonts[0]);TEXTMETRICW metrics;GetTextMetricsW(dc,&metrics);v->width=max(1,metrics.tmAveCharWidth);v->height=max(1,metrics.tmHeight+7);SelectObject(dc,old);ReleaseDC(w,dc);v->foreground=fg;v->background=bg;axyne_terminal_set_colors(v->terminal,fg,bg);resize(w,v);InvalidateRect(w,NULL,FALSE);}
