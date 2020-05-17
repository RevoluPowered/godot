/*************************************************************************/
/*  editor_scene_importer_stl.h                                          */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2020 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2020 Godot Engine contributors (cf. AUTHORS.md).   */
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

#ifndef EDITOR_SCENE_IMPORTER_STL_H
#define EDITOR_SCENE_IMPORTER_STL_H

#ifdef TOOLS_ENABLED

#include "core/vector.h"
#include "core/bind/core_bind.h"
#include "core/io/resource_importer.h"

#include "editor/import/resource_importer_scene.h"
#include "editor/project_settings_editor.h"

#include "scene/3d/mesh_instance.h"
#include "scene/3d/spatial.h"
#include "scene/resources/surface_tool.h"

class EditorSceneImporterSTL : public EditorSceneImporter {
private:
	GDCLASS(EditorSceneImporterSTL, EditorSceneImporter);

	struct ImportFormat {
		Vector<String> extensions;
		bool is_default;
	};

protected:
	static void _bind_methods(){}

public:
	EditorSceneImporterSTL() {}
	virtual ~EditorSceneImporterSTL() {}

	virtual void get_extensions(List<String> *r_extensions) const
	{
		const String import_setting_string = "filesystem/import/stl_import/";

		Map<String, ImportFormat> import_format;
		{
			Vector<String> exts;
			exts.push_back("stl");
			ImportFormat import = { exts, true };
			import_format.insert("stl", import);
		}
		// register import setting
		for (Map<String, ImportFormat>::Element *E = import_format.front(); E; E = E->next()) {

			const String use_generic = "use_" + E->key();
			_GLOBAL_DEF(import_setting_string + use_generic, E->get().is_default, true);
			if (ProjectSettings::get_singleton()->get(import_setting_string + use_generic)) {
				for (int32_t i = 0; i < E->get().extensions.size(); i++) {
					r_extensions->push_back(E->get().extensions[i]);
				}
			}
		}
	}
	virtual uint32_t get_import_flags() const
	{
		return IMPORT_SCENE;
	}
	virtual Node *import_scene(const String &p_path, uint32_t p_flags, int p_bake_fps, List<String> *r_missing_deps, Error *r_err = NULL)
	{
		print_error("loader for stl has run once!");
		return nullptr;
	}
};


#endif // TOOLS_ENABLED
#endif // EDITOR_SCENE_IMPORTER_STL_H
