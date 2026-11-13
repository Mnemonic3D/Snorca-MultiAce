#ifndef SLIC3R_SNORCA_PRINTER_CONNECT_HPP
#define SLIC3R_SNORCA_PRINTER_CONNECT_HPP

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "slic3r/Utils/SnorcaFilamentSync.hpp"
#include "slic3r/Utils/MQTT.hpp"
#include "slic3r/Utils/MoonRaker.hpp"
#include "GUI_App.hpp"

namespace Slic3r { namespace GUI { namespace SnorcaPrinterConnect {
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;

struct Ready {
    bool ok = false;
    std::shared_ptr<MqttClient> engine;
    std::shared_ptr<Moonraker_Mqtt> host;
    DynamicPrintConfig config;
    DeviceInfo device;
    std::string error;
};

inline bool connected(const DeviceInfo& device)
{
    std::string address;
    try { address = SnorcaFilamentSync::printer_address(device.ip); }
    catch (const std::invalid_argument&) { return false; }
    std::shared_ptr<PrintHost> current; wxGetApp().get_connect_host(current);
    const auto host = std::dynamic_pointer_cast<Moonraker_Mqtt>(current);
    if (!host || host->get_host() != address + ":8883" || host->m_client_id != device.clientId || !host->has_connected_engine()) return false;
    std::lock_guard<std::mutex> lock(Moonraker_Mqtt::m_sn_mtx);
    return Moonraker_Mqtt::m_sn == device.sn;
}
inline bool matching_connected(const DeviceInfo& device) { return connected(device); }

// GUI-thread refresh of display state only. Pairing identity, credentials and
// printer preset values are retained byte-for-byte. Never inserts a device.
inline bool refresh_device_flags()
{
    if (!wxIsMainThread() || !wxGetApp().app_config) return false;
    auto devices = wxGetApp().app_config->get_devices();
    bool changed = false;
    for (auto& device : devices) {
        try { (void)SnorcaFilamentSync::printer_address(device.ip); }
        catch (const std::invalid_argument&) { continue; }
        const auto count = std::count_if(devices.begin(), devices.end(), [&](const DeviceInfo& other) { return other.dev_id == device.dev_id; });
        if (device.dev_id.empty() || count != 1) continue; // save_device_info keys by dev_id.
        const bool actual = connected(device);
        if (device.connected != actual) {
            device.connected = actual;
            wxGetApp().app_config->save_device_info(device);
            changed = true;
        }
    }
    if (changed) wxGetApp().device_card_notify(Json(devices));
    return changed;
}


namespace detail {
struct Response {
    std::mutex mutex;
    std::condition_variable changed;
    bool received = false;
    Json body;
};
inline int64_t request_id()
{
    static std::atomic<uint64_t> counter{0};
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return ms * 1000 + static_cast<int64_t>(counter.fetch_add(1) % 1000);
}
inline void stop(const std::shared_ptr<MqttClient>& engine)
{
    if (engine) { std::string ignored; engine->Disconnect(ignored, 200); }
}
inline std::string bounded_string(const Json& j, const char* key, size_t maximum)
{
    if (!j.contains(key) || !j.at(key).is_string()) throw std::runtime_error("Printer authorization response is incomplete.");
    const auto value = j.at(key).get<std::string>();
    if (value.empty() || value.size() > maximum) throw std::runtime_error("Printer authorization response is invalid.");
    return value;
}
} // namespace detail

// Worker-only. A literal private LAN address and an EXISTING saved pairing are
// required. Never requests approval/PIN or starts a printer action.
// No GUI/native global host state is modified until install() after caller checks.
inline Ready prepare(const DeviceInfo& device, const DynamicPrintConfig& selected_config,
                     const std::shared_ptr<std::atomic<bool>>& alive,
                     const std::shared_ptr<std::atomic<uint64_t>>& generation, uint64_t ticket)
{
    Ready ready; ready.device = device; ready.config = selected_config;
    std::shared_ptr<MqttClient> plain;
    const auto deadline = Clock::now() + std::chrono::seconds(15);
    auto current = [&] { return alive && alive->load() && generation && generation->load() == ticket && Clock::now() < deadline; };
    auto budget = [&](int maximum) {
        if (!current()) throw std::runtime_error("Printer selection changed or connection timed out.");
        return std::max(1, std::min(maximum, static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count())));
    };
    auto exchange = [&](const std::shared_ptr<MqttClient>& engine, const std::string& response_topic,
                        const std::string& request_topic, const std::string& method, const Json& params) {
        const int64_t id = detail::request_id();
        auto response = std::make_shared<detail::Response>();
        engine->SetMessageCallback([response, response_topic, id](const std::string& topic, const std::string& payload) {
            if (topic != response_topic || payload.size() > 262144) return;
            try {
                auto body = Json::parse(payload);
                if (!body.is_object() || !body.contains("id") || body.at("id") != id) return;
                std::lock_guard<std::mutex> lock(response->mutex);
                if (!response->received) { response->body = std::move(body); response->received = true; response->changed.notify_all(); }
            } catch (...) {} // Never log an authorization payload or untrusted exception.
        });
        std::string message;
        if (!engine->Subscribe(response_topic, 1, message, budget(1500))) throw std::runtime_error("Printer response subscription failed.");
        if (!engine->Publish(request_topic, Json{{"jsonrpc","2.0"},{"method",method},{"params",params},{"id",id}}.dump(), 1, message))
            throw std::runtime_error("Printer status request failed.");
        const auto response_deadline = std::min(deadline, Clock::now() + std::chrono::seconds(3));
        std::unique_lock<std::mutex> lock(response->mutex);
        while (!response->received && current() && Clock::now() < response_deadline) response->changed.wait_for(lock, std::chrono::milliseconds(50));
        if (!current()) throw std::runtime_error("Printer selection changed or connection timed out.");
        if (!response->received || response->body.contains("error") || !response->body.contains("result") || !response->body.at("result").is_object())
            throw std::runtime_error("Printer did not confirm its existing LAN connection.");
        return response->body.at("result");
    };
    try {
        if (!SnorcaFilamentSync::valid_lan_ipv4(device.ip) || device.sn.empty() || device.sn.size() > 128 ||
            device.clientId.empty() || device.clientId.size() > 256 || device.sn.find_first_of("/+#") != std::string::npos)
            throw std::runtime_error("A unique saved LAN printer pairing is required.");
        budget(1);
        const std::string temporary_id = "orca-try-snorca-" + std::to_string(detail::request_id());
        plain = std::make_shared<MqttClient>("mqtt://" + device.ip + ":1884", temporary_id, "", "", true);
        std::string message;
        if (!plain->Connect(message, budget(2500))) throw std::runtime_error("Printer LAN authorization service is unavailable.");
        // These are the existing official native LAN configuration topics. No new
        // pairing method is called if the saved client has lost authorization.
        const auto auth = exchange(plain, "12345678/config/response", "12345678/config/request",
            "server.client_manager.confirm_lan_status", Json{{"clientid",device.clientId}});
        if (auth.value("state", std::string()) != "success") throw std::runtime_error("Saved printer pairing is not authorized; reconnect it in Device.");
        const auto serial = detail::bounded_string(auth, "sn", 128);
        const auto client = detail::bounded_string(auth, auth.contains("clientid") ? "clientid" : "clientId", 256);
        if (serial != device.sn || client != device.clientId || auth.value("port", 8883) != 8883)
            throw std::runtime_error("Printer pairing identity does not match the selected saved printer.");
        const auto ca = detail::bounded_string(auth, "ca", 65536);
        const auto cert = detail::bounded_string(auth, "cert", 65536);
        const auto key = detail::bounded_string(auth, "key", 65536);
        detail::stop(plain); plain.reset(); budget(1);
        ready.engine = std::make_shared<MqttClient>("ssl://" + device.ip + ":8883", client, ca, cert, key, "", "", false);
        if (!ready.engine->Connect(message, budget(3500))) throw std::runtime_error("Printer secure LAN connection failed.");
        // A successful TLS CONNECT alone is insufficient: prove the exact serial
        // response channel answers a read-only native status RPC before adoption.
        exchange(ready.engine, serial + "/response", serial + "/request", "machine.system_info", Json::object());
        for (const auto& suffix : {"/status", "/notification"})
            if (!ready.engine->Subscribe(serial + suffix, 1, message, budget(1000))) throw std::runtime_error("Printer status subscription failed.");
        ready.config.set("print_host", device.ip + ":8883");
        ready.config.option<ConfigOptionEnum<PrintHostType>>("host_type")->value = PrintHostType::htMoonRaker_mqtt;
        ready.host = std::make_shared<Moonraker_Mqtt>(&ready.config, false); // No external-IP probe or static-engine mutation.
        ready.host->m_client_id = client; ready.host->m_port = 8883;
        ready.host->m_ca = ca; ready.host->m_cert = cert; ready.host->m_key = key;
        budget(1); ready.ok = true;
    } catch (const std::runtime_error& e) {
        // Only locally generated status messages reach the UI. Paho/JSON exceptions
        // may include server input, so never relay or log their details.
        const std::string text = e.what();
        static const char* const messages[] = {"Printer selection changed or connection timed out.", "Printer response subscription failed.",
            "Printer status request failed.", "Printer did not confirm its existing LAN connection.", "A unique saved LAN printer pairing is required.",
            "Printer LAN authorization service is unavailable.", "Saved printer pairing is not authorized; reconnect it in Device.",
            "Printer pairing identity does not match the selected saved printer.", "Printer secure LAN connection failed.",
            "Printer status subscription failed.", "Printer authorization response is incomplete.", "Printer authorization response is invalid."};
        ready.error = "Printer LAN connection failed; previous connection retained.";
        for (const auto* allowed : messages) if (text == allowed) ready.error = allowed;
    } catch (...) { ready.error = "Printer LAN connection failed; previous connection retained."; }
    detail::stop(plain);
    if (!ready.ok) { detail::stop(ready.engine); ready.engine.reset(); ready.host.reset(); }
    return ready;
}

// GUI-thread only, AFTER caller verifies alive/generation/selected IP+serial+client.
// Adopts the real prepared native engine; it never marks a fake host connected.
inline bool install(Ready& ready, std::string& message)
{
    if (!wxIsMainThread() || !ready.ok || !ready.host || !ready.engine || !ready.engine->CheckConnected()) {
        message = "Prepared printer connection is no longer available."; return false;
    }
    std::string ignored;
    if (!ready.host->set_engine(ready.engine, ignored, 200)) { message = "Native printer connection could not be adopted."; return false; }
    { std::lock_guard<std::mutex> lock(Moonraker_Mqtt::m_sn_mtx); Moonraker_Mqtt::m_sn = ready.device.sn; }
    // Hold the host for the duration of an arriving callback. A serial-specific
    // filter also discards a late packet from an engine superseded by selection.
    std::weak_ptr<Moonraker_Mqtt> target = ready.host;
    const auto serial = ready.device.sn;
    ready.engine->SetMessageCallback([target, serial](const std::string& topic, const std::string& payload) {
        if (topic != serial + "/response" && topic != serial + "/status" && topic != serial + "/notification") return;
        { std::lock_guard<std::mutex> lock(Moonraker_Mqtt::m_sn_mtx); if (Moonraker_Mqtt::m_sn != serial) return; }
        if (auto host = target.lock()) host->on_mqtt_tls_message_arrived(topic, payload);
    });
    wxGetApp().set_connect_host(ready.host);
    wxGetApp().set_host_config(ready.config);
    refresh_device_flags();
    message.clear(); return true;
}

} } } // namespace Slic3r::GUI::SnorcaPrinterConnect
#endif
