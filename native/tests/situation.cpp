#include "../src/main.cpp"

// Native integration test for the situation overview. It uses the real sampler,
// the real D2D canvas and an isolated config; no config of the user is changed.
int main(int argc, char **argv) {
  if (argc < 2 || argc > 3) return 2;
  SetProcessDPIAware();
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  std::wstring out = wide(argv[1]);
  CreateDirectoryW(out.c_str(), nullptr);
  json report = {{"checks", json::array()}};
  int failures = 0;
  auto check = [&](bool ok, const char *name) {
    report["checks"].push_back({{"name", name}, {"passed", ok}});
    if (!ok) ++failures;
  };
  {
    App app(GetModuleHandleW(nullptr));
    app.cfg.path = out + L"/situation-test-config.json";
    app.cfg.data["appearance"] = "dark";
    app.cfg.data["motion"] = true;
    writeFile(app.cfg.path, app.cfg.data.dump(2));
    app.create(L"ftop-situation-test-" + std::to_wstring(GetCurrentProcessId()));
    check(app.situation.hwnd != nullptr, "situation window created");
    app.dirty = true;
    app.draw(app.panel);
    auto seq = app.sampler.revision();
    double until = seconds() + 6;
    while (app.sampler.revision() == seq && seconds() < until) Sleep(50);
    app.snap = app.sampler.copy();
    app.showSituation();
    Sleep(1600);
    app.snap = app.sampler.copy();
    app.updateSituation(seconds(), true);
    check(app.sitHistory.size() > 0, "history has a sample");
    check(IsWindowVisible(app.situation.hwnd), "situation visible");
    RECT r{};
    GetWindowRect(app.situation.hwnd, &r);
    check(std::abs((r.right-r.left)/app.situation.dpi-840)<2 &&
          std::abs((r.bottom-r.top)/app.situation.dpi-500)<2, "situation uses the compact default window");
    app.draw(app.situation);
    check(app.situation.hits.size() > 10, "situation has hit targets");
    app.captureSituation(out + L"/situation-dark.bmp");
    report["window"] = {{"width", r.right - r.left}, {"height", r.bottom - r.top}};
    report["samples"] = app.sitHistory.size();
    report["hitTargets"] = app.situation.hits.size();
    if (auto *s = app.sitShown()) {
      report["cpu"] = s->cpu;
      report["mem"] = s->mem;
      report["entities"] = json::array();
      for (const auto &e : s->entities) report["entities"].push_back(utf8(e.key));
    }
    app.showSettings({80, 80});
    check(std::none_of(app.settings.hits.begin(), app.settings.hits.end(), [](const Hit &h) { return h.id == "palette"; }),
          "theme is absent from compact settings");
    app.captureWindow(app.settings, out + L"/settings-compact.bmp");
    app.action("more");
    bool settingsRow = false;
    for (const auto &h : app.settings.hits) if (h.id == "switchView") settingsRow = true;
    check(settingsRow, "settings exposes the shared view switch");
    check(std::any_of(app.settings.hits.begin(), app.settings.hits.end(), [](const Hit &h) { return h.id == "palette"; }),
          "theme is available under more settings");
    app.captureWindow(app.settings, out + L"/settings-expanded.bmp");
    ShowWindow(app.settings.hwnd, SW_HIDE);
    app.cfg.data["appearance"] = "light";
    app.applyConfig();
    app.draw(app.situation);
    app.captureSituation(out + L"/situation-light.bmp");
    if (auto frame = app.sitShown(); frame && !frame->entities.empty()) {
      for (const auto &hit : app.situation.hits) if (hit.id == "sitEntity") {
        LPARAM xy = MAKELPARAM(int((hit.r.left + hit.r.right) * app.situation.dpi / 2),
                               int((hit.r.top + hit.r.bottom) * app.situation.dpi / 2));
        SendMessageW(app.situation.hwnd, WM_MOUSEMOVE, 0, xy);
        app.captureSituation(out + L"/situation-hover.bmp");
        break;
      }
      SendMessageW(app.situation.hwnd, WM_MOUSELEAVE, 0, 0);
    }
    app.inspect(out + L"/situation-state.json");
    SetWindowPos(app.situation.hwnd, nullptr, 0, 0, int(560 * app.situation.dpi), int(360 * app.situation.dpi),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    app.captureSituation(out + L"/situation-minimum.bmp");
    check(std::none_of(app.situation.hits.begin(), app.situation.hits.end(), [](const Hit &hit) {
      return hit.id=="sitTime" || hit.id=="sitHubLane";
    }), "small window omits timeline and duplicate hub controls");
    // Optional replay of a captured, real privileged sample. No synthetic IO and no elevation.
    if (argc == 3) {
      auto recorded = json::parse(readFile(wide(argv[2])));
      auto source = recorded["situation"];
      SitSample replay;
      replay.appCount=source.value("appCount",recorded.value("processGroups",size_t(0)));
      replay.processCount=source.value("processCount",size_t(0));
      replay.when=source["when"]; replay.cpu=source["cpu"]; replay.mem=source["mem"];
      replay.gpu=source["gpu"]; replay.disk=source["disk"]; replay.net=source["net"];
      replay.used=source["used"]; replay.total=source["total"];
      replay.cores=source["cores"].get<std::vector<double>>();
      for (auto &item:source["entities"]) {
        SitEntity e; e.key=wide(item["key"]); e.name=wide(item["name"]);
        e.cpu=item["cpu"]; e.bytes=item["bytes"]; e.gpu=item["gpu"];
        e.disk=item["disk"]; e.net=item["net"]; e.available=item["available"];
        e.processes=item["processes"]; replay.entities.push_back(e);
      }
      app.sitHistory.clear(); app.sitHistory.push(replay); app.sitCursor=(size_t)-1;
      app.sitFrozen=true; app.sitTokens.clear();
      SetWindowPos(app.situation.hwnd,nullptr,0,0,int(1280*app.situation.dpi),int(760*app.situation.dpi),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
      app.captureSituation(out+L"/flow-recorded-light.bmp");
      auto tokenBefore=app.sitTokens;
      Sleep(40); app.draw(app.situation);
      bool unchanged=true;
      for (auto &p:tokenBefore) unchanged &= app.sitTokens[p.first].progress==p.second.progress;
      check(unchanged,"frozen overview keeps all app tokens stationary");
      app.sitFrozen=false; app.sitFlowAt=seconds()-.04; app.draw(app.situation);
      for (int frame=0;frame<18;++frame) {
        app.sitFlowAt=seconds()-1./30.; app.draw(app.situation);
      }
      app.captureSituation(out+L"/flow-recorded-moving.bmp");
      bool advanced=false;
      for (auto &p:tokenBefore) if (p.second.lane>=0)
        advanced |= app.sitTokens[p.first].progress>p.second.progress;
      check(advanced,"native live draw advances app icons along measured routes");
      app.cfg.data["motion"]=false; tokenBefore=app.sitTokens;
      app.sitFlowAt=seconds()-.04; app.draw(app.situation); unchanged=true;
      for (auto &p:tokenBefore) unchanged &= app.sitTokens[p.first].progress==p.second.progress;
      check(unchanged,"disabled motion retains connections without moving app icons");
      app.cfg.data["motion"]=true; app.sitFrozen=true;
      for (const auto &entity:replay.entities) if (entity.disk>0 && entity.net>0) {
        app.sitPinned=entity.key;
        app.captureSituation(out+L"/flow-recorded-pinned.bmp");
        break;
      }
      app.sitPinned.clear();
      app.sitMode=4; app.captureSituation(out+L"/flow-recorded-io.bmp"); app.sitMode=0;
      app.cfg.data["appearance"]="dark"; app.applyConfig();
      app.captureSituation(out+L"/flow-recorded-dark.bmp");
      app.cfg.data["appearance"]="light"; app.applyConfig();
      SetWindowPos(app.situation.hwnd,nullptr,0,0,int(560*app.situation.dpi),int(360*app.situation.dpi),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
      app.captureSituation(out+L"/flow-recorded-minimum.bmp");
      float originalDpi=app.situation.dpi;
      for (int percent : {100,200}) for (const char *appearance : {"light","dark"}) {
        app.cfg.data["appearance"]=appearance; app.applyConfig();
        RECT bounds{0,0,560*percent/100,360*percent/100};
        UINT dpi=96*percent/100;
        SendMessageW(app.situation.hwnd,WM_DPICHANGED,MAKELONG(dpi,dpi),LPARAM(&bounds));
        app.captureSituation(out+L"/minimum-"+wide(appearance)+L"-"+std::to_wstring(percent)+L".bmp");
        bool bounded=std::abs(app.situation.dpi-percent/100.f)<.01;
        for (auto &hit:app.situation.hits)
          bounded &= hit.r.left>=0 && hit.r.top>=0 && hit.r.right<=560.1 && hit.r.bottom<=360.1;
        std::string label=std::string("minimum layout hit targets fit at ")+appearance+" "+std::to_string(percent)+" percent DPI";
        check(bounded,label.c_str());
      }
      app.situation.dpi=originalDpi;
      app.cfg.data["appearance"]="light"; app.applyConfig();
      SetWindowPos(app.situation.hwnd,nullptr,0,0,int(1280*originalDpi),int(760*originalDpi),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    }
    // Focused regressions use explicit fixtures after the real-sampler captures.
    SetWindowPos(app.situation.hwnd,nullptr,0,0,int(840*app.situation.dpi),int(500*app.situation.dpi),
                 SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    auto click = [&](const std::string &id, const std::wstring &key = L"") {
      app.draw(app.situation);
      for (const auto &h : app.situation.hits) if (h.id == id && (key.empty() || h.key == key)) {
        LPARAM xy = MAKELPARAM(int((h.r.left + h.r.right) * app.situation.dpi / 2),
                               int((h.r.top + h.r.bottom) * app.situation.dpi / 2));
        SendMessageW(app.situation.hwnd, WM_LBUTTONDOWN, 0, xy);
        SendMessageW(app.situation.hwnd, WM_LBUTTONUP, 0, xy);
        return true;
      }
      return false;
    };
    Snapshot fixture;
    fixture.cpu = 25; fixture.used = 4e9; fixture.total = 16e9;
    fixture.gpu = 30; fixture.gpuMem = 2e9; fixture.gpuTotal = 8e9;
    fixture.cores = {10, 40};
    for (int i = 0; i < 15; ++i) {
      Group group;
      group.key = L"app-" + std::to_wstring(i); group.name = group.key;
      group.cpu = 50 - i; group.bytes = 1e8; group.members.resize(2);
      fixture.groups.push_back(group);
    }
    app.snap = fixture; app.sitHistory.clear(); app.sitKeys.clear();
    app.sitFrozen = false; app.updateSituation(seconds(), true);
    auto routes = situationLinks(*app.sitShown(), 0);
    check(routes.size() == 12 && routes[0].entity == 0 && routes[0].lane == 0 && routes[0].value == 50
          && routes[1].entity == 0 && routes[1].lane == 2 && routes[1].value == 1e8,
          "bundled routes retain each app own CPU and memory readings");
    check(situationLinks(*app.sitShown(), 2).empty() && situationLinks(*app.sitShown(), 4).empty(),
          "GPU disk and network have no fabricated app routes");
    auto measured=*app.sitShown();
    measured.entities[0].gpu=12; measured.entities[0].disk=4096; measured.entities[0].net=1024;
    check(situationLinks(measured,2).size()==1 && situationLinks(measured,4).size()==2,
          "measured GPU and IO values create only their own routes");
    measured.entities[0].available=false;
    check(situationLinks(measured,2).empty() && situationLinks(measured,4).empty(),
          "unavailable app suppresses all resource routes");
    SitEntity badge; badge.key=L"C:/Windows/System32/dwm.exe"; badge.name=L"桌面窗口管理器";
    check(situationInitials(badge)==L"DW" && situationIdentity(badge.key)==situationIdentity(lower(badge.key)),
          "fallback badge uses stable executable initials and case-insensitive identity");
    // Hidden hub width must not consume travel time between the two visible curves.
    float withoutHub=0,withHub=0;
    App::sitFlowPoint(200,180,400,600,240,0,240,&withoutHub,0);
    App::sitFlowPoint(200,180,400,600,240,0,240,&withHub,120);
    bool hiddenTravel=false,rightReached=false;
    for (int i=0;i<=240;++i) {
      auto p=App::sitFlowPoint(200,180,400,600,240,i/240.,240,nullptr,120);
      hiddenTravel |= p.x>340.01f && p.x<459.99f;
      rightReached |= p.x>484 && p.x<580;
    }
    check(!hiddenTravel && rightReached && withHub<withoutHub,
          "badge hands off at hub ports without spending time on a masked central segment");
    SituationToken token; token.step({0,1,3,4},0,.2);
    token.step({0,1,3,4},5.7,0);
    check(token.lane==1 && token.progress<1,"one app token advances to the next measured active resource");
    token.step({0,1,3,4},1,0);
    double stopped=token.progress, opacity=token.opacity();
    token.step({3},.1,0);
    check(token.lane==1 && token.progress==stopped && token.opacity()<opacity && token.opacity()>0,
          "lost activity stops in place and fades instead of jumping to another route");
    token.step({},.3,0);
    check(token.lane==-1 && token.opacity()==0,"unknown activity retires without continued flow");
    token.step({3},.02,.7);
    check(token.lane==3 && token.progress<.01 && token.opacity()<.01,
          "replacement route fades in from its origin instead of resetting visibly");
    SituationToken steady; steady.step({0,1},0,.2);
    bool continuous=true;
    for (int frame=0;frame<450;++frame) {
      auto before=steady; steady.step({0,1},1./60.,0,4);
      continuous &= std::abs(steady.opacity()-before.opacity())<.06;
      if (steady.lane==before.lane) continuous &= steady.progress>=before.progress;
      else continuous &= before.opacity()<.01 && steady.opacity()==0;
    }
    check(continuous,"steady 60Hz motion and route handoffs have no opacity jumps or visible teleports");
    SituationFocus focus;
    float last=0; bool smooth=true;
    for (int i=0;i<120;++i) {
      float next=focus.step(1,1./60.,true);
      smooth &= next>=last && next-last<.08f;
      last=next;
    }
    check(smooth && focus.value==1,"activation settles to steady focus without periodic flashing");
    last=focus.value;
    for (int i=0;i<120;++i) {
      float next=focus.step(0,1./60.,true);
      smooth &= next<=last && last-next<.08f;
      last=next;
    }
    check(smooth && focus.value==0,"inactive focus fades to neutral gray without disappearing");
    auto p0=App::sitFlowPoint(100,100,300,600,400,0);
    auto p1=App::sitFlowPoint(100,100,300,600,400,1);
    auto mid=App::sitFlowPoint(100,100,300,600,400,.5);
    check(p0.x==100 && p0.y==100 && std::abs(p1.x-600)<.01 && std::abs(p1.y-400)<.01 && mid.x>=300 && mid.x<600,
          "Bezier flow starts at the app and arrives at the correct resource");
    auto curved=App::sitCurvePoint(100,100,300,200,.25f);
    check(curved.y>100 && curved.y<200 && std::abs((curved.y-100)-(curved.x-100)/2)>1,
          "branch is a real cubic curve rather than a diagonal segment");
    auto in1=App::sitCurvePoint(100,100,212,250,1);
    auto in2=App::sitCurvePoint(100,450,212,250,1);
    auto out1=App::sitCurvePoint(388,250,600,100,0);
    auto out2=App::sitCurvePoint(388,250,600,450,0);
    check(in1.x==in2.x && in1.y==in2.y && out1.x==out2.x && out1.y==out2.y && out1.x>in1.x,
          "all Bezier branches share exactly one port on each hub edge");
    SitSample metrics; metrics.cpu=11; metrics.gpu=22; metrics.mem=33; metrics.disk=1024; metrics.net=2048;
    check(sitMetricValue(metrics,0)==11 && sitMetricValue(metrics,1)==11 && sitMetricValue(metrics,2)==22 &&
          sitMetricValue(metrics,3)==33 && sitMetricValue(metrics,4)==3072,"timeline uses the active tab metric");
    SitEntity appMetrics; appMetrics.cpu=4; appMetrics.gpu=5; appMetrics.bytes=4096; appMetrics.disk=10; appMetrics.net=20;
    check(sitAppValue(appMetrics,1)==4 && sitAppValue(appMetrics,2)==5 && sitAppValue(appMetrics,3)==4096 &&
          sitAppValue(appMetrics,4)==30,"app primary metric follows CPU GPU memory and IO tabs");
    appMetrics.net=-1; metrics.net=-1;
    check(sitMetricValue(metrics,4)==-1 && sitAppValue(appMetrics,4)==-1,"partial IO data never becomes a fabricated total");
    SituationHistory changes; SitSample older,newer,future;
    older.when=10; newer.when=29; future.when=40;
    SitEntity observed; observed.key=L"observed"; observed.name=L"Observed"; observed.cpu=2;
    older.entities.push_back(observed); observed.cpu=3.2; newer.entities.push_back(observed);
    observed.cpu=99; future.entities.push_back(observed);
    changes.push(older); changes.push(newer); changes.push(future);
    auto change=sitRecentChange(changes,newer);
    check(change.name==L"Observed" && std::abs(change.delta-1.2)<.001 && change.seconds==19,
          "recent change uses matching observed apps and never looks beyond the selected frame");
    check(app.sitShown()->appCount==15 && app.sitShown()->processCount==30,"snapshot retains total app and process scope");
    Group combined; Member first,second; first.pid=1;second.pid=2;first.created=second.created=10;
    combined.members={first,second};
    std::vector<AppGpu::Engine> engines={{1,L"adapter0/3D",20},{2,L"adapter0/3D",30},{1,L"adapter1/Copy",40}};
    check(appGpuValue(combined,engines,20)==50 && appGpuValue(combined,engines,5)==-1,
          "GPU groups sum a shared engine but never sum adapters or reuse a newer PID");
    auto hover = [&](const std::wstring &key) {
      app.draw(app.situation);
      for (const auto &h : app.situation.hits) if (h.id == "sitEntity" && h.key == key) {
        LPARAM xy = MAKELPARAM(int((h.r.left + h.r.right) * app.situation.dpi / 2),
                               int((h.r.top + h.r.bottom) * app.situation.dpi / 2));
        SendMessageW(app.situation.hwnd, WM_MOUSEMOVE, 0, xy);
        return;
      }
    };
    for (const auto &h:app.situation.hits) if (h.id=="sitLane" && h.key==L"0") {
      auto xy=MAKELPARAM(int((h.r.left+h.r.right)*app.situation.dpi/2),int((h.r.top+h.r.bottom)*app.situation.dpi/2));
      SendMessageW(app.situation.hwnd,WM_MOUSEMOVE,0,xy); break;
    }
    check(app.sitHoverLane==0 && app.sitPinned.empty(),"resource hover highlights its measured app connections without pinning");
    for (const auto &h:app.situation.hits) if (h.id=="sitHubLane" && h.key==L"2") {
      auto xy=MAKELPARAM(int((h.r.left+h.r.right)*app.situation.dpi/2),int((h.r.top+h.r.bottom)*app.situation.dpi/2));
      SendMessageW(app.situation.hwnd,WM_MOUSEMOVE,0,xy); break;
    }
    check(app.sitHoverLane==2 && app.sitPinned.empty(),"ring memory sector highlights actual app associations");
    click("sitHubLane",L"2");
    check(app.sitMode==3,"ring sector click selects its resource metric");
    click("sitHubLane",L"2");
    check(app.sitMode==0,"second ring sector click returns to overview");
    SendMessageW(app.situation.hwnd,WM_MOUSELEAVE,0,0);
    auto savedFrame=app.sitHistory.samples.back();
    auto &idleFrame=app.sitHistory.samples.back();
    for (auto &e:idleFrame.entities) e.cpu=e.bytes=e.gpu=e.disk=e.net=0;
    idleFrame.cpu=idleFrame.mem=idleFrame.gpu=idleFrame.disk=idleFrame.net=0;
    app.cfg.data["motion"]=false; app.draw(app.situation);
    bool gray=true;
    for (auto &lane:app.sitLaneFocus) gray &= lane.value==0;
    for (auto &entry:app.sitAppFocus) gray &= entry.second.value==0;
    check(gray && situationLinks(idleFrame,0).size()==30,
          "zero measured activity retains neutral connections without colored activation");
    app.sitHistory.samples.back()=savedFrame;
    app.cfg.data["motion"]=true;
    hover(L"app-2");
    check(app.sitHover == L"app-2" && app.sitPinned.empty(), "hover highlights without pinning");
    click("sitEntity", L"app-2");
    SendMessageW(app.situation.hwnd, WM_MOUSELEAVE, 0, 0);
    check(app.sitHover.empty() && app.sitPinned == L"app-2", "pin survives pointer leaving");
    hover(L"app-1");
    check(app.sitHover == L"app-1" && app.sitPinned == L"app-2", "hover previews another app without replacing pin");
    SendMessageW(app.situation.hwnd, WM_MOUSELEAVE, 0, 0);
    click("sitEntity", L"app-2");
    check(app.sitPinned.empty(), "second click clears pin");
    click("sitEntity", L"app-2");
    app.snap.groups[2].cpu = 0;
    app.updateSituation(seconds() + 6, true);
    check(app.sitShown()->entities.front().key == L"app-2", "pinned app survives falling outside top twelve");
    app.snap.groups.erase(app.snap.groups.begin() + 2);
    app.updateSituation(seconds() + 12, true);
    check(app.sitShown()->entities.front().key == L"app-2" && !app.sitShown()->entities.front().available,
          "missing pinned app keeps an unavailable card");
    routes = situationLinks(*app.sitShown(), 0);
    check(std::none_of(routes.begin(), routes.end(), [](const SituationLink &link) { return link.entity == 0; }),
          "exited app has no resource routes");
    click("sitTime", L"0");
    app.snap.cores = {99, 99}; app.snap.gpuMem = 7e9; app.snap.used = 12e9;
    app.snap.groups[0].name = L"changed"; app.snap.groups[0].members.clear();
    app.updateSituation(seconds() + 18);
    check(app.sitFrozen && app.sitShown()->cores == std::vector<double>({10, 40}) &&
          app.sitShown()->gpuMem == 2e9 && app.sitShown()->used == 4e9 &&
          app.sitShown()->entities.front().name == L"app-0" && app.sitShown()->entities.front().processes == 2,
          "replay keeps core VRAM memory and process metadata from its frame");
    check(app.sitLaneSeries(0).size() == 1, "replay sparklines stop at selected frame");
    click("sitTab", L"4");
    check(app.sitMode == 4 && situationLaneFocused(app.sitMode, 3) && situationLaneFocused(app.sitMode, 4)
          && !situationLaneFocused(app.sitMode, 0), "IO tab focuses both disk and network");
    click("sitLane", L"4");
    check(app.sitMode == 0, "network lane toggles IO focus off");
    app.draw(app.situation);
    check(std::none_of(app.situation.hits.begin(),app.situation.hits.end(),[](const Hit &h){return h.id=="sitLive";}),
          "pause and resume share one state control");
    float timeWidth=0; bool timeHeight=true;
    for (auto &h:app.situation.hits) if (h.id=="sitTime") { timeWidth+=h.r.right-h.r.left; timeHeight &= h.r.bottom-h.r.top==34; }
    RECT client{}; GetClientRect(app.situation.hwnd,&client);
    check(timeHeight && std::abs(timeWidth-(client.right/app.situation.dpi-2*SituationLayout::choose(client.right/app.situation.dpi,client.bottom/app.situation.dpi).pad))<.1,
          "entire 34 DIP timeline strip selects the nearest frame");
    click("sitFreeze");
    check(!app.sitFrozen && app.sitShown()->cores == app.snap.cores, "live control resumes current data");
    app.situation.focus=-1;
    SendMessageW(app.situation.hwnd,WM_KEYDOWN,VK_TAB,0);
    SendMessageW(app.situation.hwnd,WM_KEYDOWN,VK_RETURN,0);
    check(app.situation.focus==0 && app.sitMode==0,"keyboard Tab and Enter activate the overview tab");
    app.event(app.situation.hwnd, WM_ENTERSIZEMOVE, 0, 0);
    app.event(app.situation.hwnd, WM_EXITSIZEMOVE, 0, 0);
    check(!app.resizing, "moving overview releases sampler resize gate");
    click("sitFreeze");
    SendMessageW(app.situation.hwnd, WM_KEYDOWN, VK_SPACE, 0);
    check(!app.sitFrozen, "space resumes after freeze button");
    app.quitting = true;
    DestroyWindow(app.situation.hwnd);
    DestroyWindow(app.detail.hwnd);
    DestroyWindow(app.settings.hwnd);
    DestroyWindow(app.panel.hwnd);
  }
  report["failures"] = failures;
  writeFile(out + L"/result.json", report.dump(2));
  CoUninitialize();
  return failures ? 1 : 0;
}
