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

#include "editor_scene_importer_stl.h"
#include <cstring>
#include <vector>

void EditorSceneImporterSTL::get_extensions(List<String> *r_extensions) const
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


Node * EditorSceneImporterSTL::import_scene(const String &p_path, uint32_t p_flags, int p_bake_fps, List<String> *r_missing_deps, Error *r_err)
{
	print_error("loader for stl has run once!");

	Error err;
	FileAccess *f = FileAccess::open(p_path, FileAccess::READ, &err);
	ERR_FAIL_COND_V(!f, NULL);

	PoolByteArray data;
	bool is_binary = false;
	data.resize(f->get_len());
	f->get_buffer(data.write().ptr(), data.size());
	PoolByteArray fbx_header;
	fbx_header.resize(80);
	for (int32_t byte_i = 0; byte_i < 80; byte_i++) {
		fbx_header.write()[byte_i] = data.read()[byte_i];
	}

	struct STLVector3
	{
		float x,y,z;

		Vector3 to_godot() const
		{
			return Vector3(x,y,z);
		}
	};

	struct STLFace
	{
		STLFace( STLVector3 v1, STLVector3 v2, STLVector3 v3, STLVector3 p_normal)
		{
			face[0] = v1;
			face[1] = v2;
			face[2] = v3;
			normal = p_normal;
		};

		STLVector3 face[3];
		STLVector3 normal;
	};

	struct STLMesh
	{
		void add_face( STLFace p_face)
		{
			faces.push_back(p_face);
		}

		void generate_indices( Ref<SurfaceTool> st )
		{
			int p = 0;
			for( int x = 0; x < faces.size(); x++)
			{
				st->add_index(p+2);
				st->add_index(p+1);
				st->add_index(p);
				p+=3;
			}
		}
		std::vector<STLFace> faces;
	};


	String fbx_header_string;
	if (fbx_header.size() >= 0) {
		PoolByteArray::Read r = fbx_header.read();
		fbx_header_string.parse_utf8((const char *)r.ptr(), fbx_header.size());
	}

	// face count position
	const char * start_pos = (const char*) data.write().ptr();
	const char * face_count_pos = start_pos + 80;
	uint32_t face_count = 0;
	memcpy(&face_count,face_count_pos, sizeof(uint32_t));
	const uint32_t expected_max_file_size = face_count * 50 + 84;

	unsigned int file_size = data.size();

	print_verbose("[doc] opening stl file: " + p_path);
	print_verbose("[doc] stl header: " + fbx_header_string);
	MeshInstance * mesh_node = nullptr;
	// safer to check this way as there can be different formatted headers
	if (expected_max_file_size == file_size ) {
		is_binary = true;
		print_verbose("[doc] is binary");
		print_verbose("[doc] face count: " + itos(face_count));

		mesh_node = memnew(MeshInstance);


		STLVector3 *vec;
		STLMesh stl_mesh;
		Ref<SurfaceTool> st;
		st.instance();
		st->begin(Mesh::PRIMITIVE_TRIANGLES);
		const unsigned char *sz = (const unsigned char *) face_count_pos;
		sz += 4;
		for(int x = 0; x < face_count; x++)
		{
			vec = (STLVector3 *) sz;
			STLVector3 normal, v1,v2,v3;
			memcpy(&normal, vec, sizeof(STLVector3));
			vec++;
			memcpy(&v1, vec, sizeof(STLVector3));
			vec++;
			memcpy(&v2, vec, sizeof(STLVector3));
			vec++;
			memcpy(&v3, vec, sizeof(STLVector3));
			vec++;
			sz = (const unsigned char *)vec;
			uint16_t color = *((uint16_t *)sz);
			sz+=2;

			if(color & (1 << 15))
			{
				// do nothing right now...
			}
			STLFace face(v1,v2,v3,normal);

			// add face
			stl_mesh.add_face(face);

			print_verbose("v1: "+ v1.to_godot());
			print_verbose("v2: "+ v2.to_godot());
			print_verbose("v3: "+ v3.to_godot());
			print_verbose("nr: " + normal.to_godot());
			Vector3 normal_godot = normal.to_godot();


			st->add_normal(normal_godot);
			st->add_vertex(v1.to_godot());
			st->add_normal(normal_godot);
			st->add_vertex(v2.to_godot());
			st->add_normal(normal_godot);
			st->add_vertex(v3.to_godot());
		}

		st->generate_tangents();
		stl_mesh.generate_indices(st);

		Ref<ArrayMesh> array_mesh;
		array_mesh.instance();
		Array triangle_mesh = st->commit_to_arrays();
		array_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, triangle_mesh );

		mesh_node->set_mesh(array_mesh);
	} else {
		print_verbose("[doc] is ascii");
	}


	return mesh_node;
}