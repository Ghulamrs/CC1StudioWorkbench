// The way in. Windows Forms wants a single-threaded apartment, and says so by
// refusing to show some dialogs without one.
//
// Everything is wrapped, and what goes wrong is written down. A GUI that dies
// on a machine you are not sitting at tells you nothing otherwise.

#include <cstdio>

#include "MainForm.h"



using namespace System;
using namespace System::Windows::Forms;

static void Note(String^ what) {
    try {
        System::IO::File::AppendAllText("ed1gui.log",
                                        DateTime::Now.ToString("HH:mm:ss") + "  " + what +
                                            Environment::NewLine);
    } catch (Exception^) {
        // Nowhere to write is not worth dying for.
    }
}

static void OnUnhandled(Object^, UnhandledExceptionEventArgs^ e) {
    Note("unhandled: " + e->ExceptionObject->ToString());
}

[STAThreadAttribute]
int main(array<String^>^ arguments) {
    // Its own debugger, since the machine has none.
    // Its own debugger. There is none installed on the machine this is built
    // for, and a crash with no stack is a crash you cannot fix.
    ed1_watch_for_faults("ed1-fault.log");
    AppDomain::CurrentDomain->UnhandledException +=
        gcnew UnhandledExceptionEventHandler(OnUnhandled);

    try {
        Note("starting, " + arguments->Length + " arguments");
        Application::EnableVisualStyles();
        Application::SetCompatibleTextRenderingDefault(false);

        // ed1gui [project-directory] [file ...] - every file named gets a tab.
        String^ directory = arguments->Length > 0 ? arguments[0] : nullptr;
        array<String^>^ files =
            gcnew array<String^>(arguments->Length > 1 ? arguments->Length - 1 : 0);
        for (int i = 1; i < arguments->Length; ++i) files[i - 1] = arguments[i];

        Note("building the window");
        ed1gui::MainForm^ window = gcnew ed1gui::MainForm(directory, files);
        Note("window built, running");
        Application::Run(window);
        Note("closed cleanly");
    } catch (Exception^ problem) {
        Note("caught: " + problem->ToString());
        return 1;
    }
    return 0;
}
