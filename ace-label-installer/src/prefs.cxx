// Keeps FLTK from reading or writing its preference files (fltk.prefs), so the program writes nothing
// to disk except a log the person chooses to save.
#include <FL/Fl_Preferences.H>
extern "C" void ace_no_prefs_files(void) { Fl_Preferences::file_access(Fl_Preferences::NONE); }
