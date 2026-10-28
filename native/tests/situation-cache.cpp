#include "../src/main.cpp"
#include <iostream>

// Ordinary-user replay. Uses recorded readings; starts no sampler or driver.
int main(int argc, char **argv) {
  if (argc != 3) return 2;
  SetProcessDPIAware();
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  json checks = json::array();
  int failures = 0;
  auto check = [&](bool ok, const std::string &name) {
    checks.push_back({{"name", name}, {"passed", ok}}); failures += !ok;
  };
  {
    App app(GetModuleHandleW(nullptr));
    app.cfg.path = wide(argv[2]) + L".config.json";
    app.cfg.data["appearance"] = "light";
    app.cfg.data["motion"] = true;
    app.theme.update(app.cfg);
    app.situation.hwnd = CreateWindowExW(WS_EX_LAYERED, L"STATIC", L"render check",
      WS_POPUP, 0, 0, 813, 500, nullptr, nullptr, app.instance, nullptr);
    auto data = json::parse(readFile(wide(argv[1])))["situation"];
    SitSample s;
    s.when=data["when"]; s.cpu=data["cpu"]; s.mem=data["mem"]; s.gpu=data["gpu"];
    s.disk=data["disk"]; s.net=data["net"]; s.used=data["used"]; s.total=data["total"];
    for (const auto &v : data["entities"]) {
      SitEntity e;
      e.key=wide(v["key"]); e.name=wide(v["name"]); e.cpu=v["cpu"]; e.bytes=v["bytes"];
      e.gpu=v["gpu"]; e.disk=v["disk"]; e.net=v["net"];
      e.available=v["available"]; e.processes=v["processes"];
      s.entities.push_back(e);
    }
    app.sitHistory.push(s);
    auto pixels = [&] {
      const auto &c = app.situation.canvas;
      const auto *p = static_cast<const unsigned char *>(c.pixels);
      return std::vector<unsigned char>(p, p + size_t(c.width)*c.height*4);
    };
    auto stationaryDraw = [&](bool animation) {
      app.sitFlowAt = seconds()+1; // Hold the animation clock, not the rendered state.
      app.draw(app.situation, animation);
    };
    auto sameAsFull = [&] {
      stationaryDraw(true); auto cached = pixels();
      stationaryDraw(false); return cached == pixels();
    };
    stationaryDraw(false);
    check(!app.sitBadges.empty(), "unfocused overview retains moving application badges");
    for (int size=0; size<3; ++size) {
      app.situation.dpi = size==2 ? 1.5f : 1.f;
      int w=size==0 ? 813 : size==1 ? 560 : 840, h=size==0 ? 500 : size==1 ? 360 : 540;
      SetWindowPos(app.situation.hwnd,nullptr,0,0,w,h,SWP_NOZORDER|SWP_NOACTIVATE);
      std::string label = std::to_string(w)+"x"+std::to_string(h);
      std::set<std::wstring> left, right;
      bool smooth=true, retained=true;
      std::map<std::wstring,float> opacity;
      for (int frame=0; frame<240; ++frame) {
        app.sitFlowAt=seconds()-1./30; app.draw(app.situation,true);
        for (const auto &b:app.sitBadges) {
          if (opacity.count(b.entity.key)) smooth &= std::abs(opacity[b.entity.key]-b.opacity)<.36f;
          opacity[b.entity.key]=b.opacity;
          if (b.opacity>.1f && b.point.x<app.sitPorts[1]) left.insert(b.entity.key);
          if (b.opacity>.1f && b.point.x>app.sitPorts[2]) right.insert(b.entity.key);
        }
        if (frame==80 || frame==160 || frame==239) retained &= sameAsFull();
      }
      check(!left.empty() && !right.empty() && smooth, label+" continuous flow reaches both sides");
      check(retained, label+" cached pixels equal full redraw without trails");
      auto hits=app.situation.hits.size();
      stationaryDraw(true);
      check(hits>0 && app.situation.hits.size()==hits, label+" cached frame keeps interaction targets");
    }
    auto entity=std::find_if(app.situation.hits.begin(),app.situation.hits.end(),
      [](const Hit &h){return h.id=="sitEntity";});
    check(entity!=app.situation.hits.end(), "application interaction target exists");
    if (entity!=app.situation.hits.end()) {
      auto key=entity->key;
      LPARAM point=MAKELPARAM(int((entity->r.left+entity->r.right)*.5f*app.situation.dpi),
                            int((entity->r.top+entity->r.bottom)*.5f*app.situation.dpi));
      app.event(app.situation.hwnd,WM_MOUSEMOVE,0,point);
      app.event(app.situation.hwnd,WM_LBUTTONDOWN,MK_LBUTTON,point);
      app.event(app.situation.hwnd,WM_LBUTTONUP,0,point);
      check(app.sitPinned==key && app.sitHover==key && sameAsFull(), "hover and click pin refresh cached scene");
    }
    app.sitFrozen=true; stationaryDraw(false);
    check(app.sitBadges.empty() && sameAsFull(), "freeze removes moving badges without residual pixels");
    app.sitFrozen=false; app.cfg.data["motion"]=false; stationaryDraw(false);
    check(app.sitBadges.empty() && sameAsFull(), "motion off retains static relationships only");
    app.cfg.data["appearance"]="dark"; app.theme.update(app.cfg); stationaryDraw(false);
    check(sameAsFull(), "theme refresh replaces cached scene");
    // New real reading, no synthetic resource values.
    app.sitPinned.clear(); app.sitHover.clear(); stationaryDraw(false);
    auto before=pixels(); app.sitHistory.samples.back().cpu=-1; app.sitBaseDirty=true;
    stationaryDraw(true);
    check(before!=pixels() && sameAsFull(), "unavailable new reading replaces stale cached value");
    app.hideSituation();
    check(!app.sitBase && !app.situation.canvas.rt, "hide releases both render buffers");
    stationaryDraw(false);
    check(app.sitBase && app.situation.canvas.rt && sameAsFull(), "reopen rebuilds render buffers");
    DestroyWindow(app.situation.hwnd);
  }
  writeFile(wide(argv[2]), json({{"checks",checks},{"failures",failures}}).dump(2));
  CoUninitialize();
  std::cout<<checks.size()<<" checks; "<<failures<<" failures\n";
  return failures ? 1 : 0;
}
