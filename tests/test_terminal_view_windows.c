#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/features/ui/platform/windows/terminal_view_windows.h"
static void pump(void) {MSG m;while(PeekMessageW(&m,NULL,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(10);}
static int contains(HWND grid,const char *marker){AxyneTerminalSnapshot s={0};if(axyne_terminal_snapshot(axyne_terminal_view_model(grid),&s,NULL)!=AXYNE_STATUS_OK)return 0;char buffer[32768];size_t n=0;for(int i=0;i<s.rows*s.columns&&n+1<sizeof buffer;++i){uint32_t c=s.cells[i].chars[0];buffer[n++]=c&&c<128?(char)c:' ';}buffer[n]=0;int found=strstr(buffer,marker)!=NULL;axyne_terminal_snapshot_free(&s);return found;}
static void await_marker(HWND grid,const char *marker){ULONGLONG end=GetTickCount64()+5000;while(!contains(grid,marker)&&GetTickCount64()<end)pump();if(!contains(grid,marker)){fprintf(stderr,"Native grid missing marker %s, running=%d\n",marker,axyne_terminal_view_running(grid));DestroyWindow(GetParent(grid));exit(1);}}
static void type(HWND grid,const wchar_t *text){for(;*text;++text)SendMessageW(grid,WM_CHAR,*text,0);SendMessageW(grid,WM_KEYDOWN,VK_RETURN,0);}
int main(void){
    HINSTANCE instance=GetModuleHandleW(NULL);WNDCLASSW cls={0};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=instance;cls.lpszClassName=L"AxyneTerminalTest";RegisterClassW(&cls);
    HWND parent=CreateWindowW(cls.lpszClassName,L"",WS_OVERLAPPEDWINDOW,0,0,800,400,NULL,NULL,instance,NULL);assert(parent);
    HWND grid=axyne_terminal_view_create(parent,instance);assert(grid);MoveWindow(grid,0,0,760,300,TRUE);ShowWindow(grid,SW_SHOW);SetFocus(grid);
    wchar_t class_name[64];GetClassNameW(grid,class_name,64);assert(wcscmp(class_name,L"AxyneTerminalGrid")==0);assert(GetWindow(grid,GW_CHILD)==NULL);
    const char *sample="\033[31mANSI\033[0m \xe4\xb8\xad";AxyneTerminal *terminal=axyne_terminal_view_model(grid);axyne_terminal_feed(terminal,sample,strlen(sample));
    InvalidateRect(grid,NULL,TRUE);UpdateWindow(grid);AxyneTerminalSnapshot s={0};assert(axyne_terminal_snapshot(terminal,&s,NULL)==AXYNE_STATUS_OK);assert(s.cells[0].chars[0]=='A'&&s.cells[5].chars[0]==0x4e2d&&s.cells[6].width==0);axyne_terminal_snapshot_free(&s);axyne_terminal_clear(terminal);
    char executable[MAX_PATH];UINT n=GetSystemDirectoryA(executable,MAX_PATH);assert(n>0&&n+9<MAX_PATH);strcat(executable,"\\cmd.exe");const char *args[]={"/Q"};AxyneTerminalSpec spec={0};spec.executable=executable;spec.arguments=args;spec.argument_count=1;AxyneError error={0};assert(axyne_terminal_view_start(grid,&spec,&error)==AXYNE_STATUS_OK);
    await_marker(grid,">");type(grid,L"set AXYNE_PREFIX=NATIVE_");type(grid,L"echo %AXYNE_PREFIX%INPUT");await_marker(grid,"NATIVE_INPUT");
    type(grid,L"echo \x4e2d");for(int i=0;i<20;++i)pump();assert(axyne_terminal_snapshot(terminal,&s,NULL)==AXYNE_STATUS_OK);int chinese=0;for(int i=0;i<s.rows*s.columns;++i)chinese+=s.cells[i].chars[0]==0x4e2d;assert(chinese>=1);axyne_terminal_snapshot_free(&s);
    type(grid,L"ping -n 20 127.0.0.1");for(int i=0;i<20;++i)pump();BYTE previous[256],keys[256];GetKeyboardState(previous);memcpy(keys,previous,sizeof keys);keys[VK_CONTROL]=0x80;SetKeyboardState(keys);SendMessageW(grid,WM_KEYDOWN,'C',0);SetKeyboardState(previous);type(grid,L"echo %AXYNE_PREFIX%INTERRUPT");await_marker(grid,"NATIVE_INTERRUPT");assert(axyne_terminal_view_running(grid));
    MoveWindow(grid,0,0,640,260,TRUE);assert(axyne_terminal_view_model(grid)==terminal);SendMessageW(grid,EM_SETSEL,0,-1);
    ULONGLONG start=GetTickCount64();DestroyWindow(parent);assert(GetTickCount64()-start<3000);for(int i=0;i<10;++i)pump();puts("Windows native terminal keyboard/render/cleanup tests passed");return 0;
}
