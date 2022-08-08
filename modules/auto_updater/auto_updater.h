#ifndef GODOT_SF_AUTO_UPDATER_H
#define GODOT_SF_AUTO_UPDATER_H

#include "core/object/callable_method_pointer.h"
#include "core/string/ustring.h"
#include "scene/main/http_request.h"
#include <iostream>
#include <fstream>
// Func ptrs
//using DownloadProgressReport = void (*)(int p_progress);
//using DownloadCompleted = void (*)(void);
//using DownloadFailure = void (*)(const String &p_error);

class AutoDownloader : public HTTPRequest {
	GDCLASS(AutoDownloader, HTTPRequest);

public:

	void RequestCompleted(int result, int response_code, PackedStringArray headers, PackedByteArray body) {
		printf("Request Completed result %d, response code %d\n", result, response_code);
		printf("image size bytes: %d\n", body.size() * 8);
		std::ofstream file("image.png");
		for( int x = 0; x < body.size(); ++x) {
			file << body[x];
		}
		file.close();
	}

	static void _bind_methods(){
		ClassDB::bind_method(D_METHOD("Download"), &AutoDownloader::Download);
	}

	Error Download(const String &p_file) {
		Error err = request(p_file);
		connect("request_completed", callable_mp(this, &AutoDownloader::RequestCompleted));
		if (err != OK) {
			print_error("error");
		}

		return err;
	}
};

struct AppVersion {
	int MAJOR_VERSION = 0;
	int MINOR_VERSION = 0;
	int BUILD_REVISION = 0; // increment
};

class AutoUpdater {
public:
	AutoUpdater();
	~AutoUpdater();
	bool check_for_update();
	bool ensure_write_permissions();
	int get_download_progress();
	int download_new_version();
};

#endif //GODOT_SF_AUTO_UPDATER_H
