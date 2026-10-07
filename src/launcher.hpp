// Original MFC launcher adapter. Included after executable verification
// helpers. The loader-lock bootstrap only swaps one identified import pointer.
// All file access, verification, configuration and UI work happens outside
// DllMain.
static char modernIni[MAX_PATH];
static bool modernIniPath() {
  if (modernIni[0])
    return true;
  DWORD n = GetModuleFileNameA(nullptr, modernIni, MAX_PATH);
  char *slash = n && n < MAX_PATH ? strrchr(modernIni, '\\') : nullptr;
  if (!slash || size_t(slash - modernIni) + 20 >= MAX_PATH) {
    modernIni[0] = 0;
    return false;
  }
  strcpy(slash + 1, "GuruminModern.ini");
  return true;
}
static void loadModernSettings() {
  modernSettings = {};
  if (!modernIniPath())
    return;
  auto read = [](const char *key, int fallback) {
    return int(
        GetPrivateProfileIntA("GuruminModern", key, fallback, modernIni));
  };
  gurumin::Resolution r{read("Width", 0), read("Height", 0)};
  if (gurumin::validResolution(r))
    modernSettings.resolution = r;
  int cap = read("FrameCap", 0);
  if (gurumin::validFrameCap(cap))
    modernSettings.frameCap = cap;
  modernSettings.freeCamera = read("FreeCamera", 0) != 0;
  modernSettings.cameraInteriorsOnly = read("CameraInteriorsOnly", 0) != 0;
  modernSettings.invertX = read("InvertCameraX", 1) != 0;
  modernSettings.invertY = read("InvertCameraY", 0) != 0;
  modernSettings.cameraYawSpeed =
      std::clamp(read("CameraYawSpeed", 120), 30, 360);
  modernSettings.cameraPitchSpeed =
      std::clamp(read("CameraPitchSpeed", 90), 30, 180);
  modernSettings.cameraDeadzone =
      std::clamp(read("CameraDeadzone", 8689), 0, 29490);
  int shadow = read("ShadowResolution", 256);
  if (gurumin::validShadowResolution(shadow))
    modernSettings.shadowResolution = shadow;
  int aa = read("AntiAliasing", 0);
  if (gurumin::validAntiAliasing(aa))
    modernSettings.antiAliasing = aa;
  modernSettings.uiFiltering = read("UIFiltering", 1) != 0;
  modernSettings.worldFiltering = read("WorldFiltering", 1) != 0;
}
static bool saveModernSettings(const gurumin::ModernSettings &s) {
  if (!modernIniPath())
    return false;
  char folder[MAX_PATH], temp[MAX_PATH];
  strcpy(folder, modernIni);
  *strrchr(folder, '\\') = 0;
  if (!GetTempFileNameA(folder, "gmo", 0, temp)) {
    log("Settings save temp creation error=%lu", (unsigned long)GetLastError());
    return false;
  }
  bool ok = GetFileAttributesA(modernIni) == INVALID_FILE_ATTRIBUTES ||
            CopyFileA(modernIni, temp, FALSE);
  if (!ok)
    log("Settings save copy error=%lu", (unsigned long)GetLastError());
  auto write = [&](const char *key, int value) {
    char text[24];
    snprintf(text, sizeof(text), "%d", value);
    if (ok) {
      ok = WritePrivateProfileStringA("GuruminModern", key, text, temp) != FALSE;
      if (!ok)
        log("Settings save key=%s error=%lu", key, (unsigned long)GetLastError());
    }
  };
  write("Width", s.resolution.width);
  write("Height", s.resolution.height);
  write("FrameCap", s.frameCap);
  write("FreeCamera", s.freeCamera);
  write("CameraInteriorsOnly", s.cameraInteriorsOnly);
  write("InvertCameraX", s.invertX);
  write("InvertCameraY", s.invertY);
  write("ShadowResolution", s.shadowResolution);
  write("AntiAliasing", s.antiAliasing);
  write("UIFiltering", s.uiFiltering);
  write("WorldFiltering", s.worldFiltering);
  // Keep unrelated settings, comments and advanced controller preferences.
  if (ok) {
    // The profile-cache flush returns zero even on success (Win32 contract).
    WritePrivateProfileStringA(nullptr, nullptr, nullptr, temp);
    HANDLE file = CreateFileA(temp, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ok = file != INVALID_HANDLE_VALUE;
    if (ok) {
      ok = FlushFileBuffers(file) != FALSE;
      CloseHandle(file);
    }
    if (!ok)
      log("Settings save file flush error=%lu", (unsigned long)GetLastError());
  }
  if (ok) {
    ok = MoveFileExA(temp, modernIni,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!ok)
      log("Settings save replace error=%lu", (unsigned long)GetLastError());
  }
  if (!ok)
    DeleteFileA(temp);
  return ok;
}
using CreateDialogFn = HWND(WINAPI *)(HINSTANCE, LPCDLGTEMPLATEA, HWND, DLGPROC,
                                      LPARAM);
using EndDialogFn = BOOL(WINAPI *)(HWND, INT_PTR);
static CreateDialogFn originalCreateDialog;
static EndDialogFn originalEndDialog;
static HWND launcherWindow, graphicsPage, resolutionCombo, capCombo,
    freeCameraCheck, interiorsOnlyCheck, invertXCheck, invertYCheck;
static WNDPROC launcherProc;
static WNDPROC graphicsProc;
static std::vector<gurumin::Resolution> launcherModes;
static gurumin::ModernSettings launcherDraft;
static bool pendingAcceptance, acceptedSettings;

static INT_PTR CALLBACK moreGraphicsProc(HWND window, UINT message,
                                         WPARAM wParam, LPARAM) {
  if (message == WM_INITDIALOG) {
    HFONT font = reinterpret_cast<HFONT>(
        SendMessageA(resolutionCombo, WM_GETFONT, 0, 0));
    auto control = [&](const char *cls, const char *text, DWORD style, int id,
                       int x, int y, int w, int h) {
      RECT r{x,y,x+w,y+h};
      MapDialogRect(window, &r);
      HWND c = CreateWindowExA(0, cls, text, WS_CHILD | WS_VISIBLE | style,
          r.left,r.top,r.right-r.left,r.bottom-r.top,window,
          reinterpret_cast<HMENU>(uintptr_t(id)),GetModuleHandleA(nullptr),nullptr);
      if(c) SendMessageA(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
      return c;
    };
    const char *labels[] = {"Shadow resolution", "Anti-aliasing", "UI textures",
                            "World textures"};
    for(int row=0; row<4; ++row) {
      control("STATIC",labels[row],0,6100+row,12,16+row*28,104,12);
      if(!control("COMBOBOX","",WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST,
                  6110+row,120,12+row*28,156,150)) {
        EndDialog(window,IDCANCEL); return TRUE;
      }
    }
    auto choice = [&](int id,const char *text,int value,int selected) {
      HWND c = GetDlgItem(window,id);
      LRESULT index=SendMessageA(c,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
      SendMessageA(c,CB_SETITEMDATA,index,value);
      if(value==selected) SendMessageA(c,CB_SETCURSEL,index,0);
    };
    for(int edge : {256,512,1024,2048}) {
      char label[40]; snprintf(label,sizeof(label),"%d x %d%s",edge,edge,
                              edge==256?" (original)":"");
      choice(6110,label,edge,launcherDraft.shadowResolution);
    }
    choice(6111,"Native",0,launcherDraft.antiAliasing);
    choice(6111,"FXAA - Low",1,launcherDraft.antiAliasing);
    choice(6111,"FXAA - High",2,launcherDraft.antiAliasing);
    for(int id : {6112,6113}) {
      bool selected=id==6112?launcherDraft.uiFiltering:launcherDraft.worldFiltering;
      choice(id,"Native filtering",1,selected);
      choice(id,"Nearest (pixelated)",0,selected);
    }
    control("STATIC","Changes apply when you accept the main settings window.",
            0,6120,12,128,264,24);
    control("BUTTON","Apply",WS_TABSTOP|BS_DEFPUSHBUTTON,IDOK,132,160,68,18);
    control("BUTTON","Cancel",WS_TABSTOP,IDCANCEL,208,160,68,18);
    return TRUE;
  }
  if(message==WM_CLOSE) { EndDialog(window,IDCANCEL); return TRUE; }
  if(message==WM_COMMAND && LOWORD(wParam)==IDCANCEL) {
    EndDialog(window,IDCANCEL); return TRUE;
  }
  if(message==WM_COMMAND && LOWORD(wParam)==IDOK) {
    int values[4];
    for(int row=0;row<4;++row) {
      HWND c=GetDlgItem(window,6110+row);
      LRESULT i=SendMessageA(c,CB_GETCURSEL,0,0);
      if(i==CB_ERR) return TRUE;
      values[row]=int(SendMessageA(c,CB_GETITEMDATA,i,0));
    }
    if(!gurumin::validShadowResolution(values[0]) ||
       !gurumin::validAntiAliasing(values[1]) ||
       values[2]<0 || values[2]>1 || values[3]<0 || values[3]>1) return TRUE;
    launcherDraft.shadowResolution=values[0];
    launcherDraft.antiAliasing=values[1];
    launcherDraft.uiFiltering=values[2]!=0;
    launcherDraft.worldFiltering=values[3]!=0;
    EndDialog(window,IDOK); return TRUE;
  }
  return FALSE;
}
static void openMoreGraphics() {
  // A standard native modal, with an empty template and controls created in
  // WM_INITDIALOG. No parallel settings store or acceptance path.
  const wchar_t title[]=L"Gurumin Enhanced Rendering - Graphics";
  std::vector<unsigned char> bytes(sizeof(DLGTEMPLATE)+4+sizeof(title),0);
  auto t=reinterpret_cast<DLGTEMPLATE*>(bytes.data());
  t->style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_CENTER;
  t->cx=288; t->cy=190;
  memcpy(bytes.data()+sizeof(DLGTEMPLATE)+4,title,sizeof(title));
  DialogBoxIndirectParamA(GetModuleHandleA(nullptr),t,launcherWindow,
                          moreGraphicsProc,0);
}
static LRESULT CALLBACK modernGraphicsProc(HWND window, UINT message,
                                           WPARAM wParam, LPARAM lParam) {
  if(message==WM_COMMAND && LOWORD(wParam)==6002 &&
     HIWORD(wParam)==BN_CLICKED)
    EnableWindow(interiorsOnlyCheck,
        SendMessageA(freeCameraCheck,BM_GETCHECK,0,0)==BST_CHECKED);
  if(message==WM_COMMAND && LOWORD(wParam)==6007 &&
     HIWORD(wParam)==BN_CLICKED) { openMoreGraphics(); return 0; }
  return CallWindowProcA(graphicsProc,window,message,wParam,lParam);
}

// Bound all directory walks to the loaded PE image. Verify the actual imported
// name, not merely a presumed slot or executable timestamp.
static void **importSlot(uintptr_t base, const char *name) {
  auto dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
      dos->e_lfanew > 0x100000)
    return nullptr;
  auto nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE ||
      nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
      nt->FileHeader.TimeDateStamp != 0x553e3cc7)
    return nullptr;
  size_t size = nt->OptionalHeader.SizeOfImage;
  auto valid = [&](size_t offset, size_t count) {
    return offset < size && count <= size - offset;
  };
  auto dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (!valid(dir.VirtualAddress, dir.Size))
    return nullptr;
  auto entries =
      reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + dir.VirtualAddress);
  for (size_t i = 0; i < dir.Size / sizeof(*entries) && entries[i].Name; ++i) {
    auto &e = entries[i];
    if (!valid(e.Name, 12) ||
        memcmp(reinterpret_cast<void *>(base + e.Name), "USER32.dll", 11))
      continue;
    if (!e.OriginalFirstThunk)
      return nullptr;
    for (size_t n = 0; n < 4096; ++n) {
      size_t source = e.OriginalFirstThunk + n * sizeof(IMAGE_THUNK_DATA);
      size_t dest = e.FirstThunk + n * sizeof(void *);
      if (!valid(source, sizeof(IMAGE_THUNK_DATA)) ||
          !valid(dest, sizeof(void *)))
        return nullptr;
      auto thunk = reinterpret_cast<IMAGE_THUNK_DATA *>(base + source);
      if (!thunk->u1.AddressOfData)
        break;
      if (IMAGE_SNAP_BY_ORDINAL(thunk->u1.Ordinal))
        continue;
      size_t text = thunk->u1.AddressOfData + 2, length = strlen(name) + 1;
      if (valid(text, length) &&
          !memcmp(reinterpret_cast<void *>(base + text), name, length))
        return reinterpret_cast<void **>(base + dest);
    }
  }
  return nullptr;
}
static bool replaceImport(void **slot, void *replacement) {
  if (!slot)
    return false;
  DWORD old;
  if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old))
    return false;
  *slot = replacement;
  DWORD unused;
  VirtualProtect(slot, sizeof(void *), old, &unused);
  return true;
}
static BOOL WINAPI launcherEndDialog(HWND window, INT_PTR result) {
  // Verified CDialog::OnOK validates first, then CDialog::EndDialog marks the
  // MFC loop ended BEFORE this import call (return RVA 0x17ed). Both Start and
  // Close use IDOK; Cancel uses IDCANCEL. Do not depend on Win32 EndDialog's
  // result for MFC's independently managed modal loop.
  bool commit = window == launcherWindow && pendingAcceptance &&
                result == IDOK &&
                uintptr_t(__builtin_return_address(0)) - gameBase == 0x17ed;
  BOOL nativeResult = originalEndDialog(window, result);
  if (commit) {
    acceptedSettings = true;
    pendingAcceptance = false;
    modernSettings = launcherDraft;
    bool saved = saveModernSettings(modernSettings);
    log("Launcher accepted resolution=%dx%d cap=%d freeCamera=%d saved=%d",
        modernSettings.resolution.width, modernSettings.resolution.height,
        modernSettings.frameCap, modernSettings.freeCamera, saved);
    if (!saved)
      MessageBoxA(nullptr,
                  "The modern settings could not be saved. Check that the game "
                  "folder is writable.",
                  "Gurumin Enhanced Rendering", MB_OK | MB_ICONERROR);
  }
  return nativeResult;
}
static LRESULT CALLBACK modernLauncherProc(HWND window, UINT message,
                                           WPARAM wParam, LPARAM lParam) {
  auto native = launcherProc;
  if (message == WM_COMMAND && (LOWORD(wParam) == 1 || LOWORD(wParam) == 3) &&
      HIWORD(wParam) == BN_CLICKED) {
    int index = int(SendMessageA(resolutionCombo, CB_GETCURSEL, 0, 0));
    int capIndex = int(SendMessageA(capCombo, CB_GETCURSEL, 0, 0));
    if (index < 0 || size_t(index) >= launcherModes.size() || capIndex < 0)
      return 0;
    launcherDraft.resolution = launcherModes[index];
    launcherDraft.frameCap =
        int(SendMessageA(capCombo, CB_GETITEMDATA, capIndex, 0));
    launcherDraft.freeCamera =
        SendMessageA(freeCameraCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    launcherDraft.cameraInteriorsOnly =
        SendMessageA(interiorsOnlyCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    launcherDraft.invertX =
        SendMessageA(invertXCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    launcherDraft.invertY =
        SendMessageA(invertYCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    pendingAcceptance = true;
    acceptedSettings = false;
    int oldNative = game<int>(0x1213568);
    SendMessageA(resolutionCombo, CB_SETCURSEL, 5, 0);
    game<int>(0x1213568) = 5;
    LRESULT result = CallWindowProcA(native, window, message, wParam, lParam);
    if (!acceptedSettings) {
      pendingAcceptance = false;
      game<int>(0x1213568) = oldNative;
      if (IsWindow(resolutionCombo))
        SendMessageA(resolutionCombo, CB_SETCURSEL, index, 0);
    }
    return result;
  }
  if (message == WM_CLOSE ||
      (message == WM_COMMAND && LOWORD(wParam) == IDCANCEL))
    pendingAcceptance = false;
  LRESULT result = CallWindowProcA(native, window, message, wParam, lParam);
  if (message == WM_COMMAND && LOWORD(wParam) == 1023 &&
      HIWORD(wParam) == BN_CLICKED) {
    // Native Reset remains responsible for its original controls. The added
    // controls are a draft until the original dialog accepts.
    launcherDraft = {};
    SendMessageA(capCombo, CB_SETCURSEL, 0, 0);
    SendMessageA(freeCameraCheck, BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessageA(interiorsOnlyCheck, BM_SETCHECK, BST_UNCHECKED, 0);
    EnableWindow(interiorsOnlyCheck, FALSE);
    SendMessageA(invertXCheck, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageA(invertYCheck, BM_SETCHECK, BST_UNCHECKED, 0);
  }
  if (message == WM_NCDESTROY)
    launcherWindow = nullptr;
  return result;
}
static BOOL CALLBACK findGraphicsPage(HWND control, LPARAM) {
  if (GetDlgCtrlID(control) == 1006) {
    HWND parent = GetParent(control);
    if (GetDlgItem(parent, 1068) && GetDlgItem(parent, 1005)) {
      if (graphicsPage && graphicsPage != parent) {
        graphicsPage = resolutionCombo = nullptr;
        return FALSE;
      }
      graphicsPage = parent;
      resolutionCombo = control;
    }
  }
  return TRUE;
}
static RECT dialogRect(HWND page, int x, int y, int width, int height) {
  RECT r{x, y, x + width, y + height};
  MapDialogRect(page, &r);
  return r;
}
static HWND modernControl(const char *cls, const char *text, DWORD style,
                          int id, int x, int y, int width, int height,
                          HFONT font) {
  RECT r = dialogRect(graphicsPage, x, y, width, height);
  HWND c = CreateWindowExA(0, cls, text, WS_CHILD | WS_VISIBLE | style, r.left,
                           r.top, r.right - r.left, r.bottom - r.top,
                           graphicsPage, reinterpret_cast<HMENU>(uintptr_t(id)),
                           GetModuleHandleA(nullptr), nullptr);
  if (c)
    SendMessageA(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  return c;
}
static void customizeLauncher(HWND window) {
  if (launcherWindow || !GetDlgItem(window, 1000) || !GetDlgItem(window, 3) ||
      !GetDlgItem(window, 1) || !GetDlgItem(window, 2))
    return;
  if (!verifyExecutable())
    return;
  gameBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
  graphicsPage = resolutionCombo = nullptr;
  EnumChildWindows(window, findGraphicsPage, 0);
  if (!graphicsPage || !resolutionCombo)
    return;
  void **endSlot = importSlot(gameBase, "EndDialog");
  if (!endSlot)
    return;
  if (!originalEndDialog) {
    originalEndDialog = reinterpret_cast<EndDialogFn>(*endSlot);
    if (!replaceImport(endSlot, reinterpret_cast<void *>(launcherEndDialog))) {
      originalEndDialog = nullptr;
      return;
    }
  }
  loadModernSettings();
  launcherDraft = modernSettings;
  int originalIndex = int(SendMessageA(resolutionCombo, CB_GETCURSEL, 0, 0));
  if (SendMessageA(resolutionCombo, CB_GETCOUNT, 0, 0) != 6)
    return;
  std::vector<std::string> originalLabels;
  for (int i = 0; i < 6; ++i) {
    LRESULT length = SendMessageA(resolutionCombo, CB_GETLBTEXTLEN, i, 0);
    if (length < 0 || length > 200)
      return;
    std::vector<char> text(size_t(length) + 1);
    SendMessageA(resolutionCombo, CB_GETLBTEXT, i,
                 reinterpret_cast<LPARAM>(text.data()));
    originalLabels.emplace_back(text.data());
  }
  RECT originalDrop{}, originalAudio{};
  SendMessageA(resolutionCombo, CB_GETDROPPEDCONTROLRECT, 0,
               reinterpret_cast<LPARAM>(&originalDrop));
  GetWindowRect(GetDlgItem(graphicsPage, 1069), &originalAudio);
  MapWindowPoints(nullptr, graphicsPage,
                  reinterpret_cast<POINT *>(&originalAudio), 2);
  auto rollback = [&]() {
    SendMessageA(resolutionCombo, CB_RESETCONTENT, 0, 0);
    for (auto &label : originalLabels)
      SendMessageA(resolutionCombo, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(label.c_str()));
    SendMessageA(resolutionCombo, CB_SETCURSEL, originalIndex, 0);
    RECT r{};
    GetWindowRect(resolutionCombo, &r);
    SetWindowPos(resolutionCombo, nullptr, 0, 0, r.right - r.left,
                 originalDrop.bottom - originalDrop.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    for (int id = 6000; id <= 6008; ++id)
      if (HWND control = GetDlgItem(graphicsPage, id))
        DestroyWindow(control);
    MoveWindow(GetDlgItem(graphicsPage, 1069), originalAudio.left,
               originalAudio.top, originalAudio.right - originalAudio.left,
               originalAudio.bottom - originalAudio.top, TRUE);
    capCombo = freeCameraCheck = interiorsOnlyCheck = invertXCheck = invertYCheck = nullptr;
  };
  static const gurumin::Resolution stock[] = {{800, 600},   {1024, 600},
                                              {1280, 720},  {1280, 960},
                                              {1680, 1050}, {1920, 1080}};
  gurumin::Resolution current = stock[std::clamp(originalIndex, 0, 5)];
  if (originalIndex == 5)
    current = {int(codeImmediate(0x20a582)), int(codeImmediate(0x20a58c))};
  if (gurumin::validResolution(modernSettings.resolution))
    current = modernSettings.resolution;
  launcherModes = gurumin::resolutionChoices(
      {GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)}, current);
  SendMessageA(resolutionCombo, CB_RESETCONTENT, 0, 0);
  int selected = 0;
  for (size_t i = 0; i < launcherModes.size(); ++i) {
    char label[48];
    snprintf(label, sizeof(label), "%d x %d", launcherModes[i].width,
             launcherModes[i].height);
    SendMessageA(resolutionCombo, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(label));
    if (launcherModes[i] == current)
      selected = int(i);
  }
  SendMessageA(resolutionCombo, CB_SETCURSEL, selected, 0);
  if (SendMessageA(resolutionCombo, CB_GETCOUNT, 0, 0) !=
      LRESULT(launcherModes.size())) {
    rollback();
    return;
  }
  RECT dropdown = dialogRect(graphicsPage, 0, 0, 175, 180);
  SendMessageA(resolutionCombo, CB_SETDROPPEDWIDTH, dropdown.right, 0);
  RECT existing;
  GetWindowRect(resolutionCombo, &existing);
  MapWindowPoints(nullptr, graphicsPage, reinterpret_cast<POINT *>(&existing),
                  2);
  SetWindowPos(resolutionCombo, nullptr, existing.left, existing.top,
               existing.right - existing.left, dropdown.bottom,
               SWP_NOZORDER | SWP_NOACTIVATE);
  auto font =
      reinterpret_cast<HFONT>(SendMessageA(resolutionCombo, WM_GETFONT, 0, 0));
  RECT audio = dialogRect(graphicsPage, 263, 18, 203, 108);
  MoveWindow(GetDlgItem(graphicsPage, 1069), audio.left, audio.top,
             audio.right - audio.left, audio.bottom - audio.top, TRUE);
  modernControl("BUTTON", "Display / Camera", BS_GROUPBOX, 6000, 263, 132, 203,
                107, font);
  modernControl("STATIC", "Frame cap", 0, 6004, 276, 150, 60, 10, font);
  capCombo =
      modernControl("COMBOBOX", "", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                    6001, 334, 146, 118, 150, font);
  freeCameraCheck = modernControl("BUTTON", "Free camera",
                                  WS_TABSTOP | BS_AUTOCHECKBOX, 6002, 276, 172,
                                  90, 12, font);
  interiorsOnlyCheck = modernControl("BUTTON", "Interiors only",
      WS_TABSTOP | BS_AUTOCHECKBOX, 6008, 370, 172, 84, 12, font);
  invertXCheck =
      modernControl("BUTTON", "Invert camera X", WS_TABSTOP | BS_AUTOCHECKBOX,
                    6006, 276, 191, 90, 12, font);
  invertYCheck =
      modernControl("BUTTON", "Invert camera Y", WS_TABSTOP | BS_AUTOCHECKBOX,
                    6003, 370, 191, 84, 12, font);
  HWND more = modernControl("BUTTON", "More graphics settings...",
                WS_TABSTOP,6007,276,211,178,18,font);
  if (!capCombo || !freeCameraCheck || !interiorsOnlyCheck || !invertXCheck || !invertYCheck || !more) {
    log("Launcher modern controls could not be created");
    rollback();
    return;
  }
  static const int caps[] = {0,   -1,  30,  60,  90,  120,
                             144, 165, 175, 180, 240, 360};
  std::vector<int> choices;
  choices.reserve(sizeof(caps) / sizeof(caps[0]) + 1);
  for (int cap : caps)
    choices.push_back(cap);
  if (std::find(choices.begin(), choices.end(), modernSettings.frameCap) ==
      choices.end())
    choices.push_back(modernSettings.frameCap);
  for (int cap : choices) {
    char label[48];
    if (cap == 0)
      strcpy(label, "Use VSync setting");
    else if (cap == -1)
      strcpy(label, "Uncapped");
    else
      snprintf(label, sizeof(label), "%d FPS", cap);
    LRESULT i = SendMessageA(capCombo, CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(label));
    SendMessageA(capCombo, CB_SETITEMDATA, i, cap);
    if (cap == modernSettings.frameCap)
      SendMessageA(capCombo, CB_SETCURSEL, i, 0);
  }
  SendMessageA(freeCameraCheck, BM_SETCHECK,
               modernSettings.freeCamera ? BST_CHECKED : BST_UNCHECKED, 0);
  SendMessageA(interiorsOnlyCheck, BM_SETCHECK,
               modernSettings.cameraInteriorsOnly ? BST_CHECKED : BST_UNCHECKED, 0);
  EnableWindow(interiorsOnlyCheck, modernSettings.freeCamera);
  SendMessageA(invertXCheck, BM_SETCHECK,
               modernSettings.invertX ? BST_CHECKED : BST_UNCHECKED, 0);
  SendMessageA(invertYCheck, BM_SETCHECK,
               modernSettings.invertY ? BST_CHECKED : BST_UNCHECKED, 0);
  launcherProc =
      reinterpret_cast<WNDPROC>(GetWindowLongPtrA(window, GWLP_WNDPROC));
  SetLastError(0);
  if (!SetWindowLongPtrA(window, GWLP_WNDPROC,
                         reinterpret_cast<LONG_PTR>(modernLauncherProc)) &&
      GetLastError()) {
    rollback();
    return;
  }
  launcherWindow = window;
  graphicsProc = reinterpret_cast<WNDPROC>(GetWindowLongPtrA(graphicsPage,GWLP_WNDPROC));
  SetLastError(0);
  if(!SetWindowLongPtrA(graphicsPage,GWLP_WNDPROC,
                        reinterpret_cast<LONG_PTR>(modernGraphicsProc)) && GetLastError()) {
    SetWindowLongPtrA(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(launcherProc));
    launcherWindow=nullptr;
    rollback(); return;
  }
  log("Launcher expanded resolution choices=%u selected=%dx%d",
      unsigned(launcherModes.size()), current.width, current.height);
}
static HWND WINAPI launcherCreateDialog(HINSTANCE module,
                                        LPCDLGTEMPLATEA dialog, HWND parent,
                                        DLGPROC proc, LPARAM param) {
  HWND window = originalCreateDialog(module, dialog, parent, proc, param);
  if (window)
    customizeLauncher(window);
  return window;
}
static void installLauncherBootstrap() {
  uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
  void **slot = importSlot(base, "CreateDialogIndirectParamA");
  if (slot) {
    originalCreateDialog = reinterpret_cast<CreateDialogFn>(*slot);
    replaceImport(slot, reinterpret_cast<void *>(launcherCreateDialog));
  }
}
