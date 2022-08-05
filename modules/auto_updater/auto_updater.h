#ifndef GODOT_SF_AUTO_UPDATER_H
#define GODOT_SF_AUTO_UPDATER_H

#include "core/object/callable_method_pointer.h"
#include "core/string/ustring.h"
#include "scene/main/http_request.h"

// Func ptrs
using DownloadProgressReport = void (*)(int p_progress);
using DownloadCompleted = void (*)(void);
using DownloadFailure = void (*)(const String &p_error);

class AutoDownloader : public Node {
	GDCLASS(AutoDownloader, Node);

public:
	HTTPRequest request;

	void RequestCompleted(int result, int response_code, PackedStringArray headers, PackedByteArray body) {
		printf("Request Completed\n");
	}

	void Download(const String &p_file) {
		Error err = request.request(p_file);
		request.connect("request_completed", callable_mp(this, &AutoDownloader::RequestCompleted));
		if (err != OK) {
			print_error("error");
		}
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
