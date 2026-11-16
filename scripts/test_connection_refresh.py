#!/usr/bin/env python3
"""Compile the actual GUI connection methods against a deterministic queued transport.
No GUI, network, printer, or installed settings are used. Pass an output directory.
"""
from pathlib import Path
import subprocess
import sys
root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)
cpp = (root / "src/slic3r/GUI/GUI_App.cpp").read_text()
hpp = (root / "src/slic3r/GUI/GUI_App.hpp").read_text()
state = hpp[hpp.index("    struct GatewayDeviceState"):hpp.index("    std::string m_gateway_loaded_base_url;")]
methods = cpp[cpp.index("void GUI_App::refresh_gateway_connection()"):cpp.index("void GUI_App::query_gateway_device_objects()")]
clear = cpp[cpp.index("void GUI_App::clear_gateway_device()"):cpp.index("void GUI_App::apply_gateway_machine_slots(")]
prefix = r'''#include "slic3r/Utils/GatewayProtocol.hpp"
#include <cassert>
#include <functional>
#include <deque>
#include <memory>
#include <sstream>
#include <iostream>
using namespace Slic3r;
using J = nlohmann::json;
#define BOOST_LOG_TRIVIAL(x) std::ostringstream()
struct Sidebar { void update_all_preset_comboboxes(bool) {} };
struct Plater { Sidebar s; Sidebar& sidebar() { return s; } };
struct Main { Plater p; Plater* plater() { return &p; } };
struct Bundle { std::vector<int> m_connect_machine_info_list; };
struct Transport {
 using Callback = std::function<void(Gateway::GatewayError,const J&)>;
 struct Request {std::string method; Callback callback;};
 std::deque<Request> requests;
 void request(std::string method, const J&, Callback callback) { requests.push_back({method,callback}); }
 void watch_device(const J&, Callback) {}
 Callback take(std::string method) {
  for(auto i=requests.begin();i!=requests.end();++i) if(i->method==method) {
   auto cb=i->callback; requests.erase(i); return cb;
  }
  assert(false); return {};
 }
};
struct GUI_App {
 std::shared_ptr<Transport> m_gateway_service = std::make_shared<Transport>();
 Main* mainframe=nullptr; Bundle* preset_bundle=nullptr;
 std::deque<std::function<void()>> later;
 void CallAfter(std::function<void()> f) {later.push_back(f);}
 void drain() {while(!later.empty()) {auto f=later.front();later.pop_front();f();}}
 void query_gateway_device_objects() {}
 void clear_gateway_machine_slots() {}
 void refresh_gateway_machine_slots_ui() {}
 void refresh_gateway_connection();
 void query_gateway_current_device();
 void clear_gateway_device();
'''
main = r'''
J current(std::string sn) {return J{{"sn",sn},{"connected",true}};}
J listing(std::string sn,std::string ip) {return J{{"devices",J::array({J{{"sn",sn},{"ip",ip},{"connected",true},{"type","LAN"}}})}};}
void answer(GUI_App& a,std::string sn,std::string ip) {
 a.m_gateway_service->take("query.device.current")({},current(sn));
 a.m_gateway_service->take("query.device.list")({},listing(sn,ip));
}
int main() {
 GUI_App a; auto t=a.m_gateway_service;
 a.refresh_gateway_connection(); answer(a,"A","192.168.1.30");
 assert(a.m_gateway_device.lan_address=="192.168.1.30");
 a.query_gateway_current_device();
 t->take("query.device.current")({},current("A"));
 assert(a.m_gateway_device.lan_address=="192.168.1.30");
 auto old_list=t->take("query.device.list");
 a.query_gateway_current_device(); t->take("query.device.current")({},current("A"));
 assert(t->requests.empty()); // coalesced list request
 std::cout<<"PASS same-printer refresh retains address and coalesces lookups\n";
 a.query_gateway_current_device(); t->take("query.device.current")({},current("B"));
 assert(a.m_gateway_device.lan_address.empty());
 old_list({},listing("A","192.168.1.30"));
 assert(a.m_gateway_device.lan_address.empty());
 t->take("query.device.list")({},listing("B","192.168.1.31"));
 assert(a.m_gateway_device.lan_address=="192.168.1.31");
 std::cout<<"PASS switch clears identity; late old-printer reply ignored\n";
 a.query_gateway_current_device(); auto old_current=t->take("query.device.current");
 a.m_gateway_current_query_started-=std::chrono::seconds(11);
 a.refresh_gateway_connection();
 old_current({},current("A"));
 assert(a.m_gateway_device.serial_number.empty());
 answer(a,"B","192.168.1.31");
 std::cout<<"PASS stalled current query retries; late timed-out reply ignored\n";
 a.query_gateway_current_device();t->take("query.device.current")({},current("B"));
 auto timed_list=t->take("query.device.list");
 a.m_gateway_device.address_query_started-=std::chrono::seconds(11);
 a.m_gateway_current_query_started-=std::chrono::seconds(11);
 a.refresh_gateway_connection();answer(a,"B","192.168.1.32");
 timed_list({},listing("B","192.168.1.31"));
 assert(a.m_gateway_device.lan_address=="192.168.1.32");
 std::cout<<"PASS stalled address query recovers without reconnect; stale reply ignored\n";
 a.query_gateway_current_device();a.query_gateway_current_device();
 assert(t->requests.size()==1);
 t->take("query.device.current")({},current("B"));a.drain();
 assert(t->requests.size()==2); // one list + one queued current
 t->take("query.device.current")({},current("B"));
 t->take("query.device.list")({Gateway::GatewayErrorCode::TransportError,"lost"},{});
 assert(a.m_gateway_device.lan_address.empty());
 a.m_gateway_current_query_started-=std::chrono::seconds(3);
 a.refresh_gateway_connection(); answer(a,"B","192.168.1.32");
 std::cout<<"PASS repeated notifications coalesce; transport error recovers\n";
 a.query_gateway_current_device();auto disconnected_reply=t->take("query.device.current");
 a.clear_gateway_device();disconnected_reply({},current("B"));
 assert(!a.m_gateway_device.connected && a.m_gateway_device.lan_address.empty());
 std::cout<<"PASS disconnect invalidates in-flight reply\n";
 J l=listing("A","192.168.1.30");auto& ds=l["devices"];
 ds.push_back(ds[0]);assert(Gateway::connected_lan_address(l,"A")=="192.168.1.30");
 ds.push_back(J{{"sn","A"},{"type","cloud"},{"connected",true},{"ip","10.0.0.1"}});
 assert(Gateway::connected_lan_address(l,"A")=="192.168.1.30");
 ds.push_back(J{{"sn","A"},{"type","LAN"},{"connected",true},{"ip","192.168.1.31"}});
 assert(Gateway::connected_lan_address(l,"A").empty());
 assert(Gateway::connected_lan_address(l,"B").empty());
 std::cout<<"PASS duplicate/cloud records accepted; conflicting LAN endpoints rejected\n";
}
'''
source=out/"connection_refresh.cpp"
source.write_text(prefix+state+"};\n"+methods+clear+main)
exe=out/"connection_refresh"
subprocess.run(["c++","-std=c++17","-O0","-I"+str(root/"src"),"-I"+str(root/"deps_src"),str(source),str(root/"src/slic3r/Utils/GatewayProtocol.cpp"),"-o",str(exe)],check=True)
subprocess.run([str(exe)],check=True)
