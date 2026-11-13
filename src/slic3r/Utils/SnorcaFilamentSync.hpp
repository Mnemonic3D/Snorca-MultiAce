#ifndef slic3r_SnorcaFilamentSync_hpp_
#define slic3r_SnorcaFilamentSync_hpp_

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

namespace Slic3r { namespace SnorcaFilamentSync {

using json = nlohmann::json;

// Literal RFC1918 addresses only: no DNS, public endpoints or ambiguous octal forms.
inline bool valid_lan_ipv4(const std::string& value)
{
    int octets[4]={};size_t position=0;
    for(int part=0;part<4;++part){
        const size_t start=position;int number=0;
        while(position<value.size()&&value[position]>='0'&&value[position]<='9'){
            number=number*10+(value[position++]-'0');if(position-start>3||number>255)return false;
        }
        if(position==start||(position-start>1&&value[start]=='0'))return false;
        octets[part]=number;
        if(part<3){if(position>=value.size()||value[position++]!='.')return false;}
    }
    if(position!=value.size())return false;
    return octets[0]==10||(octets[0]==172&&octets[1]>=16&&octets[1]<=31)||(octets[0]==192&&octets[1]==168);
}
inline std::string printer_address(std::string value)
{
    while(!value.empty()&&std::isspace(static_cast<unsigned char>(value.front())))value.erase(value.begin());
    while(!value.empty()&&std::isspace(static_cast<unsigned char>(value.back())))value.pop_back();
    if(value.rfind("http://",0)==0)value.erase(0,7);else if(value.rfind("https://",0)==0)value.erase(0,8);
    while(!value.empty()&&value.back()=='/')value.pop_back();
    const auto slash=value.find('/');
    if(slash!=std::string::npos){if(value.substr(slash)!="/multiace")throw std::invalid_argument("Printer address has an unsupported path.");value.erase(slash);}
    const auto colon=value.find(':');
    if(colon!=std::string::npos){const auto port=value.substr(colon+1);unsigned number=0;
        if(port.empty()||port.size()>5)throw std::invalid_argument("Printer port is invalid.");
        for(char c:port){if(c<'0'||c>'9')throw std::invalid_argument("Printer port is invalid.");number=number*10+unsigned(c-'0');}
        if(number<1||number>65535)throw std::invalid_argument("Printer port is invalid.");value.erase(colon);
    }
    if(!valid_lan_ipv4(value))throw std::invalid_argument("Select a saved private LAN IPv4 printer address.");
    return value;
}

namespace detail {

inline void require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::invalid_argument("Filament sync: " + message);
}

inline int index(const json& value, const std::string& name)
{
    require(value.is_number_integer(), name + " must be an integer");
    // Compare before converting: unsigned or very large JSON numbers must not wrap.
    require(value >= 0 && value <= 3, name + " is outside 0..3");
    return value.get<int>();
}

inline std::string text(const json& object, const char* key)
{
    if (!object.contains(key) || object.at(key).is_null())
        return {};
    require(object.at(key).is_string(), std::string(key) + " must be a string or null");
    std::string result = object.at(key).get<std::string>();
    require(result.size() <= 256, std::string(key) + " exceeds 256 bytes");
    return result;
}

inline bool boolean(const json& object, const char* key, bool fallback = false)
{
    if (!object.contains(key) || object.at(key).is_null())
        return fallback;
    require(object.at(key).is_boolean(), std::string(key) + " must be boolean");
    return object.at(key).get<bool>();
}

inline std::string upper(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    return value;
}

inline std::string color_text(const std::string& value)
{
    std::string color = value;
    if (!color.empty() && color.front() == '#')
        color.erase(color.begin());
    if (color.size() != 6 || !std::all_of(color.begin(), color.end(), [](unsigned char c) { return std::isxdigit(c); }))
        return {};
    return "#" + upper(color);
}

inline json color(const json& item, bool identity_supplied, json& warnings, const std::string& name)
{
    const std::string explicit_color = text(item, "color");
    if (!explicit_color.empty()) {
        const std::string normalized = color_text(explicit_color);
        if (!normalized.empty())
            return normalized;
        warnings.push_back(name + ": invalid color; left unknown");
        return nullptr;
    }
    // Some firmware emits [0,0,0] for unlabelled slots. Numeric RGB is only
    // meaningful when an identity is supplied separately; zero alone is not black.
    if (identity_supplied && item.contains("color_rgb") && !item.at("color_rgb").is_null()) {
        const json& rgb = item.at("color_rgb");
        if (rgb.is_array() && rgb.size() == 3 && std::all_of(rgb.begin(), rgb.end(), [](const json& v) {
                return v.is_number_integer() && v >= 0 && v <= 255;
            })) {
            static const char* digits = "0123456789ABCDEF";
            std::string value = "#";
            for (const auto& channel : rgb) {
                const int v = channel.get<int>();
                value += digits[v >> 4];
                value += digits[v & 15];
            }
            return value;
        }
        warnings.push_back(name + ": invalid RGB; left unknown");
    }
    return nullptr;
}

inline std::map<int, const json*> indexed(const json& array, const std::string& name)
{
    require(array.is_array() && array.size() <= 4, name + " must contain at most four entries");
    std::map<int, const json*> result;
    for (const auto& item : array) {
        require(item.is_object() && item.contains("idx"), name + " entry has no idx");
        const int id = index(item.at("idx"), name + " idx");
        require(result.emplace(id, &item).second, name + " has duplicate idx " + std::to_string(id));
    }
    return result;
}

inline json row(const std::string& printer_id, const std::string& address, const std::string& label,
                int nozzle, int slot, const json& ace, const json& ace_slot, const json& item,
                bool available, bool identity_supplied, const std::string& source, json& warnings)
{
    const std::string material = upper(text(item, "material"));
    const json swatch = color(item, identity_supplied && !material.empty(), warnings, label);
    const bool identified = available && identity_supplied && !material.empty() && !swatch.is_null();
    std::string description = label + (available ? "" : " (unavailable)");
    description += material.empty() ? " — unknown material" : " — " + material;
    if (swatch.is_null())
        description += " / unknown color";
    if (available && !identified)
        warnings.push_back(label + ": identity unverified; supplied values are not RFID confirmation");
    return {{"key", printer_id + "|" + address}, {"label", description}, {"material", material},
            {"color", swatch}, {"nozzle", nozzle}, {"mms_slot", slot}, {"ace_unit", ace}, {"ace_slot", ace_slot},
            {"source", source}, {"identified", identified}, {"available", available}, {"user_override", false}};
}

} // namespace detail

// Pure offline transformation. No HTTP, filesystem, printer or profile writes.
// Throws invalid_argument for malformed/ambiguous topology; callers must keep
// their prior inventory and display the error instead of applying partial rows.
// printer_id must be the selected printer's stable serial, not its display name.
inline json normalize(const json& state, const std::string& printer_id, const std::string& host,
                      const json& overrides = json::object())
{
    using namespace detail;
    require(valid_lan_ipv4(host), "host is not a private LAN printer address");
    require(!printer_id.empty() && printer_id.size() <= 128 &&
                std::all_of(printer_id.begin(), printer_id.end(), [](unsigned char c) {
                    return std::isalnum(c) || c == '-' || c == '_' || c == '.';
                }), "printer_id must be a nonempty stable serial");
    require(state.is_object(), "state must be an object");
    require(text(state, "mode") == "head", "only explicit head mode is supported");
    require(state.contains("ace_heads") && state.at("ace_heads").is_array() && state.at("ace_heads").size() <= 4,
            "ace_heads must be an array with at most four heads");
    require(state.contains("head_feeder") && state.at("head_feeder").is_object() && state.at("head_feeder").size() == 4,
            "head_feeder must contain all four heads");
    require(state.contains("head_ace") && state.at("head_ace").is_object() && state.at("head_ace").size() == 4,
            "head_ace must contain all four heads");
    require(state.contains("aces") && state.contains("toolheads"), "aces or toolheads missing");
    require(overrides.is_object(), "overrides must be an object scoped to this printer");
    for (auto it = overrides.begin(); it != overrides.end(); ++it) {
        require(it.key() == "ACE:0:3", "override is not authorized for this slot");
        require(it.value().is_object(), "override must be an object");
        require(upper(text(it.value(), "material")) == "PETG", "brown slot override must be PETG");
        require(!color_text(text(it.value(), "color")).empty(), "override needs an explicit approximate color");
        require(!text(it.value(), "label").empty(), "override needs an explicit user label");
    }

    const auto aces = indexed(state.at("aces"), "aces");
    const auto heads = indexed(state.at("toolheads"), "toolheads");
    require(heads.size() == 4, "toolheads must include every head 0..3");
    std::set<int> ace_heads;
    for (const auto& value : state.at("ace_heads")) {
        const int h = index(value, "ace_heads value");
        require(ace_heads.insert(h).second, "ace_heads contains a duplicate");
    }
    std::map<int, int> ace_to_head;
    for (int h = 0; h < 4; ++h) {
        const std::string key = std::to_string(h);
        const json& feeders = state.at("head_feeder");
        const json& mapping = state.at("head_ace");
        require(feeders.contains(key) && feeders.at(key).is_boolean() && mapping.contains(key), "head mapping is incomplete");
        const bool feeder = feeders.at(key).get<bool>();
        const int unit = index(mapping.at(key), "head_ace mapping");
        require(feeder != (ace_heads.count(h) != 0), "ace_heads conflicts with head_feeder for head " + key);
        const json& head = *heads.at(h);
        if (head.contains("feeder"))
            require(boolean(head, "feeder") == feeder, "toolhead feeder conflicts with head_feeder");
        if (!feeder) {
            require(aces.count(unit) != 0, "head maps to an absent ACE unit");
            require(ace_to_head.emplace(unit, h).second, "multiple heads map to the same ACE unit");
            if (head.contains("ace") && !head.at("ace").is_null())
                require(index(head.at("ace"), "toolhead ace") == unit, "toolhead ACE conflicts with head_ace");
            if (head.contains("slot") && !head.at("slot").is_null())
                index(head.at("slot"), "toolhead slot");
        } else {
            require(!head.contains("ace") || head.at("ace").is_null(), "external head unexpectedly names an ACE");
            require(!head.contains("slot") || head.at("slot").is_null(), "external head unexpectedly names an ACE slot");
        }
    }
    require(ace_to_head.size() == aces.size(), "unmapped ACE unit makes routing ambiguous");
    if (state.contains("wiring")) {
        require(state.at("wiring").is_array() && state.at("wiring").size() <= 4, "wiring must be a bounded array");
        std::set<int> wired_heads;
        for (const auto& wire : state.at("wiring")) {
            require(wire.is_object() && wire.contains("ace") && wire.contains("slot") && wire.contains("toolhead"),
                    "incomplete wiring entry");
            const int unit = index(wire.at("ace"), "wiring ACE");
            const int h = index(wire.at("toolhead"), "wiring head");
            index(wire.at("slot"), "wiring slot");
            require(wired_heads.insert(h).second, "duplicate wiring head");
            require(ace_to_head.count(unit) && ace_to_head.at(unit) == h, "wiring conflicts with head mapping");
        }
    }

    json result = {{"printer_id", printer_id}, {"host", host}, {"rows", json::array()}, {"warnings", json::array()}};
    json& warnings = result["warnings"];
    // ace_head is a legacy scalar. It must never override the validated plural mapping.
    if (state.contains("ace_head") && (ace_heads.size() != 1 || state.at("ace_head") != *ace_heads.begin()))
        warnings.push_back("Legacy ace_head disagrees; used validated ace_heads/head_feeder/head_ace mapping");
    for (const auto& entry : aces) {
        const int unit = entry.first;
        const json& ace = *entry.second;
        require(ace.contains("slots"), "ACE slots missing");
        const auto slots = indexed(ace.at("slots"), "ACE slots");
        const bool connected = boolean(ace, "connected");
        for (int s = 0; s < 4; ++s) {
            const std::string address = "ACE:" + std::to_string(unit) + ":" + std::to_string(s);
            const std::string label = "ACE " + std::to_string(unit + 1) + " slot " + std::to_string(s + 1);
            const json empty = json::object();
            const json& slot = slots.count(s) ? *slots.at(s) : empty;
            const std::string status = text(slot, "state").empty() ? text(slot, "status") : text(slot, "state");
            const bool available = connected && status == "ready";
            std::string source = text(slot, "source");
            const bool identity_supplied = source == "rfid" || source == "override" || !text(slot, "ident_src").empty();
            if (source.empty())
                source = "unknown";
            json normalized = row(printer_id, address, label, ace_to_head.at(unit), s, unit, s, slot,
                                  available, identity_supplied, source, warnings);
            if (!slots.count(s))
                warnings.push_back(label + ": missing from snapshot; retained as unavailable/unknown");
            if (overrides.contains(address)) {
                if (available && normalized.at("material").get<std::string>().empty() && normalized.at("color").is_null()) {
                    const json& supplied = overrides.at(address);
                    normalized["label"] = label + " — " + text(supplied, "label") + " (user label; approximate color)";
                    normalized["material"] = upper(text(supplied, "material"));
                    normalized["color"] = color_text(text(supplied, "color"));
                    normalized["source"] = "user";
                    normalized["identified"] = false;
                    normalized["user_override"] = true;
                } else {
                    warnings.push_back(label + ": local label not applied to unavailable or partially/fully labelled replacement filament");
                }
            }
            result["rows"].push_back(std::move(normalized));
        }
    }
    for (const auto& entry : heads) {
        const int h = entry.first;
        if (ace_heads.count(h))
            continue; // Its current ACE spool already appears above; never duplicate it as external.
        const json& head = *entry.second;
        const bool available = boolean(head, "module_exist") && boolean(head, "filament_detected") && !boolean(head, "load_failed");
        // In multiACE, head_source_known means known ACE routing. It is always
        // false for external feeders, whose identity comes from print_task_config.
        result["rows"].push_back(row(printer_id, "EXTERNAL:" + std::to_string(h), "External nozzle " + std::to_string(h + 1),
                                     h, 0, nullptr, nullptr, head, available, true, "printer-reported", warnings));
    }
    return result;
}

}} // namespace Slic3r::SnorcaFilamentSync

#endif
