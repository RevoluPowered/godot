/*************************************************************************/
/*  app_protocol.cpp                                                     */
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

#include "app_protocol.h"

#include "core/config/project_settings.h"
#include "core/os/memory.h"
#include "core/os/os.h"

AppProtocol *AppProtocol::singleton = nullptr;

void AppProtocol::initialize() {
	if (singleton == nullptr) {
		singleton = memnew(AppProtocol);
	}
}
void AppProtocol::finalize() {
	if (singleton != nullptr) {
		memdelete(singleton);
	}
}

AppProtocol *AppProtocol::get_singleton() {
	return singleton;
}

void AppProtocol::register_project_settings() {
	GLOBAL_DEF("app-protocol/enable_app_protocol", false);
	GLOBAL_DEF("app-protocol/editor_launch_enabled", false);
	GLOBAL_DEF("app-protocol/protocol_name", "godotapp");
	GLOBAL_DEF("app-protocol/editor_launch_path", ProjectSettings::get_singleton()->get_resource_path());

	ProjectSettings *projectSettings = ProjectSettings::get_singleton();

	if (!(bool)projectSettings->get("app-protocol/enable_app_protocol")) {
		return;
	}

	String protocol_name = projectSettings->get("app-protocol/protocol_name");

	// Do you want to ensure the project can be launched from editor project folder?
	if ((bool)projectSettings->get("app-protocol/editor_launch_enabled") && !protocol_name.is_empty()) {
		OS::get_singleton()->register_protocol(protocol_name);
	}
}
