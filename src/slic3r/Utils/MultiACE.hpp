#ifndef slic3r_MultiACE_hpp_
#define slic3r_MultiACE_hpp_

// FOS: print host for multiACE (github.com/decay71/multiACE), a Klipper add-on
// for the Snapmaker U1 that drives multiple Anycubic ACE units.
//
// Derives from OctoPrint only to reuse its host / apikey / cafile plumbing and
// make_url(); the wire format is entirely different. The upload is a bare
// multipart POST of the RAW slicer export to the store-only inbox, equivalent
// to the call multiACE documents:
//
//   curl -F "file=@model.gcode" http://<printer-ip>/multiace/api/preflight/inbox
//
// multiACE runs its preflight in the browser when the user picks the file up,
// so there is no start-print post-upload action. The inbox answers 409 for a
// file that already carries multiACE's processed markers, so never send G-code
// that has been through multiace post_process_virtual_toolheads.py.

#include <string>
#include <map>   // SNORCA
#include <vector> // SNORCA

#include <wx/string.h>

#include "OctoPrint.hpp"

namespace Slic3r {

class DynamicPrintConfig;
class Http;

class MultiACE : public OctoPrint
{
public:
    MultiACE(DynamicPrintConfig *config);
    ~MultiACE() override = default;

    const char* get_name() const override;

    bool     test(wxString &curl_msg) const override;
    wxString get_test_ok_msg() const override;
    wxString get_test_failed_msg(wxString &msg) const override;

    // Store-only drop point: nothing to start, so no post-upload actions.
    PrintHostPostUploadActions get_post_upload_actions() const override { return {}; }
    bool has_auto_discovery() const override { return false; }
    bool can_test() const override { return true; }

    bool upload(PrintHostUpload upload_data, ProgressFn progress_fn, ErrorFn error_fn, InfoFn info_fn) const override;

    // Paths are relative to the printer root; nginx maps /multiace/ onto the
    // multiACE backend on 127.0.0.1:7126, stripping the prefix.
    static std::string inbox_path()    { return "multiace/api/preflight/inbox"; }
    static std::string health_path()   { return "multiace/api/health"; }
    static std::string livedata_path() { return "multiace/api/preflight/livedata"; }

    // FOS: one synchronous fetch of the supply topology from the preflight livedata
    // endpoint. Its shape is dictated by preflight_core.build_report and consumed by
    // multiACE's own in-browser preflight, so it cannot drift without breaking them
    // first. Never build on /api/state - that is the dashboard payload and churns.
    struct TopologyReport
    {
        bool        ok          = false;  // a usable head_ctx was parsed
        bool        manual_hold = false;  // 409: a head is set to manual. Fall back to
                                          // manual entry, never treat as an error.
        std::string error;                // network / HTTP / parse detail for the dialog
        std::string mode;                 // "normal" | "multi" | "head" ("single" is
                                          // mapped to multi, matching ace.py's rewrite)
        int         units_seen = 0;       // distinct units in live_slots. LOWER BOUND: a
                                          // unit with no rfid/override-identified spool
                                          // is invisible to this endpoint.
        std::vector<bool>   ace_driven_heads; // head-indexed; head mode only
        std::vector<double> head_nozzles;     // head-indexed supply diameters; may be empty
    };
    static TopologyReport fetch_topology(std::string host);

    // SNORCA: Print plans and starts the job by itself through multiACE's own server-side preflight
    // (POST api/preflight, then api/preflight/print with the colour->target table, as the page's  SNORCA
    // in-printer path does). Only when every used filament gets its own loaded spool of exactly the  SNORCA
    // same type (closest colour wins) and the printer is idle; otherwise the file goes to the inbox  SNORCA
    // as before and the reason is kept here for the GUI, which shows the "did not start" window.  SNORCA
    struct SnorcaOutcome                                                                    // SNORCA
    {                                                                                     // SNORCA
        bool        started = false;   // the printer reported "printing" after the start // SNORCA
        bool        stored  = false;   // the file was put in the inbox instead           // SNORCA
        std::string reason;            // one line: why it did not start                  // SNORCA
        std::string summary;           // one line: filament -> spool table               // SNORCA
        // {what, type, colour} rows for the "did not start" window: what the job needs   // SNORCA
        // (from the G-code) and what the printer has loaded (from multiACE).              // SNORCA
        std::vector<std::vector<std::string>> needs, loaded;                              // SNORCA
        // Started job, one row per filament: {filament, type, job colour, where, spool type, spool colour}. // SNORCA
        std::vector<std::vector<std::string>> plan;                                       // SNORCA
    };                                                                                    // SNORCA
    SnorcaOutcome snorca_outcome() const { return m_snorca_outcome; }                           // SNORCA
    // Rule (2026-11-12): exact filament type, closest colour, one spool per filament.  // SNORCA
    // Fills {t: target_id}; returns false with a one-line reason when it cannot.         // SNORCA
    static bool snorca_assign_head_targets(const std::string &report_json, int index_base,   // SNORCA
                                         std::map<std::string, std::string> &assignment,   // SNORCA
                                         std::string &reason, std::string &summary,        // SNORCA
                                         std::vector<std::vector<std::string>> *plan = nullptr); // SNORCA
private:                                                                                  // SNORCA
    int snorca_auto_start(const PrintHostUpload &upload_data, ProgressFn progress_fn) const; // SNORCA
    mutable SnorcaOutcome m_snorca_outcome;                                                   // SNORCA
};

} // namespace Slic3r

#endif // slic3r_MultiACE_hpp_
