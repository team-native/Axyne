#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <wincodec.h>
#include <objbase.h>
/* AlphaBlend is declared by wingdi.h through windows.h. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "axyne/release_ui.h"
#include "axyne/release_notes.h"

/* A small GDI owner-drawn surface keeps the release browser independent of
 * the editor's child-control layout.  Network and persistence calls run on
 * detached jobs; the UI only observes them through a timer. */
enum { RW_TIMER = 7101, RW_TOAST_TIMER = 7102, RW_WINDOW = 1, RW_TOAST = 2, RW_RAW = 3 };
enum { C_BG=0x141713, C_PANEL=0x1a1716, C_SIDEBAR=0x1a1716,
       C_BORDER=0x37312e, C_TEXT=0xcec8c4, C_MUTED=0x9b918b,
       C_ACCENT=0x553d2d, C_ACCENT_TEXT=0xfbcce2 };
typedef struct RwJob {
    volatile LONG refs, done, cancelled;
    int kind, enabled;
    AxyneReleaseCatalog catalog;
    AxyneStatus status;
    AxyneError error;
} RwJob;
typedef struct RwState {
    HWND owner;
    int kind, filter, selected, scroll, toast_mode, show_after;
    int loading, failed, checked, alive;
    HWND raw_edit;
    char *raw_notes;
    RwJob *job;
    AxyneReleaseCatalog catalog;
} RwState;
static const wchar_t *RW_CLASS=L"AxyneReleaseWindow";
static const wchar_t *RT_CLASS=L"AxyneReleaseToast";
static HINSTANCE rw_instance;
static HBITMAP rw_assets[5];

static COLORREF rgb(unsigned c){ return RGB(c&255,(c>>8)&255,(c>>16)&255); }
static void rw_release(RwJob*j){
    if(InterlockedDecrement(&j->refs)==0){ axyne_release_catalog_clear(&j->catalog); free(j); }
}
static DWORD WINAPI rw_worker(void *arg){
    RwJob*j=(RwJob*)arg;
    if(j->kind==0) j->status=axyne_release_fetch(NULL,NULL,&j->catalog,&j->error);
    else if(j->kind==1) j->status=axyne_release_suppress(NULL,j->catalog.latest_version,&j->error);
    else j->status=axyne_release_set_show_after_update(NULL,j->enabled,&j->error);
    InterlockedExchange(&j->done,1); rw_release(j); return 0;
}
static RwJob *rw_start(int kind,int enabled,const char *version){
    RwJob*j=(RwJob*)calloc(1,sizeof(*j)); HANDLE t;
    if(!j)return NULL; j->refs=2; j->kind=kind; j->enabled=enabled;
    if(version){ j->catalog.latest_version=(char*)malloc(strlen(version)+1); if(!j->catalog.latest_version){free(j);return NULL;}strcpy(j->catalog.latest_version,version); }
    t=CreateThread(NULL,0,rw_worker,j,0,NULL);
    if(!t){ rw_release(j);rw_release(j);return NULL; } CloseHandle(t); return j;
}
static void rw_text(HDC dc,int x,int y,const wchar_t*s,int size,int weight,COLORREF color){
    HFONT f; HGDIOBJ old; LOGFONTW lf={0};lf.lfHeight=-size;lf.lfWeight=weight;wcscpy(lf.lfFaceName,L"Segoe UI");f=CreateFontIndirectW(&lf);old=SelectObject(dc,f);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);TextOutW(dc,x,y,s,(int)wcslen(s));SelectObject(dc,old);DeleteObject(f);
}
static int rw_text_width(HDC dc,const wchar_t*s,int size,int weight){
    HFONT f;HGDIOBJ old;LOGFONTW lf={0};SIZE extent={0};lf.lfHeight=-size;lf.lfWeight=weight;wcscpy(lf.lfFaceName,L"Segoe UI");f=CreateFontIndirectW(&lf);if(!f)return 0;old=SelectObject(dc,f);GetTextExtentPoint32W(dc,s,(int)wcslen(s),&extent);SelectObject(dc,old);DeleteObject(f);return extent.cx;
}
static void rw_text_ellipsized(HDC dc,const RECT*rect,const wchar_t*s,int size,int weight,COLORREF color){
    HFONT f;HGDIOBJ old;LOGFONTW lf={0};RECT bounds=*rect;if(!s)return;lf.lfHeight=-size;lf.lfWeight=weight;wcscpy(lf.lfFaceName,L"Segoe UI");f=CreateFontIndirectW(&lf);if(!f)return;old=SelectObject(dc,f);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);DrawTextW(dc,s,-1,&bounds,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);SelectObject(dc,old);DeleteObject(f);
}
static wchar_t *rw_wide(const char*s){int n;if(!s)return NULL;n=MultiByteToWideChar(CP_UTF8,0,s,-1,NULL,0);if(!n)return NULL;wchar_t*w=(wchar_t*)calloc((size_t)n,sizeof(wchar_t));if(w)MultiByteToWideChar(CP_UTF8,0,s,-1,w,n);return w;}
static void fill(HDC d,int x,int y,int w,int h,unsigned c){RECT r={x,y,x+w,y+h};HBRUSH b=CreateSolidBrush(rgb(c));FillRect(d,&r,b);DeleteObject(b);}
static void rw_round_fill(HDC d,int x,int y,int w,int h,int radius,unsigned c){HBRUSH b=CreateSolidBrush(rgb(c));HPEN p=CreatePen(PS_SOLID,1,rgb(c));HGDIOBJ old_b,old_p;if(!b||!p){if(b)DeleteObject(b);if(p)DeleteObject(p);return;}old_b=SelectObject(d,b);old_p=SelectObject(d,p);RoundRect(d,x,y,x+w,y+h,radius*2,radius*2);SelectObject(d,old_p);SelectObject(d,old_b);DeleteObject(p);DeleteObject(b);}
static HBITMAP rw_asset(int id){
    IWICImagingFactory*f=NULL;IWICBitmapDecoder*dec=NULL;IWICBitmapFrameDecode*frame=NULL;IWICFormatConverter*conv=NULL;IStream*stream=NULL;HGLOBAL hg;void*mem;HRSRC res;DWORD bytes;HRESULT hr;UINT w=0,h=0;BITMAPINFO bi={0};void*bits=NULL;HBITMAP bmp=NULL;
    if(id<4701||id>4704)return NULL;if(rw_assets[id-4700])return rw_assets[id-4700];
    res=FindResourceW(rw_instance,MAKEINTRESOURCEW(id),RT_RCDATA);if(!res)return NULL;bytes=SizeofResource(rw_instance,res);hg=GlobalAlloc(GMEM_MOVEABLE,bytes);if(!hg)return NULL;mem=GlobalLock(hg);if(!mem){GlobalFree(hg);return NULL;}memcpy(mem,LockResource(LoadResource(rw_instance,res)),bytes);GlobalUnlock(hg);
    if(FAILED(CreateStreamOnHGlobal(hg,TRUE,&stream))){GlobalFree(hg);return NULL;}
    HRESULT com=CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
    hr=CoCreateInstance(&CLSID_WICImagingFactory,NULL,CLSCTX_INPROC_SERVER,&IID_IWICImagingFactory,(void**)&f);
    if(SUCCEEDED(hr))hr=IWICImagingFactory_CreateDecoderFromStream(f,stream,NULL,WICDecodeMetadataCacheOnLoad,&dec);
    if(SUCCEEDED(hr))hr=IWICBitmapDecoder_GetFrame(dec,0,&frame);
    if(SUCCEEDED(hr))hr=IWICImagingFactory_CreateFormatConverter(f,&conv);
    if(SUCCEEDED(hr))hr=IWICFormatConverter_Initialize(conv,(IWICBitmapSource*)frame,&GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,NULL,0,WICBitmapPaletteTypeCustom);
    if(SUCCEEDED(hr))hr=IWICBitmapSource_GetSize((IWICBitmapSource*)conv,&w,&h);
    if(SUCCEEDED(hr)){bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=(LONG)w;bi.bmiHeader.biHeight=-(LONG)h;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;bmp=CreateDIBSection(NULL,&bi,DIB_RGB_COLORS,&bits,NULL,0);if(!bmp||FAILED(IWICBitmapSource_CopyPixels((IWICBitmapSource*)conv,NULL,w*4,w*h*4,(BYTE*)bits))){if(bmp)DeleteObject(bmp);bmp=NULL;}}
    if(conv)IWICFormatConverter_Release(conv);if(frame)IWICBitmapFrameDecode_Release(frame);if(dec)IWICBitmapDecoder_Release(dec);if(f)IWICImagingFactory_Release(f);if(stream)IStream_Release(stream);if(SUCCEEDED(com))CoUninitialize();
    if(bmp)rw_assets[id-4700]=bmp;return bmp;
}
static void rw_draw_asset(HDC dc,int id,int x,int y,int w,int h){HBITMAP b=rw_asset(id);if(b){BITMAP bm;HDC m=CreateCompatibleDC(dc);GetObject(b,sizeof(bm),&bm);SelectObject(m,b);AlphaBlend(dc,x,y,w,h,m,0,0,bm.bmWidth,bm.bmHeight, (BLENDFUNCTION){AC_SRC_OVER,0,255,AC_SRC_ALPHA});DeleteDC(m);}}
static const wchar_t *catname(int c){static const wchar_t*n[]={L"All",L"Feature",L"Improvement",L"Fix",L"Notice"};return n[c<0||c>4?0:c];}
static int count_category(const AxyneReleaseEntry*e,int c){return e?(int)axyne_release_category_count(e,(AxyneReleaseCategory)c):0;}
static int visible_index(RwState*s,int n){int k=-1;for(size_t i=0;i<s->catalog.count;i++){if(s->filter==0||count_category(&s->catalog.entries[i],s->filter)>0){if(++k==n)return(int)i;}}return -1;}
static void rw_paint(HWND w,RwState*s,HDC d){
    RECT rc;GetClientRect(w,&rc);int width=rc.right,height=rc.bottom;fill(d,0,0,width,height,C_BG);
    if(s->kind==RW_TOAST){
        wchar_t heading[256];const char*version_text=s->catalog.latest_version?s->catalog.latest_version:axyne_release_notes_version();wchar_t*version=rw_wide(version_text);if(s->loading)wcscpy(heading,L"Checking for updates…");else swprintf(heading,256,L"Axyne %s",version?version:L"");free(version);
        const char*message=s->loading?"Checking for updates…":s->failed?"Could not check for updates":s->catalog.message&&s->catalog.message[0]?s->catalog.message:s->catalog.update_available?"A new release is available.":"You are up to date.";wchar_t*detail=rw_wide(message);RECT detail_rect={54,34,width-20,54};
        fill(d,0,0,width,height,0x22201e);rw_draw_asset(d,4701,16,14,28,28);rw_text(d,54,16,heading,13,600,rgb(C_TEXT));rw_text_ellipsized(d,&detail_rect,detail,11,400,rgb(C_MUTED));free(detail);
        if(s->toast_mode==1){
            int secondary_x=width-308,primary_x=width-140;
            /* Color literals below use Win32 COLORREF byte order (0x00BBGGRR). */
            rw_round_fill(d,secondary_x,67,160,28,4,0x2b2623);rw_text(d,secondary_x+8,76,L"Don't show this version again",9,400,rgb(C_TEXT));
            rw_round_fill(d,primary_x,67,128,28,4,0xfa6bb4);rw_text(d,primary_x+10,76,L"View release notes",10,600,rgb(0xffffff));
        }
        rw_draw_asset(d,4704,width-26,12,10,10);return;
    }
    if(s->kind==RW_RAW){fill(d,0,0,width,32,0x171413);rw_text(d,12,8,L"CHANGELOG — Raw release notes",12,400,rgb(C_TEXT));rw_draw_asset(d,4702,width-28,11,10,10);return;}
    fill(d,0,0,width,32,0x171413);rw_draw_asset(d,4701,12,8,16,16);rw_text(d,38,8,L"Release notes — Axyne",12,400,rgb(C_TEXT));rw_draw_asset(d,4702,width-28,11,10,10);
    fill(d,0,32,220,height-118,C_SIDEBAR);rw_text(d,16,48,L"RELEASE HISTORY",11,600,rgb(C_MUTED));
    if(s->loading)rw_text(d,246,64,L"Loading release history…",14,400,rgb(C_MUTED));
    else if(s->failed)rw_text(d,246,64,L"Release history is unavailable.",14,400,rgb(C_MUTED));
    int row=0;
    for(int i=0;(i=visible_index(s,row))>=0;row++){
        int y=82+row*56-s->scroll;if(y<35||y>height-100)continue;
        if(i==s->selected)fill(d,8,y,204,52,0x2b2623);
        wchar_t *v=rw_wide(s->catalog.entries[i].version);if(v){if(strcmp(s->catalog.entries[i].version,axyne_release_notes_version())==0){int badge_x=20+rw_text_width(d,v,13,700)+8;if(badge_x<54)badge_x=54;if(badge_x>142)badge_x=142;RECT version_rect={20,y+3,badge_x-8,y+27};rw_text_ellipsized(d,&version_rect,v,13,700,rgb(C_TEXT));rw_round_fill(d,badge_x,y+5,58,18,9,0x552d3b);rw_text(d,badge_x+6,y+7,L"Installed",9,400,rgb(0xfbcce2));}else{RECT version_rect={20,y+3,200,y+27};rw_text_ellipsized(d,&version_rect,v,13,700,rgb(C_TEXT));}free(v);}
        const char*dt=s->catalog.entries[i].published_at;wchar_t*dw=rw_wide(dt);rw_text(d,20,y+30,dw?dw:L"Date unavailable",11,400,rgb(C_MUTED));free(dw);
    }
    fill(d,220,32,1,height-118,C_BORDER);
    AxyneReleaseEntry*e=s->selected>=0&&(size_t)s->selected<s->catalog.count?&s->catalog.entries[s->selected]:NULL;
    if(e){wchar_t*v=rw_wide(e->version);wchar_t title[256];swprintf(title,256,L"Axyne %s",v?v:L"");rw_text(d,252,58,title,26,700,rgb(C_TEXT));free(v);
      wchar_t*date=rw_wide(e->published_at);rw_text(d,252,94,date?date:L"Date unavailable",12,400,rgb(C_MUTED));free(date);
      for(int c=0;c<5;c++){int x=252+c*89;int n=count_category(e,c);fill(d,x,128,79,27,c==s->filter?C_ACCENT:0x211d1b);wchar_t label[64];swprintf(label,64,L"%s  %d",catname(c),n);rw_text(d,x+8,134,label,11,400,rgb(c==s->filter?C_ACCENT_TEXT:C_TEXT));}
      int y=174,shown=0;
      for(size_t i=0;i<e->change_count&&y<height-160;i++){AxyneReleaseChange*c=&e->changes[i];if(s->filter&&c->category!=s->filter)continue;wchar_t*t=rw_wide(c->title),*b=rw_wide(c->body);rw_text(d,252,y,catname(c->category),11,600,rgb(C_ACCENT_TEXT));rw_text(d,338,y,t?t:L"",13,600,rgb(C_TEXT));if(b)rw_text(d,338,y+22,b,12,400,rgb(C_MUTED));free(t);free(b);y+=58;shown++;}
      if(!shown)rw_text(d,252,178,L"No changes in this category.",13,400,rgb(C_MUTED));
    }
    fill(d,0,height-86,width,1,C_BORDER);fill(d,18,height-58,16,16,s->show_after?C_ACCENT:0x24211f);if(s->show_after)rw_draw_asset(d,4703,19,height-57,13,13);rw_text(d,42,height-61,L"Show release notes after update",12,400,rgb(C_TEXT));rw_text(d,width-285,height-61,L"View raw CHANGELOG",12,400,rgb(C_MUTED));fill(d,width-132,height-69,112,34,C_ACCENT);rw_text(d,width-106,height-60,L"Close",12,600,rgb(C_ACCENT_TEXT));
}
static void rw_close(HWND w){RwState*s=(RwState*)GetWindowLongPtrW(w,GWLP_USERDATA);if(!s)return;s->alive=0;if(s->job){InterlockedExchange(&s->job->cancelled,1);rw_release(s->job);s->job=NULL;}KillTimer(w,RW_TIMER);axyne_release_catalog_clear(&s->catalog);free(s->raw_notes);free(s);SetWindowLongPtrW(w,GWLP_USERDATA,0);}
static void rw_open_raw(HWND parent,const char*notes){RwState*s=(RwState*)calloc(1,sizeof(*s));if(!s)return;s->owner=GetWindow(parent,GW_OWNER);s->kind=RW_RAW;if(notes){size_t n=strlen(notes)+1;s->raw_notes=(char*)malloc(n);if(s->raw_notes)memcpy(s->raw_notes,notes,n);}HWND w=CreateWindowExW(WS_EX_TOOLWINDOW,RW_CLASS,L"CHANGELOG",WS_POPUP,CW_USEDEFAULT,CW_USEDEFAULT,780,600,parent,NULL,rw_instance,s);if(!w){free(s->raw_notes);free(s);return;}RECT r;GetWindowRect(parent,&r);SetWindowPos(w,NULL,r.left+70,r.top+40,780,600,SWP_NOZORDER);ShowWindow(w,SW_SHOW);SetFocus(s->raw_edit);}
static LRESULT CALLBACK rw_proc(HWND w,UINT m,WPARAM wp,LPARAM lp){RwState*s=(RwState*)GetWindowLongPtrW(w,GWLP_USERDATA);
 if(m==WM_NCCREATE){CREATESTRUCTW*c=(CREATESTRUCTW*)lp;s=(RwState*)c->lpCreateParams;SetWindowLongPtrW(w,GWLP_USERDATA,(LONG_PTR)s);return TRUE;}
 switch(m){case WM_CREATE:if(s&&s->kind==RW_RAW){s->raw_edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY,8,38,764,524,w,(HMENU)1,rw_instance,NULL);if(s->raw_edit){wchar_t*notes=rw_wide(s->raw_notes?s->raw_notes:"");SetWindowTextW(s->raw_edit,notes?notes:L"");free(notes);}}return 0;
 case WM_PAINT:{PAINTSTRUCT p;HDC d=BeginPaint(w,&p);rw_paint(w,s,d);EndPaint(w,&p);return 0;}
 case WM_CTLCOLOREDIT:if(s&&s->kind==RW_RAW){HDC d=(HDC)wp;static HBRUSH b;if(!b)b=CreateSolidBrush(rgb(0x171413));SetTextColor(d,rgb(C_TEXT));SetBkColor(d,rgb(0x171413));return(LRESULT)b;}break;
 case WM_TIMER:if(s&&s->job&&InterlockedCompareExchange(&s->job->done,0,0)){RwJob*j=s->job;s->job=NULL;if(j->kind==0){axyne_release_catalog_clear(&s->catalog);s->catalog=j->catalog;memset(&j->catalog,0,sizeof(j->catalog));s->failed=j->status!=AXYNE_STATUS_OK;s->loading=0;s->show_after=s->catalog.show_after_update;if(s->kind==RW_TOAST){if(s->catalog.update_available&&s->catalog.suppressed){rw_release(j);DestroyWindow(w);return 0;}s->toast_mode=s->catalog.update_available?1:0;SetTimer(w,RW_TOAST_TIMER,9000,NULL);}else s->selected=s->catalog.count?0:-1;}else if(j->kind==2)s->show_after=j->enabled;rw_release(j);InvalidateRect(w,NULL,FALSE);}else if(s&&wp==RW_TOAST_TIMER)DestroyWindow(w);return 0;
 case WM_LBUTTONUP:if(s){int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);RECT r;GetClientRect(w,&r);if(s->kind==RW_TOAST){if(y<28&&x>r.right-40){DestroyWindow(w);return 0;}if(y>=67&&y<95&&x>=r.right-140&&x<r.right-12){HWND owner=s->owner;DestroyWindow(w);axyne_release_ui_open(owner,0);return 0;}if(y>=67&&y<95&&x>=r.right-308&&x<r.right-148&&s->catalog.latest_version){RwJob*j=rw_start(1,0,s->catalog.latest_version);if(j)rw_release(j);DestroyWindow(w);return 0;}if(y>55){DestroyWindow(w);return 0;}}
 else if(s->kind==RW_RAW){if(y<32&&x>r.right-42)DestroyWindow(w);}
 else {if(y<32&&x>r.right-42){DestroyWindow(w);return 0;}if(x<220&&y>=82){int idx=(y-82+s->scroll)/56;int actual=visible_index(s,idx);if(actual>=0)s->selected=actual;}else if(y>=120&&y<160&&x>=252){s->filter=(x-252)/89;if(s->filter>4)s->filter=4;}else if(y>r.bottom-82&&x<260){s->show_after=!s->show_after;RwJob*j=rw_start(2,s->show_after,NULL);if(j){if(s->job)rw_release(s->job);s->job=j;}}else if(y>r.bottom-82&&x>r.right-145)DestroyWindow(w);else if(y>r.bottom-82&&x>r.right-310){AxyneReleaseEntry*entry=s->selected>=0&&(size_t)s->selected<s->catalog.count?&s->catalog.entries[s->selected]:NULL;rw_open_raw(w,entry?entry->notes:NULL);}}InvalidateRect(w,NULL,FALSE);}return 0;
 case WM_MOUSEWHEEL:if(s&&s->kind==RW_WINDOW){s->scroll-=GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*48;if(s->scroll<0)s->scroll=0;InvalidateRect(w,NULL,FALSE);}return 0;
 case WM_KEYDOWN:if(wp==VK_ESCAPE){DestroyWindow(w);return 0;}if(s&&s->kind==RW_WINDOW&&(wp==VK_UP||wp==VK_DOWN)){int n=visible_index(s,0),max=0;while(visible_index(s,max)>=0)max++;if(max){int cur=0;for(int i=0;i<max;i++)if(visible_index(s,i)==s->selected)cur=i;cur+=wp==VK_DOWN?1:-1;if(cur<0)cur=0;if(cur>=max)cur=max-1;s->selected=visible_index(s,cur);(void)n;InvalidateRect(w,NULL,FALSE);}return 0;}return DefWindowProcW(w,m,wp,lp);
 case WM_CLOSE:DestroyWindow(w);return 0;case WM_DESTROY:rw_close(w);return 0;default:return DefWindowProcW(w,m,wp,lp);}return DefWindowProcW(w,m,wp,lp);}
static void rw_register_classes(void){static int registered;WNDCLASSEXW c={0};if(registered)return;c.cbSize=sizeof(c);c.hInstance=rw_instance;c.lpfnWndProc=rw_proc;c.hCursor=LoadCursor(NULL,IDC_ARROW);c.hbrBackground=CreateSolidBrush(rgb(C_BG));c.lpszClassName=RW_CLASS;(void)RegisterClassExW(&c);c.lpszClassName=RT_CLASS;(void)RegisterClassExW(&c);registered=1;}
static BOOL CALLBACK rw_close_enum(HWND w,LPARAM param){
    wchar_t c[64];
    if(GetWindow(w,GW_OWNER)!=(HWND)param)return TRUE;
    if(GetClassNameW(w,c,64)>0&&(!wcscmp(c,RW_CLASS)||!wcscmp(c,RT_CLASS)))DestroyWindow(w);
    return TRUE;
}
void axyne_release_ui_open(void*owner,int check_only){
    HWND parent=(HWND)owner;rw_instance=(HINSTANCE)GetWindowLongPtrW(parent,GWLP_HINSTANCE);if(!rw_instance)rw_instance=GetModuleHandleW(NULL);
    rw_register_classes();
    RwState*s=(RwState*)calloc(1,sizeof(*s));if(!s)return;s->owner=parent;s->kind=check_only?RW_TOAST:RW_WINDOW;s->filter=0;s->selected=-1;s->loading=1;s->show_after=1;
    HWND w=CreateWindowExW(check_only?WS_EX_TOPMOST|WS_EX_TOOLWINDOW:WS_EX_TOOLWINDOW,check_only?RT_CLASS:RW_CLASS,check_only?L"Axyne update":L"Release notes — Axyne",WS_POPUP, CW_USEDEFAULT,CW_USEDEFAULT,check_only?380:960,check_only?108:640,parent,NULL,rw_instance,s);
    if(!w){free(s);return;}if(check_only){RECT wr;GetWindowRect(parent,&wr);SetWindowPos(w,HWND_TOPMOST,wr.right-400,wr.bottom-128,380,108,SWP_NOACTIVATE);}else{RECT wr;GetWindowRect(parent,&wr);SetWindowPos(w,NULL,wr.left+(wr.right-wr.left-960)/2,wr.top+(wr.bottom-wr.top-640)/2,960,640,SWP_NOZORDER);}
    ShowWindow(w,SW_SHOW);UpdateWindow(w);SetTimer(w,RW_TIMER,120,NULL);s->job=rw_start(0,0,NULL);if(!s->job){s->loading=0;s->failed=1;InvalidateRect(w,NULL,FALSE);}
}
void axyne_release_ui_close_owner(void*owner){EnumWindows(rw_close_enum,(LPARAM)owner);}
