#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <d3d11.h>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"
#include "../Shared/ImageIO.h"
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"comdlg32.lib")
#pragma comment(lib,"comctl32.lib")
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
namespace {
HWND window;
ID3D11Device* device{};
ID3D11DeviceContext* context{};
IDXGISwapChain* swapchain{};
ID3D11RenderTargetView* target{};
std::vector<Color> palette;
int selected=0;
bool dirty=false, closing=false;
fs::path filename;
char hexValue[7]{};
std::string error;
float pickerColor[4]{0,0,0,1};
int pickerIndex=0;
bool pickerRequested=false;
RECT windowBeforePicker{};
bool restorePickerSize=false;
const wchar_t* extensions[]={L"gpl",L"json",L"act",L"txt",L"c"};
void syncHex() { snprintf(hexValue,sizeof(hexValue),"%06X",rgb(palette[selected])); }
void changed(Color c) { if(rgb(c)!=rgb(palette[selected])) {palette[selected]=c;dirty=true;} syncHex(); }
void renderTarget() {
    ID3D11Texture2D* buffer{};
    if(SUCCEEDED(swapchain->GetBuffer(0,IID_PPV_ARGS(&buffer)))) {
        device->CreateRenderTargetView(buffer,nullptr,&target); buffer->Release();
    }
}
void cleanup() {
    if(target) target->Release();
    if(swapchain) swapchain->Release();
    if(context) context->Release();
    if(device) device->Release();
}
bool save(bool saveAs) {
    try {
        fs::path output=filename;
        if(saveAs || output.empty()) {
            wchar_t path[32768]=L"palette.gpl";
            if(!filename.empty()) wcsncpy_s(path,filename.c_str(),_TRUNCATE);
            OPENFILENAMEW dialog{sizeof(dialog)}; dialog.hwndOwner=window;
            dialog.lpstrFilter=L"GIMP Palette (*.gpl)\0*.gpl\0JSON RGB (*.json)\0*.json\0Adobe Color Table (*.act)\0*.act\0Hex text (*.txt)\0*.txt\0C initializer (*.c)\0*.c\0\0";
            dialog.lpstrFile=path; dialog.nMaxFile=32768; dialog.nFilterIndex=1;
            for(int i=0;i<5;++i) if(lower(output.extension().wstring())==std::wstring(L".")+extensions[i]) dialog.nFilterIndex=i+1;
            dialog.lpstrDefExt=extensions[dialog.nFilterIndex-1];
            dialog.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
            if(!GetSaveFileNameW(&dialog)) { if(CommDlgExtendedError()) throw std::runtime_error("Save dialog failed."); return false; }
            output=path;
            auto expected=std::wstring(L".")+extensions[dialog.nFilterIndex-1];
            if(lower(output.extension().wstring())!=expected) {
                output.replace_extension(expected);
                if(fs::exists(output) && MessageBoxW(window,L"Replace the existing file?",L"Pal16Edit",MB_YESNO|MB_ICONQUESTION)!=IDYES) return false;
            }
        }
        savePalette({},output,palette); filename=output; dirty=false; return true;
    } catch(const std::exception& e) {error=e.what();return false;}
}
bool discard(bool exiting=false) {
    if(!dirty) return true;
    const TASKDIALOG_BUTTON buttons[]={
        {IDYES,exiting?L"保存して終了":L"保存して開く"},
        {IDNO,exiting?L"保存せずに終了":L"保存せずに開く"},
        {IDCANCEL,L"キャンセル（編集に戻る）"}
    };
    std::wstring content=L"「"+(filename.empty()?std::wstring(L"無題のパレット"):filename.filename().wstring())+
        L"」には、まだ保存していない変更があります。\n保存しない場合、今回の変更は失われます。";
    TASKDIALOGCONFIG dialog{sizeof(dialog)};
    dialog.hwndParent=window; dialog.pszWindowTitle=L"Pal16Edit — 未保存の変更";
    dialog.pszMainInstruction=exiting?L"変更を保存してから終了しますか？":L"別のファイルを開く前に、変更を保存しますか？";
    dialog.pszContent=content.c_str(); dialog.pszMainIcon=TD_WARNING_ICON;
    dialog.dwFlags=TDF_ALLOW_DIALOG_CANCELLATION|TDF_POSITION_RELATIVE_TO_WINDOW|TDF_SIZE_TO_CONTENT;
    dialog.cButtons=3; dialog.pButtons=buttons; dialog.nDefaultButton=IDCANCEL;
    int answer=IDCANCEL;
    if(FAILED(TaskDialogIndirect(&dialog,&answer,nullptr,nullptr))) return false;
    return answer==IDNO || (answer==IDYES && save(false));
}
std::vector<Color> read(fs::path path) {
    path=bmpPath(path);
    auto ext=lower(path.extension().wstring());
    if(ext==L".gpl" || ext==L".json") return loadPalette(path);
    if(ext==L".bmp" || ext==L".png") {
        auto colors=readImage(path).palette;
        if(colors.size()!=16) throw std::runtime_error("BMP/PNG must contain exactly 16 indexed palette colors. For RGB images, generate a palette with BmpPalette -makepal first.");
        return colors;
    }
    std::ifstream in(path,std::ios::binary);
    if(!in) throw std::runtime_error("Cannot open palette.");
    std::string text((std::istreambuf_iterator<char>(in)),{});
    std::vector<Color> colors;
    if(ext==L".act") {
        if(text.size()!=772 && text.size()!=768) throw std::runtime_error("Invalid ACT size.");
        if(text.size()==772 && ((unsigned char)text[768]*256+(unsigned char)text[769])!=16) throw std::runtime_error("ACT must declare 16 colors.");
        for(int i=0;i<16;++i) colors.push_back({(unsigned char)text[i*3],(unsigned char)text[i*3+1],(unsigned char)text[i*3+2]});
    } else {
        std::istringstream s(text); std::string token;
        if(ext==L".c") { for(char& c:text) if(c=='{'||c=='}'||c==',') c=' '; s.str(text); }
        while(s>>token) {
            if(token[0]=='#') token.erase(0,1);
            else if(token.starts_with("0x")||token.starts_with("0X")) token.erase(0,2);
            if(token.size()!=6 || token.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos) throw std::runtime_error("Expected six-digit RGB values.");
            colors.push_back(color(std::stoul(token,nullptr,16)));
        }
        if(colors.size()!=16) throw std::runtime_error("Palette must contain exactly 16 colors.");
    }
    return colors;
}
void open() {
    if(!discard()) return;
    wchar_t path[32768]{};
    OPENFILENAMEW dialog{sizeof(dialog)}; dialog.hwndOwner=window;
    dialog.lpstrFilter=L"Palette / BMP / PNG files\0*.gpl;*.json;*.act;*.txt;*.c;*.bmp;*.png\0All files\0*.*\0\0";
    dialog.lpstrDefExt=L"bmp";
    dialog.lpstrFile=path; dialog.nMaxFile=32768; dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(!GetOpenFileNameW(&dialog)) return;
    try {auto input=bmpPath(path);auto colors=read(input); palette=std::move(colors); selected=0; dirty=false;auto ext=lower(input.extension().wstring());filename=(ext==L".bmp"||ext==L".png")?fs::path{}:input; syncHex();}
    catch(const std::exception& e) {error=e.what();}
}
void pick() {
    pickerIndex=selected;
    const auto c=palette[pickerIndex];
    pickerColor[0]=c.r/255.f;pickerColor[1]=c.g/255.f;pickerColor[2]=c.b/255.f;
    // Keep the picker and its action buttons inside the application viewport.
    RECT client{};GetClientRect(window,&client);
    if(client.bottom<560 && !IsZoomed(window)) {
        restorePickerSize=GetWindowRect(window,&windowBeforePicker)!=FALSE;
        RECT bounds{0,0,(std::max)(client.right,560L),560};
        AdjustWindowRectEx(&bounds,static_cast<DWORD>(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,
                           static_cast<DWORD>(GetWindowLongPtrW(window,GWL_EXSTYLE)));
        SetWindowPos(window,nullptr,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,
                     SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    }
    pickerRequested=true;
}
void pickerDialog() {
    if(pickerRequested) {ImGui::OpenPopup("色を選択");pickerRequested=false;}
    ImGui::SetNextWindowSize(ImVec2(420,0),ImGuiCond_Appearing);
    if(ImGui::BeginPopupModal("色を選択",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("パレット番号: %d",pickerIndex);
        const auto original=palette[pickerIndex];
        float reference[]={original.r/255.f,original.g/255.f,original.b/255.f,1.f};
        ImGui::SetNextItemWidth(390);
        ImGui::ColorPicker4("##picker",pickerColor,
            ImGuiColorEditFlags_PickerHueBar|ImGuiColorEditFlags_DisplayRGB|
            ImGuiColorEditFlags_DisplayHex|ImGuiColorEditFlags_Uint8|ImGuiColorEditFlags_NoAlpha,reference);
        ImGui::TextUnformatted("「適用」で選択した色を反映します。");
        if(ImGui::Button("適用",ImVec2(120,0))) {
            selected=pickerIndex;
            changed({static_cast<unsigned char>(std::clamp(pickerColor[0],0.f,1.f)*255.f+0.5f),
                     static_cast<unsigned char>(std::clamp(pickerColor[1],0.f,1.f)*255.f+0.5f),
                     static_cast<unsigned char>(std::clamp(pickerColor[2],0.f,1.f)*255.f+0.5f)});
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if(ImGui::Button("キャンセル",ImVec2(120,0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if(restorePickerSize && !ImGui::IsPopupOpen("色を選択")) {
        SetWindowPos(window,nullptr,0,0,
                     windowBeforePicker.right-windowBeforePicker.left,
                     windowBeforePicker.bottom-windowBeforePicker.top,
                     SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        restorePickerSize=false;
    }
}
void copy() { char value[8]; snprintf(value,sizeof(value),"#%06X",rgb(palette[selected])); ImGui::SetClipboardText(value); }
void paste() {
    const char* clipboard=ImGui::GetClipboardText(); if(!clipboard) return;
    std::string value=clipboard;
    if(!value.empty() && value[0]=='#') value.erase(0,1);
    if(value.size()!=6 || value.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos) {error="Paste expects RRGGBB or #RRGGBB.";return;}
    changed(color(std::stoul(value,nullptr,16)));
}
void ui() {
    auto* viewport=ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos); ImGui::SetNextWindowSize(viewport->Size);
    ImGui::Begin("Pal16Edit",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
    if(ImGui::Button("Open")) open(); ImGui::SameLine();
    if(ImGui::Button("Save")) save(false); ImGui::SameLine();
    if(ImGui::Button("SaveAs")) save(true); ImGui::SameLine(0,24);
    if(ImGui::Button("Copy")) copy(); ImGui::SameLine();
    if(ImGui::Button("Paste")) paste(); ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth()-64);
    if(ImGui::Button("Quit")) closing=true;
    ImGui::Separator();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth()-150);
    for(int direction : {-1,1}) {
        ImGui::BeginDisabled(selected+direction<0 || selected+direction>=16);
        if(ImGui::Button(direction<0?"<<":">>",ImVec2(64,0))) {
            std::swap(palette[selected],palette[selected+direction]); selected+=direction;dirty=true;syncHex();
        }
        ImGui::EndDisabled(); if(direction==-1) ImGui::SameLine();
    }
    float width=ImGui::GetContentRegionAvail().x/16.0f;
    for(int i=0;i<16;++i) {
        ImGui::PushID(i); auto c=palette[i]; auto pos=ImGui::GetCursorScreenPos();
        if(ImGui::InvisibleButton("swatch",ImVec2(width,44))) {selected=i;syncHex();}
        bool doubleClick=ImGui::IsItemHovered()&&ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        auto* draw=ImGui::GetWindowDrawList();
        draw->AddRectFilled(pos,ImVec2(pos.x+width,pos.y+44),IM_COL32(c.r,c.g,c.b,255));
        char label[4];snprintf(label,sizeof(label),"%d",i);auto size=ImGui::CalcTextSize(label);
        draw->AddText(ImVec2(pos.x+(width-size.x)/2,pos.y+14),(c.r*299+c.g*587+c.b*114)>140000?IM_COL32(0,0,0,255):IM_COL32(255,255,255,255),label);
        if(selected==i) draw->AddRect(ImVec2(pos.x+1,pos.y+1),ImVec2(pos.x+width-1,pos.y+43),IM_COL32(255,65,65,255),0.0f,3.0f);
        if(doubleClick) {selected=i;syncHex();pick();}
        ImGui::PopID(); if(i!=15) ImGui::SameLine(0,0);
    }
    ImGui::Spacing(); auto c=palette[selected];
    ImGui::ColorButton("Selected color",ImVec4(c.r/255.f,c.g/255.f,c.b/255.f,1),ImGuiColorEditFlags_NoTooltip,ImVec2(100,100));
    if(ImGui::IsItemHovered()&&ImGui::IsMouseDoubleClicked(0)) pick();
    ImGui::SameLine(); ImGui::BeginGroup();
    ImGui::SetNextItemWidth(140);
    if(ImGui::InputText("Hex",hexValue,sizeof(hexValue),ImGuiInputTextFlags_CharsHexadecimal|ImGuiInputTextFlags_CharsUppercase)) {
        if(strlen(hexValue)==6) changed(color(std::stoul(hexValue,nullptr,16)));
    }
    int channels[]={c.r,c.g,c.b}; const char* labels[]={"R","G","B"};
    for(int i=0;i<3;++i) {
        ImGui::PushID(i);ImGui::SetNextItemWidth(100);
        bool edit=ImGui::InputInt(labels[i],&channels[i],1,16); ImGui::SameLine(); ImGui::SetNextItemWidth((std::max)(100.f,ImGui::GetContentRegionAvail().x-8));
        edit=ImGui::SliderInt("##channel",&channels[i],0,255)||edit;
        if(edit) {channels[i]=std::clamp(channels[i],0,255);changed({(unsigned char)channels[0],(unsigned char)channels[1],(unsigned char)channels[2]});}
        ImGui::PopID();
    }
    ImGui::EndGroup(); ImGui::Spacing();
    ImGui::TextDisabled("Index %d / 15   |   Double-click to choose a color",selected);
    pickerDialog();
    if(!error.empty()) ImGui::OpenPopup("Error");
    if(ImGui::BeginPopupModal("Error",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {ImGui::TextUnformatted(error.c_str());if(ImGui::Button("OK")){error.clear();ImGui::CloseCurrentPopup();}ImGui::EndPopup();}
    ImGui::End();
    std::wstring title=L"Pal16Edit - "+(filename.empty()?std::wstring(L"Untitled"):filename.filename().wstring())+(dirty?L" *":L"");SetWindowTextW(window,title.c_str());
}
LRESULT CALLBACK procedure(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(ImGui_ImplWin32_WndProcHandler(hwnd,msg,wp,lp)) return 1;
    switch(msg) {
    case WM_SIZE:
        if(swapchain && wp!=SIZE_MINIMIZED) {if(target){target->Release();target=nullptr;} if(SUCCEEDED(swapchain->ResizeBuffers(0,LOWORD(lp),HIWORD(lp),DXGI_FORMAT_UNKNOWN,0))) renderTarget();}return 0;
    case WM_GETMINMAXINFO: reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize={560,300};return 0;
    case WM_CLOSE: closing=true;return 0;
    case WM_DESTROY: PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR arguments,int show) {
    for(auto value:{0x0000ff,0xe2aa98,0x111111,0x333333,0x555555,0x770000,0x990000,0xaa2222,0xcc4444,0xee5555,0xee8888,0xff4444,0xcccccc,0xeeaaff,0xffbbbb,0xffffff}) palette.push_back(color(value));
    if(std::wstring(arguments)==L"--self-test") {
        try {
            auto directory=fs::current_path()/L"tests"/L"generated"/L"editor";
            fs::create_directories(directory);
            for(auto extension:extensions) {
                auto path=directory/(std::wstring(L"日本語 palette.")+extension);
                savePalette({},path,palette);
                auto loaded=read(path);
                if(loaded.size()!=palette.size()) return 2;
                for(size_t i=0;i<palette.size();++i) if(rgb(loaded[i])!=rgb(palette[i])) return 3;
                savePalette({},path,loaded); // Existing-file Save must work too.
            }
            // Allow writing but deny renaming: replacement must fail without
            // changing the old bytes, and its temporary file must be removed.
            auto protectedPath=directory/L"protected.json";
            savePalette({},protectedPath,palette);
            auto bytes=[](const fs::path& path) {
                std::ifstream in(path,std::ios::binary);
                return std::string(std::istreambuf_iterator<char>(in),{});
            };
            auto before=bytes(protectedPath);
            HANDLE locked=CreateFileW(protectedPath.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
            if(locked==INVALID_HANDLE_VALUE) return 8;
            bool failed=false;
            auto modified=palette;modified[0]=color(rgb(modified[0])^0xffffff);
            try {savePalette({},protectedPath,modified);} catch(const std::exception&) {failed=true;}
            CloseHandle(locked);
            if(!failed || bytes(protectedPath)!=before) return 9;
            for(const auto& entry:fs::directory_iterator(directory))
                if(entry.path().filename().wstring().starts_with(L".palette-")) return 10;
            auto invalidAct=directory/L"protected.act";
            savePalette({},invalidAct,palette);before=bytes(invalidAct);
            failed=false;
            try {savePalette({},invalidAct,std::vector<Color>(257));} catch(const std::exception&) {failed=true;}
            if(!failed || bytes(invalidAct)!=before) return 11;
            auto bad=directory/L"invalid.txt";
            {std::ofstream out(bad);out<<"#FF0000\n";}
            bool rejected=false;try {read(bad);}catch(const std::exception&) {rejected=true;}
            if(!rejected) return 4;
            const auto fixtures=fs::current_path()/L"tests"/L"fixtures";
            auto pngPalette=read(fixtures/L"palette16.png");
            auto bmpPalette=read(fixtures/L"palette16"); // Omitted extension means BMP.
            for(size_t i=0;i<16;++i) {
                const Color expected{static_cast<unsigned char>(i*17),static_cast<unsigned char>(255-i*17),static_cast<unsigned char>(i*7)};
                if(rgb(pngPalette[i])!=rgb(expected) || rgb(bmpPalette[i])!=rgb(expected)) return 6;
            }
            rejected=false;
            try {read(fixtures/L"rgb.png");}catch(const std::exception&) {rejected=true;}
            if(!rejected) return 7;
            return 0;
        } catch(...) {return 5;}
    }
    syncHex(); ImGui_ImplWin32_EnableDpiAwareness();
    WNDCLASSW cls{};cls.lpfnWndProc=procedure;cls.hInstance=instance;cls.lpszClassName=L"Pal16Edit";cls.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&cls);
    window=CreateWindowW(cls.lpszClassName,L"Pal16Edit",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,740,330,nullptr,nullptr,instance,nullptr);
    DXGI_SWAP_CHAIN_DESC desc{};desc.BufferCount=2;desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.OutputWindow=window;desc.SampleDesc.Count=1;desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    HRESULT result=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&swapchain,&device,nullptr,&context);
    if(FAILED(result)) result=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&swapchain,&device,nullptr,&context);
    if(FAILED(result)) {MessageBoxW(window,L"Cannot initialize DirectX 11.",L"Pal16Edit",MB_ICONERROR);cleanup();return 1;}
    renderTarget();if(!target){cleanup();return 1;}
    IMGUI_CHECKVERSION();ImGui::CreateContext();ImGui::GetIO().IniFilename=nullptr;ImGui::StyleColorsLight();
    // Neutral gray surrounds the swatches without adding a color cast.
    auto& colors=ImGui::GetStyle().Colors;
    colors[ImGuiCol_WindowBg]=ImVec4(0.50f,0.50f,0.50f,1.f);
    colors[ImGuiCol_PopupBg]=ImVec4(0.60f,0.60f,0.60f,1.f);
    colors[ImGuiCol_ChildBg]=ImVec4(0.50f,0.50f,0.50f,1.f);
    colors[ImGuiCol_FrameBg]=ImVec4(0.72f,0.72f,0.72f,1.f);
    colors[ImGuiCol_FrameBgHovered]=ImVec4(0.80f,0.80f,0.80f,1.f);
    colors[ImGuiCol_FrameBgActive]=ImVec4(0.85f,0.85f,0.85f,1.f);
    colors[ImGuiCol_TextDisabled]=ImVec4(0.15f,0.15f,0.15f,1.f);
    colors[ImGuiCol_Text]=ImVec4(0.06f,0.06f,0.06f,1.f);
    colors[ImGuiCol_Button]=ImVec4(0.76f,0.76f,0.76f,1.f);
    colors[ImGuiCol_ButtonHovered]=ImVec4(0.88f,0.88f,0.88f,1.f);
    colors[ImGuiCol_ButtonActive]=ImVec4(0.64f,0.64f,0.64f,1.f);
    colors[ImGuiCol_Border]=ImVec4(0.30f,0.30f,0.30f,1.f);
    colors[ImGuiCol_TitleBg]=ImVec4(0.70f,0.70f,0.70f,1.f);
    colors[ImGuiCol_TitleBgActive]=ImVec4(0.76f,0.76f,0.76f,1.f);
    ImGui::GetStyle().FrameBorderSize=1.f;
    wchar_t windowsDirectory[MAX_PATH]{};GetWindowsDirectoryW(windowsDirectory,MAX_PATH);
    auto fontPath=fs::path(windowsDirectory)/L"Fonts"/L"meiryo.ttc";
    ImGui::GetIO().Fonts->AddFontFromFileTTF(fontPath.string().c_str(),16.f,nullptr,ImGui::GetIO().Fonts->GetGlyphRangesJapanese());
    ImGui::GetStyle().FrameRounding=3; ImGui::GetStyle().WindowPadding=ImVec2(12,10);
    ImGui_ImplWin32_Init(window);ImGui_ImplDX11_Init(device,context);ShowWindow(window,show);
    bool done=false;
    while(!done) {
        MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);if(msg.message==WM_QUIT) done=true;}
        if(done) break;
        if(closing){
            closing=false;
            // Restore before showing save confirmation or reporting a save error.
            if(dirty && IsIconic(window)) ShowWindow(window,SW_RESTORE);
            if(discard(true)) break;
        }
        if(IsIconic(window)){WaitMessage();continue;}
        ImGui_ImplDX11_NewFrame();ImGui_ImplWin32_NewFrame();ImGui::NewFrame();ui();
        if(closing){closing=false;if(discard(true)) done=true;}
        ImGui::Render();const float background[]={0.50f,0.50f,0.50f,1};
        if(target){context->OMSetRenderTargets(1,&target,nullptr);context->ClearRenderTargetView(target,background);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());swapchain->Present(1,0);}
    }
    ImGui_ImplDX11_Shutdown();ImGui_ImplWin32_Shutdown();ImGui::DestroyContext();cleanup();DestroyWindow(window);UnregisterClassW(cls.lpszClassName,instance);return 0;
}
