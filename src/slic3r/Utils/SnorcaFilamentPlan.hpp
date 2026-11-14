#pragma once
#include <algorithm>
#include <nlohmann/json.hpp>
#include <string>
#include <set>
#include <map>
#include <vector>
#include <stdexcept>
#include "SnorcaColourName.hpp" // SNORCA: CIEDE2000 for the closest-colour remap
#include "SnorcaFilamentSync.hpp" // SNORCA: detail::upper (already included before this file by Plater.cpp)
namespace Slic3r { namespace SnorcaFilamentSync {
// Native Device connections include the MQTT control port. Inventory always uses HTTP port80.
// Normalize the validated head-mode feed map to the native FOS accessory controls.
// FOS numbers head-mode units by ascending driven head, so reject a topology it cannot represent.
inline nlohmann::json topology(const nlohmann::json& inventory)
{
    using J=nlohmann::json;std::map<int,int> head_units;std::set<int> units;
    for(const auto& row:inventory.at("rows"))if(!row.at("ace_unit").is_null()){
        const int head=row.at("nozzle"),unit=row.at("ace_unit");
        if(head<0||head>3||unit<0||unit>3)throw std::invalid_argument("Unrepresentable ACE topology.");
        if(head_units.count(head)&&head_units.at(head)!=unit)throw std::invalid_argument("Several ACE units share a head.");
        head_units[head]=unit;units.insert(unit);
    }
    if(head_units.empty())throw std::invalid_argument("No ACE topology was reported.");
    J mask=J::array({false,false,false,false});int ordinal=0;
    for(const auto& entry:head_units){if(entry.second!=ordinal++)throw std::invalid_argument("ACE unit numbering differs from multiACE head-mode numbering; palette retained."); /* SNORCA: rename, no "FOS" shown */mask[entry.first]=true;}
    return J{{"mms_system","multiace"},{"fos_mms_topology","head"},{"fos_mms_unit_count",units.size()},
             {"fos_mms_head_ace",mask},{"mms_host",inventory.at("host")}};
}
// Pure transaction planner. It never mutates the supplied project or the printer.
inline nlohmann::json plan(const nlohmann::json& current, const nlohmann::json& inventory,
                           const nlohmann::json& compatible_presets, bool explicit_sync,
                           unsigned maximum_rows)
{
    using J=nlohmann::json;
    const auto& incoming=inventory.at("rows");
    if (!incoming.is_array() || incoming.empty()) throw std::invalid_argument("The printer inventory is empty.");
    J rows=current.at("rows"), warnings=inventory.at("warnings");
    bool already_owned=false;for(const auto& row:rows)if(!row.value("ownership",J::object()).empty())already_owned=true;
    bool only_unchanged_owned=!rows.empty(),other_printer_owned=!rows.empty();
    for(const auto& row:rows){const J own=row.value("ownership",J::object());
        if(own.value("printer_id",std::string())==inventory.at("printer_id"))other_printer_owned=false;
        if(own.empty()||row.value("preset",std::string())!=own.value("preset",std::string())||row.value("color",std::string())!=own.value("color",std::string())||row.value("nozzle",-1)!=own.value("nozzle",-2)||row.value("mms_slot",-1)!=own.value("mms_slot",-2))only_unchanged_owned=false;}
    const bool repair_granted=current.value("verified_empty_repair",false);
    const bool clean_empty=(explicit_sync||current.value("untouched_empty_palette",false)||repair_granted) && !current.at("has_objects").get<bool>() && (!already_owned||(only_unchanged_owned&&other_printer_owned)||repair_granted) && current.value("project",std::string()).empty();
    // The first sync of a new empty project adopts its startup palette, with a full Undo snapshot.
    // A populated plate retains all indices, including unowned/manual rows and painted assignments.
    if(clean_empty) rows=J::array();
    std::set<std::string> seen;
    unsigned manual_preserved=0;
    for(const auto& row:rows) if(row.value("ownership",J::object()).empty()) ++manual_preserved;
    for(const auto& feed:incoming) {
        const std::string key=feed.at("key");
        if(!seen.insert(key).second)throw std::invalid_argument("Duplicate printer feed identity.");
        size_t at=rows.size();
        for(size_t i=0;i<rows.size();++i) {
            const J own=rows[i].value("ownership",J::object());
            if(own.value("key",std::string())==key) {
                if(at!=rows.size())throw std::invalid_argument("Duplicate synchronized project row.");
                at=i;
            }
        }
        if(at==rows.size()) {
            std::vector<size_t> candidates;
            for(size_t i=0;i<rows.size();++i) {
                const auto own=rows[i].value("ownership",J::object());
                if(rows[i].value("nozzle",-1)==feed.at("nozzle").get<int>() && rows[i].value("mms_slot",-1)==feed.at("mms_slot").get<int>()) {
                    const bool other_printer = !own.empty() &&
                        own.value("printer_id", std::string()) != inventory.at("printer_id");
                    // Reuse the project row index for the same physical slot.
                    if (own.empty() || other_printer) candidates.push_back(i);
                }
            }
            if(candidates.size()>1)throw std::invalid_argument("Several project rows share one physical nozzle/slot. Palette retained; resolve that mapping first.");
            if(candidates.size()==1){at=candidates.front();if(manual_preserved)--manual_preserved;}
        }
        const bool existing=at<rows.size();
        J row=existing?rows[at]:J::object();
        const J old=row.value("ownership",J::object());
        const std::string material=feed.value("material",std::string());
        std::string selected;
        // Keep calibrated/custom presets when their material already matches.
        const std::string old_preset=row.value("preset",std::string());
        const bool owned=existing&&!old.empty();
        const bool user_label_retained=owned&&old.value("user_override",false)&&!feed.value("user_override",false);
        bool manual_color=owned&&(row.value("color",std::string())!=old.value("color",std::string())||user_label_retained);
        bool manual_preset=owned&&(old_preset!=old.value("preset",std::string())||user_label_retained);
        bool manual_route=owned&&(row.value("nozzle",-1)!=old.value("nozzle",-1)||row.value("mms_slot",-1)!=old.value("mms_slot",-1));
        if(manual_preset)selected=old_preset;
        for(const auto& preset:compatible_presets)
            if(!manual_preset&&preset.at("name")==old_preset && preset.at("material")==material)selected=old_preset;
        if(selected.empty()&&!material.empty())
            for(const auto& preset:compatible_presets)if(preset.at("material")==material){selected=preset.at("name");if(preset.value("generic",false))break;}
        if(selected.empty()) {
            if(material.empty()) {
                // A visible, explicitly unidentified placeholder is never reported as a measured material.
                if(!old_preset.empty()) selected=old_preset;
                else if(!compatible_presets.empty()) selected=compatible_presets[0].at("name");
                warnings.push_back(feed.at("label").get<std::string>()+": material unidentified; preset is only a placeholder.");
            } else throw std::invalid_argument("No compatible local "+material+" filament preset. Previous palette retained.");
        }
        if(selected.empty())throw std::invalid_argument("No compatible local filament presets are available.");
        const std::string color=feed.at("color").is_string()?feed.at("color").get<std::string>():row.value("color",std::string("#808080"));
        // Once a user changes an imported field, preserve that manual override on subsequent refreshes.
        if(!manual_color)row["color"]=color;
        if(!manual_preset)row["preset"]=selected;
        if(!manual_route){row["nozzle"]=feed.at("nozzle");row["mms_slot"]=feed.at("mms_slot");}
        if(manual_color||manual_preset||manual_route)warnings.push_back(feed.at("label").get<std::string>()+": manual project override retained.");
        J ownership={{"key",key},{"printer_id",inventory.at("printer_id")},{"host",inventory.at("host")},
            {"label",feed.at("label")},{"material",material},{"source",feed.at("source")},
            {"identified",feed.at("identified")},{"available",feed.at("available")},
            {"user_override",feed.at("user_override")},{"color",color},{"preset",selected},
            {"nozzle",feed.at("nozzle")},{"mms_slot",feed.at("mms_slot")}};
        // Keep the import baseline for overridden fields, so the next poll cannot adopt a manual value as its own.
        if(manual_color)ownership["color"]=old.at("color");
        if(manual_preset)ownership["preset"]=old.at("preset");
        if(manual_route){ownership["nozzle"]=old.at("nozzle");ownership["mms_slot"]=old.at("mms_slot");}
        if(user_label_retained){ownership["user_override"]=true;ownership["source"]="user";ownership["identified"]=false;ownership["label"]=old.at("label");ownership["material"]=old.at("material");ownership["printer_reported"]=feed;warnings.push_back("User-supplied label retained; printer now reports different or unavailable metadata. Review before replacing it.");}
        row["ownership"]=ownership;
        if(existing)rows[at]=row;else rows.push_back(row);
    }
    // Empty plate: no object references a filament index, so drop unrouted leftover rows and number rows by nozzle, then slot.
    if(!current.at("has_objects").get<bool>()){
        std::vector<J> kept;
        for(const auto& row:rows)if(!(row.value("ownership",J::object()).empty()&&row.value("nozzle",-1)<0))kept.push_back(row);
        std::stable_sort(kept.begin(),kept.end(),[](const J& a,const J& b){
            const int an=a.value("nozzle",-1),bn=b.value("nozzle",-1);
            if((an>=0)!=(bn>=0))return an>=0;
            if(an!=bn)return an<bn;
            return a.value("mms_slot",0)<b.value("mms_slot",0);
        });
        rows=J::array();
        for(const auto& row:kept)rows.push_back(row);
        manual_preserved=0;
        for(const auto& row:rows)if(row.value("ownership",J::object()).empty())++manual_preserved;
    }
    if(rows.size()>maximum_rows)throw std::invalid_argument("Printer feeds exceed the filament limit; manual project rows were not removed.");
    return J{{"rows",rows},{"warnings",warnings},{"printer_rows",incoming.size()},{"manual_rows_preserved",manual_preserved},
             {"clean_empty_palette",clean_empty},{"printer_id",inventory.at("printer_id")},{"host",inventory.at("host")}};
}
// SNORCA (user request 2026-11-12, "make the filament pool match the available filaments on the printer itself"): the pool is  SNORCA
// EXACTLY the selected printer's feeds, in the printer's order (nozzle, then slot), numbered straight through.        SNORCA
// Cause of the extra rows it replaces: the filament list is global (it is not switched with the printer preset), and  SNORCA
// plan() above reused another printer's rows by nozzle/slot and never removed rows owned by another printer, so      SNORCA
// switching printer A <-> printer B accumulated both printers' feeds (10 rows instead of 7).                                  SNORCA
// Rows that are not a feed of this printer (another printer's, unowned, or a feed no longer reported):               SNORCA
//  - not used by any object: removed;                                                                                  SNORCA
//  - used by an object: removed, and the objects move to the printer row of the same material with the closest        SNORCA
//    colour (CIEDE2000), else to the closest colour of any material. Round button and Auto alike (2026-11-13); Auto   SNORCA
//    keeps them at the END of the pool only when current.keep_used_not_on_printer is set (an Auto move was undone).   SNORCA
// Hand edits of a printer row (colour, preset, nozzle or slot changed by the user): the round button replaces them    SNORCA
// with the printer's values; Auto keeps them and reports them as not following the printer.                           SNORCA
// current: {rows:[{preset,color,nozzle,mms_slot,ownership}], used:[0-based row indices used by objects]}.             SNORCA
// Returns rows, remap (old index -> new index, -1 = unused row dropped) and counts for the status line and the log.   SNORCA
inline nlohmann::json plan_exact(const nlohmann::json& current, const nlohmann::json& inventory,                       // SNORCA
                                 const nlohmann::json& compatible_presets, bool manual, unsigned maximum_rows)        // SNORCA
{                                                                                                                       // SNORCA
    using J=nlohmann::json;                                                                                             // SNORCA
    const auto& incoming=inventory.at("rows");                                                                          // SNORCA
    if(!incoming.is_array()||incoming.empty())throw std::invalid_argument("The printer inventory is empty.");          // SNORCA
    const J old=current.at("rows"); J warnings=inventory.at("warnings");                                               // SNORCA
    std::set<int> used; for(const auto& u:current.value("used",J::array()))used.insert(u.get<int>());                 // SNORCA
    auto material_of=[&](const std::string& preset){for(const auto& p:compatible_presets)if(p.at("name")==preset)return p.at("material").get<std::string>();return std::string();}; // SNORCA
    std::vector<J> feeds; std::set<std::string> seen;                                                                  // SNORCA
    for(const auto& f:incoming){if(!seen.insert(f.at("key").get<std::string>()).second)throw std::invalid_argument("Duplicate printer feed identity.");feeds.push_back(f);} // SNORCA
    std::stable_sort(feeds.begin(),feeds.end(),[](const J& a,const J& b){                                             // SNORCA
        const int an=a.at("nozzle"),bn=b.at("nozzle");if(an!=bn)return an<bn;return a.at("mms_slot").get<int>()<b.at("mms_slot").get<int>();}); // SNORCA
    J rows=J::array(),hand=J::array(),kept=J::array(),removed=J::array(),moved=J::array();                             // SNORCA
    std::vector<int> remap(old.size(),-1);                                                                              // SNORCA
    for(const auto& feed:feeds){                                                                                        // SNORCA
        const std::string key=feed.at("key");                                                                           // SNORCA
        int at=-1;                                                                                                      // SNORCA
        for(size_t i=0;i<old.size();++i){const J own=old[i].value("ownership",J::object());                             // SNORCA
            if(remap[i]<0&&own.value("key",std::string())==key&&own.value("printer_id",std::string())==inventory.at("printer_id")){at=int(i);break;}} // SNORCA
        const J prev=at>=0?old[at]:J::object(); const J own=prev.value("ownership",J::object());                       // SNORCA
        const std::string material=feed.value("material",std::string()), old_preset=prev.value("preset",std::string()); // SNORCA
        std::string selected;                                                                                           // SNORCA
        for(const auto& p:compatible_presets)if(p.at("name")==old_preset&&p.at("material")==material)selected=old_preset; // SNORCA
        if(selected.empty()&&!material.empty())for(const auto& p:compatible_presets)if(p.at("material")==material){selected=p.at("name");if(p.value("generic",false))break;} // SNORCA
        if(selected.empty()){                                                                                           // SNORCA
            if(!material.empty())throw std::invalid_argument("No compatible local "+material+" filament preset. Previous palette retained."); // SNORCA
            selected=!old_preset.empty()?old_preset:(compatible_presets.empty()?std::string():compatible_presets[0].at("name").get<std::string>()); // SNORCA
            warnings.push_back(feed.at("label").get<std::string>()+": material unidentified; preset is only a placeholder."); } // SNORCA
        if(selected.empty())throw std::invalid_argument("No compatible local filament presets are available.");        // SNORCA
        const std::string color=feed.at("color").is_string()?feed.at("color").get<std::string>():prev.value("color",std::string("#808080")); // SNORCA
        J row={{"preset",selected},{"color",color},{"nozzle",feed.at("nozzle")},{"mms_slot",feed.at("mms_slot")}};    // SNORCA
        J ownership={{"key",key},{"printer_id",inventory.at("printer_id")},{"host",inventory.at("host")},              // SNORCA
            {"label",feed.at("label")},{"material",material},{"source",feed.at("source")},                              // SNORCA
            {"identified",feed.at("identified")},{"available",feed.at("available")},                                    // SNORCA
            {"user_override",feed.at("user_override")},{"color",color},{"preset",selected},                             // SNORCA
            {"nozzle",feed.at("nozzle")},{"mms_slot",feed.at("mms_slot")}};                                            // SNORCA
        // Auto only: a field the user changed after the last import stays, and the import baseline stays with it,   SNORCA
        // so the next poll still sees it as a hand edit (same rule as plan() above).                                 SNORCA
        if(!manual&&at>=0&&!own.empty()){                                                                               // SNORCA
            bool edited=false;                                                                                          // SNORCA
            if(prev.value("color",std::string())!=own.value("color",std::string())){row["color"]=prev.at("color");ownership["color"]=own.at("color");edited=true;} // SNORCA
            if(old_preset!=own.value("preset",std::string())){row["preset"]=old_preset;ownership["preset"]=own.at("preset");edited=true;} // SNORCA
            if(prev.value("nozzle",-1)!=own.value("nozzle",-1)||prev.value("mms_slot",-1)!=own.value("mms_slot",-1)){  // SNORCA
                row["nozzle"]=prev.at("nozzle");row["mms_slot"]=prev.at("mms_slot");ownership["nozzle"]=own.at("nozzle");ownership["mms_slot"]=own.at("mms_slot");edited=true;} // SNORCA
            if(edited)hand.push_back(feed.at("label"));                                                                 // SNORCA
        }                                                                                                               // SNORCA
        row["ownership"]=ownership;                                                                                     // SNORCA
        if(at>=0)remap[at]=int(rows.size());                                                                            // SNORCA
        rows.push_back(row);                                                                                            // SNORCA
    }                                                                                                                   // SNORCA
    const size_t printer_rows=rows.size();                                                                              // SNORCA
    for(size_t i=0;i<old.size();++i){                                                                                   // SNORCA
        if(remap[i]>=0)continue;                                                                                        // SNORCA
        const J own=old[i].value("ownership",J::object());                                                              // SNORCA
        const std::string what=std::to_string(i+1)+" "+old[i].value("preset",std::string())+" "+old[i].value("color",std::string()) // SNORCA
            +(own.empty()?std::string(" (not from a printer)"):" (from "+own.value("host",std::string())+")");           // SNORCA
        if(!used.count(int(i))){removed.push_back(what);continue;}                                                      // SNORCA
        // SNORCA (user request 2026-11-13, "It didn't automatically refresh like it should"): Auto now moves these objects too,  SNORCA
        // by the same rule as the round button. Only when the caller asks (keep_used_not_on_printer: an automatic   SNORCA
        // move was just undone with Edit > Undo) does Auto keep the row at the end as before.                        SNORCA
        if(!manual&&current.value("keep_used_not_on_printer",false)){remap[i]=int(rows.size());rows.push_back(old[i]);kept.push_back(what);continue;} // SNORCA
        // Used row not on this printer (round button and Auto): same material, closest colour; else closest colour.  SNORCA
        const std::string m=detail::upper(material_of(old[i].value("preset",std::string())));                           // SNORCA
        int a[3],b[3]; const bool ca=SnorcaColour::parse(old[i].value("color",std::string()),a);                          // SNORCA
        int best=-1; double bd=1e18; bool same_material=false;                                                          // SNORCA
        for(int pass=0;pass<2&&best<0;++pass)for(size_t k=0;k<printer_rows;++k){                                        // SNORCA
            if(pass==0&&(m.empty()||rows[k].at("ownership").value("material",std::string())!=m))continue;              // SNORCA
            const double d=ca&&SnorcaColour::parse(rows[k].value("color",std::string()),b)?SnorcaColour::delta_e(a,b):1e9; // SNORCA
            if(d<bd){bd=d;best=int(k);same_material=pass==0;}}                                                          // SNORCA
        remap[i]=best; removed.push_back(what);                                                                         // SNORCA
        // SNORCA (2026-11-13): plain names for the status line, e.g. "8 Black PLA -> 2 Black PLA".                       SNORCA
        auto plain=[](const std::string& hex,const std::string& mat){const std::string n=SnorcaColour::name(hex);         // SNORCA
            return (n.empty()?hex:n)+(mat.empty()?std::string():" "+mat);};                                             // SNORCA
        moved.push_back({{"from",i+1},{"to",best+1},{"same_material",same_material},{"what",what},                       // SNORCA
            {"from_name",plain(old[i].value("color",std::string()),m)},                                                 // SNORCA
            {"to_name",plain(rows[best].value("color",std::string()),rows[best].at("ownership").value("material",std::string()))}}); // SNORCA
    }                                                                                                                   // SNORCA
    if(rows.size()>maximum_rows)throw std::invalid_argument("Printer feeds exceed the filament limit; palette retained."); // SNORCA
    return J{{"rows",rows},{"remap",remap},{"warnings",warnings},{"printer_rows",printer_rows},{"hand_edited",hand},    // SNORCA
             {"kept_not_on_printer",kept},{"removed",removed},{"moved",moved},{"printer_id",inventory.at("printer_id")},{"host",inventory.at("host")}}; // SNORCA
}                                                                                                                       // SNORCA
} }
