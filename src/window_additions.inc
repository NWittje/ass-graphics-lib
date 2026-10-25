// Included in application.cpp: Window is intentionally private to that module.
namespace ag {
bool context_request(Object *source,LPARAM l) {
    auto receiver=source;
    if(!receiver->context_callback) {
        auto root=GetAncestor(source->hwnd,GA_ROOT);
        for(auto candidate:source->context->objects)if(candidate->hwnd==root && candidate->context_callback){receiver=candidate;break;}
    }
    if(!receiver->context_callback)return false;
    POINT pt{SHORT(LOWORD(l)),SHORT(HIWORD(l))};bool keyboard=pt.x==-1 && pt.y==-1;
    if(keyboard){RECT r{};GetClientRect(source->hwnd,&r);pt={(r.right-r.left)/2,(r.bottom-r.top)/2};}
    else ScreenToClient(source->hwnd,&pt);
    float ratio=96.f/dpi(source->hwnd);ag_context_request request{sizeof(request),1,source,{pt.x*ratio,pt.y*ratio},uint32_t(keyboard)};
    receiver->context_callback(receiver->context_user,&request);return true;
}
static ag_monitor_info monitor_info(HMONITOR h) {
    MONITORINFOEXW m{};m.cbSize=sizeof(m);wincheck(GetMonitorInfoW(h,&m),L"Monitor information unavailable");
    ag_monitor_info out{sizeof(out),1};wcscpy_s(out.device,m.szDevice);wcscpy_s(out.identity,m.szDevice);
    out.left=m.rcMonitor.left;out.top=m.rcMonitor.top;out.right=m.rcMonitor.right;out.bottom=m.rcMonitor.bottom;
    out.work_left=m.rcWork.left;out.work_top=m.rcWork.top;out.work_right=m.rcWork.right;out.work_bottom=m.rcWork.bottom;
    out.primary=(m.dwFlags&MONITORINFOF_PRIMARY)!=0;
    UINT32 pc{},mc{};
    if(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&pc,&mc)==ERROR_SUCCESS) {
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pc);std::vector<DISPLAYCONFIG_MODE_INFO> modes(mc);
        if(QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&pc,paths.data(),&mc,modes.data(),nullptr)==ERROR_SUCCESS)for(UINT32 i=0;i<pc;++i){
            auto &p=paths[i];DISPLAYCONFIG_SOURCE_DEVICE_NAME src{};src.header={DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,sizeof(src),p.sourceInfo.adapterId,p.sourceInfo.id};
            if(DisplayConfigGetDeviceInfo(&src.header)!=ERROR_SUCCESS || wcscmp(src.viewGdiDeviceName,m.szDevice))continue;
            DISPLAYCONFIG_TARGET_DEVICE_NAME target{};target.header={DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME,sizeof(target),p.targetInfo.adapterId,p.targetInfo.id};
            if(DisplayConfigGetDeviceInfo(&target.header)==ERROR_SUCCESS && target.monitorDevicePath[0])wcscpy_s(out.identity,target.monitorDevicePath);
        }
    }
    return out;
}
struct MonitorSearch {const wchar_t *id{};HMONITOR result{};};
static BOOL CALLBACK find_monitor(HMONITOR h,HDC,LPRECT,LPARAM data) noexcept {
    auto &s=*reinterpret_cast<MonitorSearch*>(data);
    try{auto m=monitor_info(h);if(!wcscmp(m.identity,s.id)||!wcscmp(m.device,s.id)){s.result=h;return FALSE;}}catch(...){}
    return TRUE;
}
static HMONITOR select_monitor(HWND window,const wchar_t *id) {
    MonitorSearch s{id};if(id&&id[0])EnumDisplayMonitors(nullptr,nullptr,find_monitor,reinterpret_cast<LPARAM>(&s));
    return s.result?s.result:MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST);
}
}
ag_result ag_context_requests(ag_handle h,ag_context_fn callback,void *user){return api([&]{auto p=object(h);p->ui();require(p->kind==Kind::Window||p->kind==Kind::Canvas||p->kind==Kind::Chart||p->kind==Kind::Scene||p->kind==Kind::Display,L"Context requests require a window or surface");p->context_callback=callback;p->context_user=user;});}
ag_result ag_dialog_create(ag_handle owner,const wchar_t *title,uint32_t w,uint32_t height,ag_event_fn callback,void *user,ag_handle *out){return api([&]{output(out);auto p=get<Window>(owner,Kind::Window);p->ui();require(IsWindow(p->hwnd)&&w&&height&&w<=16384&&height<=16384,L"Invalid dialog owner/dimensions");ag_handle h{};check(ag_window_create(p->context,title,int(w),int(height),callback,user,&h),L"Owned dialog creation failed");auto d=get<Window>(h,Kind::Window);d->dialog_owner=p->hwnd;SetWindowLongPtrW(d->hwnd,GWLP_HWNDPARENT,reinterpret_cast<LONG_PTR>(p->hwnd));SetWindowLongPtrW(d->hwnd,GWL_EXSTYLE,GetWindowLongPtrW(d->hwnd,GWL_EXSTYLE)|WS_EX_DLGMODALFRAME);RECT r{},o{};GetWindowRect(d->hwnd,&r);GetWindowRect(p->hwnd,&o);SetWindowPos(d->hwnd,nullptr,o.left+(o.right-o.left-r.right+r.left)/2,o.top+(o.bottom-o.top-r.bottom+r.top)/2,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);*out=h;});}
ag_result ag_monitors(ag_handle h,ag_monitor_fn callback,void *user){return api([&]{auto c=get<Context>(h,Kind::Context);c->ui();require(callback,L"Monitor callback required");struct State{ag_monitor_fn fn;void *user;HRESULT result{S_OK};}state{callback,user};EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR monitor,HDC,LPRECT,LPARAM data)->BOOL{auto &s=*reinterpret_cast<State*>(data);s.result=api([&]{auto info=monitor_info(monitor);s.fn(s.user,&info);});return SUCCEEDED(s.result);},reinterpret_cast<LPARAM>(&state));check(state.result,L"Monitor enumeration callback failed");});}
ag_result ag_window_placement_get(ag_handle h,ag_window_placement *out){return api([&]{auto p=get<Window>(h,Kind::Window);p->ui();require(out&&out->size==sizeof(*out)&&out->version==1,L"Unsupported placement descriptor");WINDOWPLACEMENT w{sizeof(w)};if(p->borderless)w=p->restored;else wincheck(GetWindowPlacement(p->hwnd,&w),L"Window placement unavailable");auto m=monitor_info(MonitorFromRect(&w.rcNormalPosition,MONITOR_DEFAULTTONEAREST));RECT r=w.rcNormalPosition;OffsetRect(&r,m.work_left-m.left,m.work_top-m.top);*out={sizeof(*out),1};wcscpy_s(out->monitor,m.identity);out->left=r.left;out->top=r.top;out->right=r.right;out->bottom=r.bottom;out->dpi=dpi(p->hwnd);out->mode=p->borderless?2:(w.showCmd==SW_SHOWMAXIMIZED || (w.flags&WPF_RESTORETOMAXIMIZED))?1:0;});}
ag_result ag_window_borderless(ag_handle h,uint32_t enabled,const wchar_t *id){return api([&]{
 auto p=get<Window>(h,Kind::Window);p->ui();require(enabled<=1 && (!id||wcsnlen_s(id,128)<128),L"Invalid borderless monitor");
 // Visibility and enablement belong to the live window/dialog lifecycle, not
 // the frame captured at fullscreen entry (which may occur inside an owned dialog).
 const DWORD live=DWORD(GetWindowLongPtrW(p->hwnd,GWL_STYLE));
 constexpr DWORD dynamic=WS_DISABLED|WS_VISIBLE;
 const bool visible=IsWindowVisible(p->hwnd)!=FALSE;
 if(enabled){if(!p->borderless){p->restored.length=sizeof(p->restored);wincheck(GetWindowPlacement(p->hwnd,&p->restored),L"Restored placement unavailable");p->restored_style=live&~dynamic;p->restored_exstyle=DWORD(GetWindowLongPtrW(p->hwnd,GWL_EXSTYLE));p->borderless=true;}
 auto m=monitor_info(select_monitor(p->hwnd,id));SetWindowLongPtrW(p->hwnd,GWL_STYLE,(p->restored_style&~(WS_OVERLAPPEDWINDOW|WS_HSCROLL|WS_VSCROLL))|WS_POPUP|(live&dynamic));SetWindowPos(p->hwnd,nullptr,m.left,m.top,m.right-m.left,m.bottom-m.top,SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
 }else if(p->borderless){p->borderless=false;SetWindowLongPtrW(p->hwnd,GWL_STYLE,p->restored_style|(live&dynamic));SetWindowLongPtrW(p->hwnd,GWL_EXSTYLE,p->restored_exstyle);wincheck(SetWindowPlacement(p->hwnd,&p->restored),L"Window restoration failed");SetWindowPos(p->hwnd,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);}
 if(!visible)ShowWindow(p->hwnd,SW_HIDE);
 });}
ag_result ag_window_placement_set(ag_handle h,const ag_window_placement *in){return api([&]{auto p=get<Window>(h,Kind::Window);p->ui();require(in&&in->size==sizeof(*in)&&in->version==1&&in->mode<=2&&in->dpi>=48&&in->dpi<=960&&wcsnlen_s(in->monitor,128)<128,L"Invalid placement descriptor");int64_t w=int64_t(in->right)-in->left,ht=int64_t(in->bottom)-in->top;require(w>0&&ht>0&&w<=65536&&ht<=65536,L"Invalid restored window bounds");MonitorSearch search{in->monitor};EnumDisplayMonitors(nullptr,nullptr,find_monitor,reinterpret_cast<LPARAM>(&search));bool missing=!search.result;auto m=monitor_info(search.result?search.result:select_monitor(p->hwnd,nullptr));check(ag_window_borderless(h,0,nullptr),L"Cannot restore window style");RECT r{in->left,in->top,in->right,in->bottom};if(missing||r.right<=m.work_left||r.left>=m.work_right||r.bottom<=m.work_top||r.top>=m.work_bottom){w=std::min<int64_t>(w,m.work_right-m.work_left);ht=std::min<int64_t>(ht,m.work_bottom-m.work_top);r={m.work_left,m.work_top,m.work_left+LONG(w),m.work_top+LONG(ht)};}bool visible=IsWindowVisible(p->hwnd)!=FALSE;WINDOWPLACEMENT out{sizeof(out)};out.showCmd=in->mode==1&&!missing?SW_SHOWMAXIMIZED:SW_SHOWNORMAL;OffsetRect(&r,m.left-m.work_left,m.top-m.work_top);out.rcNormalPosition=r;wincheck(SetWindowPlacement(p->hwnd,&out),L"Window placement restore failed");if(in->mode==2&&!missing)check(ag_window_borderless(h,1,in->monitor),L"Borderless restore failed");if(!visible)ShowWindow(p->hwnd,SW_HIDE);});}

ag_result ag_dialog_show(ag_handle h,uint32_t visible,uint32_t activate){return api([&]{auto p=get<Window>(h,Kind::Window);p->ui();require(p->dialog_owner && visible<=1 && activate<=1,L"Invalid owned dialog show");if(visible&&!p->owner_disabled){p->owner_disabled=IsWindowEnabled(p->dialog_owner)!=FALSE;if(p->owner_disabled)EnableWindow(p->dialog_owner,FALSE);}if(!visible&&p->owner_disabled){EnableWindow(p->dialog_owner,TRUE);p->owner_disabled=false;}ShowWindow(p->hwnd,visible?(activate?SW_SHOW:SW_SHOWNOACTIVATE):SW_HIDE);});}
