#ifndef EDITOR_SETTINGS_H
#define EDITOR_SETTINGS_H

#include <string>

namespace editor {

// The editor's own configuration, as against a project's.
//
// There are two configuration files now and they answer different questions.
// `ed1.json` in a directory says what that project is: its groups, its indent,
// which compiler and target it wants. This one says what *you* had, on this
// machine - and the first thing it holds is the project you were last in.
//
// That could not go in a project file. Which project was open last is not a
// fact about any project, and putting it in one would leave every ed1.json on
// disk claiming to have been the most recent.
//
// It is an object in a file rather than a path on a line, so that the second
// thing worth remembering does not need a new format or a new file.
namespace settings {

// `.ed1config.json` beside your own files. Empty when the machine will not say
// where those are, in which case nothing is remembered and nothing complains.
std::string fileName();

// The project that was open last, or empty. A directory that has been deleted
// or renamed since is not offered: what is wanted is somewhere to open, not a
// name to fail on.
std::string lastProject();

// Kept for next time. False when it could not be written, which is not worth
// stopping for - the editor still works, it just forgets.
bool rememberProject(const std::string& directory);

// Whether the screen is framed in plain ASCII rather than the box-drawing
// characters. A property of the console you are sitting at - its font may draw
// the junctions from a second face, which breaks the lines at every join - so
// it belongs here beside the project and not in any ed1.json.
bool plainFrame();

// The font the window draws code in, kept as the window spells it - a name and
// a size, and this side does not look inside. A property of the machine you are
// sitting at, like the frame above it: the same ed1.json opened elsewhere must
// not drag along a font that machine may not have. Empty when nothing was ever
// chosen, and the window uses its own default then.
std::string codeFont();
bool rememberCodeFont(const std::string& described);

// Where an unreadable configuration was put, if that happened during this run,
// and empty otherwise.
//
// A file that will not parse used to be treated as no file at all: the editor
// carried on with its defaults, and the first setting changed afterwards wrote
// a fresh object over whatever was there. Nothing said, nothing kept.
//
// Now, once, in this order: the old file is renamed to
// `.ed1config.json.error`, a fresh empty one is written in its place, and this
// says where the old one went so a front end can tell you. The same courtesy
// an ed1.json gets, which is never written over when it will not parse. This is
// less precious than somebody's project file, but it may hold a hand-edit, or a
// key a later version wrote and this one does not know.
std::string setAside();
bool rememberPlainFrame(bool plain);

}  // namespace settings
}  // namespace editor

#endif
