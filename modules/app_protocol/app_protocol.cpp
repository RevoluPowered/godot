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
#include "thirdparty/ipc/ipc.h"

#include "core/config/project_settings.h"
#include "core/os/memory.h"

AppProtocol *AppProtocol::singleton = nullptr;

AppProtocol::AppProtocol() {
}

AppProtocol::~AppProtocol() {
}

void AppProtocol::_bind_methods() {
	ClassDB::bind_method(D_METHOD("poll_server"), &AppProtocol::poll_server);
	// TODO: add signal registration for incoming messages.
}

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
	GLOBAL_DEF("app_protocol/enable_app_protocol", false);
	GLOBAL_DEF("app_protocol/editor_launch_enabled", false);
	GLOBAL_DEF("app_protocol/protocol_name", "godotapp");
	GLOBAL_DEF("app_protocol/require_single_instance_socket", true);

	ProjectSettings *projectSettings = ProjectSettings::get_singleton();

	if (!(bool)projectSettings->get("app_protocol/enable_app_protocol")) {
		return;
	}

	const String protocol_name = projectSettings->get("app_protocol/protocol_name");

#ifdef TOOLS_ENABLED
	// If a server is already registered there is no way to register another protocol until it closes.
	if (!is_server_already_running()) {
		// Do you want to ensure the project can be launched from editor project folder?
		if (!protocol_name.is_empty()) {
			if (this->Server == nullptr) {
				print_verbose("Starting IPC server");
				this->Server = memnew(IPCServer);
				this->Server->setup();
				this->Server->add_receive_callback(&AppProtocol::on_server_get_message);
				// from this point onwards we need to call poll regularly.
			}
			CompiledPlatform.register_protocol_handler(protocol_name);
		}
	}
#else
	CompiledPlatform.register_protocol_handler(protocol_name);
#endif
}

bool AppProtocol::is_server_already_running() {
	// connection will be refused if it is - connection will be closed instantly.
	IPCClient client;
	return client.setup();
}

void AppProtocol::poll_server() {
	if (get_singleton()->Server) {
		get_singleton()->Server->poll();
	}
}

void AppProtocol::on_server_get_message(const char *p_str, int strlen) {
	print_error("Got message from client: " + String(p_str));
}

bool ProtocolPlatformImplementation::validate_protocol(const String &p_protocol) {
#ifdef TOOLS_ENABLED
	// This warning should be shared between platforms, so putting it here.
	// However, it's not technically related to validation.
	WARN_PRINT("Registering protocols in the editor likely won't work as expected, since it will point to the editor binary. Consider only doing this in exported projects.");
#endif
	// https://datatracker.ietf.org/doc/html/rfc3986#section-6.2.2.1
	// Protocols can't be empty, must be lowercase, must start with a letter,
	// can only contain letters, numbers, '+', '-', and '.' characters.
	if (p_protocol.is_empty()) {
		return false;
	}
	if (p_protocol[0] > 'z' || p_protocol[0] < 'a') {
		ERR_PRINT("Invalid protocol character: " + String::chr(p_protocol[0]) + ". Protocols must start with a lowercase letter.");
		return false;
	}
	for (int i = 1; i < p_protocol.length(); i++) {
		char32_t c = p_protocol[i];
		if (c != '+' && c != '-' && c != '.' && !(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9')) {
			ERR_PRINT("Invalid protocol character: " + String::chr(c) + ". Protocols must be lowercase, must start with a letter, can only contain letters, numbers, '+', '-', and '.' characters.");
			return false;
		}
	}
	return true;
}
