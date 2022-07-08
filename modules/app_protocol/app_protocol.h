/*************************************************************************/
/*  app_protocol.h                                                       */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2022 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2022 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#ifndef APP_PROTOCOL_H
#define APP_PROTOCOL_H

#include "core/config/project_settings.h"
#include "core/os/os.h"
#include "core/string/ustring.h"

class ProtocolPlatformImplementation {
public:
	ProtocolPlatformImplementation(){};
	virtual ~ProtocolPlatformImplementation(){};
	virtual bool validate_protocol(const String &p_protocol);
	virtual Error register_protocol_handler(const String &p_protocol) = 0;
};

class LinuxDesktopProtocol : public ProtocolPlatformImplementation {
public:
	virtual Error register_protocol_handler(const String &p_protocol) {
		ERR_FAIL_COND_V(!validate_protocol(p_protocol), ERR_INVALID_PARAMETER);
		OS *os = OS::get_singleton();

		// Generate the desktop entry.
		const String scheme_handler = "x-scheme-handler/" + p_protocol;
		const String name = "\nName=" + p_protocol.to_upper() + " Protocol Handler";
#ifdef TOOLS_ENABLED
		// tools must call explicit path to application
		const String exec = "\nExec=" + os->get_executable_path() + " --path=\"" + ProjectSettings::get_singleton()->get_resource_path() + "\" --uri=\"%u\"";
#else
		// non tools we assume its an exported game and runs from the current folder - we should test this assumption
		const String exec = "\nExec=" + os->get_executable_path() + " --uri=\"%u\"";
#endif
		const String mime = "\nMimeType=" + scheme_handler + ";\n";
		// Example file:
		// [Desktop Entry]
		// Type=Application
		// Name=MYPROTOCOL Protocol Handler
		// Exec=/path/to/godot --uri="%u"
		// MimeType=x-scheme-handler/myprotocol;
		const String desktop_entry = "[Desktop Entry]\nType=Application" + name + exec + mime;
		// Write the desktop entry to a file.
		const String file_name = p_protocol + "-protocol-handler.desktop";
		{
			const String path = os->get_environment("HOME") + "/.local/share/applications/" + file_name;
			Error err;
			Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE, &err);
			if (err) {
				return err;
			}
			file->store_string(desktop_entry);
		}
		// Register this new file with xdg-mime.
		List<String> args;
		args.push_back("default");
		args.push_back(file_name);
		args.push_back(scheme_handler);
		return os->execute("xdg-mime", args);
	}
};

// TODO: make this swap depending on the compiled platform.
// Here I made the assumption that the compiled platform is what uses this
// In the case you run the editor its the CurrentPlatformDefiniton
// In the case you are using an export template it can also be the CurrentPlatformDefiniton since
// The events are happening at export time, and baked into the application
// If this assumption needs to change, totally open to this.
using CurrentPlatformDefiniton = LinuxDesktopProtocol;

class AppProtocol {
protected:
	static AppProtocol *singleton;

public:
	AppProtocol();
	~AppProtocol();
	static void initialize();
	static void finalize();
	static AppProtocol *get_singleton();
	// this object is compile time, so we always keep the same class.
	CurrentPlatformDefiniton CompiledPlatform;
	void register_project_settings();
};

#endif // APP_PROTOCOL_H