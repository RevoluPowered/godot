/*************************************************************************/
/*  fbx_mesh_data.h                                                      */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2020 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2020 Godot Engine contributors (cf. AUTHORS.md)    */
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

#ifndef EDITOR_SCENE_FBX_MESH_DATA_H
#define EDITOR_SCENE_FBX_MESH_DATA_H

#include "../tools/import_utils.h"
#include "core/bind/core_bind.h"
#include "core/io/resource_importer.h"
#include "core/vector.h"
#include "editor/import/resource_importer_scene.h"
#include "editor/project_settings_editor.h"
#include "fbx_bone.h"
#include "fbx_node.h"
#include "fbx_skeleton.h"
#include "pivot_transform.h"
#include "scene/3d/mesh_instance.h"
#include "scene/3d/skeleton.h"
#include "scene/3d/spatial.h"
#include "scene/animation/animation_player.h"
#include "scene/resources/animation.h"
#include "scene/resources/surface_tool.h"

#include <thirdparty/assimp/code/FBX/FBXDocument.h>
#include <thirdparty/assimp/code/FBX/FBXImportSettings.h>
#include <thirdparty/assimp/code/FBX/FBXMeshGeometry.h>
#include <thirdparty/assimp/code/FBX/FBXParser.h>
#include <thirdparty/assimp/code/FBX/FBXTokenizer.h>
#include <thirdparty/assimp/code/FBX/FBXUtil.h>
#include <thirdparty/assimp/include/assimp/matrix4x4.h>
#include <thirdparty/assimp/include/assimp/types.h>

struct FBXMeshData;
struct FBXBone;

struct FBXSplitBySurfaceVertexMapping {
	// Original Mesh Data
	Map<size_t, Vector3> vertex_with_id = Map<size_t, Vector3>();
	Vector<Vector2> uv_0, uv_1 = Vector<Vector2>();
	Vector<Vector3> normals = Vector<Vector3>();
	Vector<Color> colors = Vector<Color>();

	void add_uv_0(Vector2 vec) {
		vec.y = 1.0f - vec.y;
		//print_verbose("added uv_0 " + vec);
		uv_0.push_back(vec);
	}

	void add_uv_1(Vector2 vec) {
		vec.y = 1.0f - vec.y;
		uv_1.push_back(vec);
	}

	Vector3 get_normal(int vertex_id, bool &found) const {
		found = false;
		if (vertex_id < normals.size()) {
			found = true;
			return normals[vertex_id];
		}
		return Vector3();
	}

	Color get_colors(int vertex_id, bool &found) const {
		found = false;
		if (vertex_id < colors.size()) {
			found = true;
			return colors[vertex_id];
		}
		return Color();
	}

	Vector2 get_uv_0(int vertex_id, bool &found) const {
		found = false;
		if (vertex_id < uv_0.size()) {
			found = true;
			return uv_0[vertex_id];
		}
		return Vector2();
	}

	Vector2 get_uv_1(int vertex_id, bool &found) const {
		found = false;
		if (vertex_id < uv_1.size()) {
			found = true;
			return uv_1[vertex_id];
		}
		return Vector2();
	}

	void GenerateIndices(Ref<SurfaceTool> st, uint32_t mesh_face_count ) const
	{
		switch(mesh_face_count) {
			case 1: // todo: validate this
				for (int x = 0; x < vertex_with_id.size(); x += 1) {
					st->add_index(x);
					st->add_index(x);
					st->add_index(x);
				}
				break;
			case 2: // todo: validate this
				for (int x = 0; x < vertex_with_id.size(); x += 2) {
					st->add_index(x + 1);
					st->add_index(x + 1);
					st->add_index(x);
				}
				break;
			case 3: {
				// triangle only
				for (int x = 0; x < vertex_with_id.size(); x += 3) {
					st->add_index(x + 2);
					st->add_index(x + 1);
					st->add_index(x);
				}
			} break;
			case 4: {
				// quad conversion to triangle
				for (int x = 0; x < vertex_with_id.size(); x += 4) {
					// complete first side of triangle
					st->add_index(x + 2);
					st->add_index(x + 1);
					st->add_index(x);

					// complete second side of triangle

					// top right
					// bottom right
					// top left

					// first triangle is
					// (x+2), (x+1), (x)
					// second triangle is
					// (x+2), (x), (x+3)

					st->add_index(x + 2);
					st->add_index(x);
					st->add_index(x + 3);

					// anti clockwise rotation in indices
					// note had to reverse right from left here
					// [0](x) bottom right (-1,-1)
					// [1](x+1) bottom left (1,-1)
					// [2](x+2) top left (1,1)
					// [3](x+3) top right (-1,1)

					// we have 4 points
					// we have 2 triangles
					// we have CCW
				}
			} break;
			default:
				print_error("number is not implemented!");
				break;
		}


	}

	void GenerateSurfaceMaterial(Ref<SurfaceTool> st, size_t vertex_id) const {
		bool uv_0 = false;
		bool uv_1 = false;
		bool normal_found = false;
		bool color_found = false;
		Vector2 uv_0_vec = get_uv_0(vertex_id, uv_0);
		Vector2 uv_1_vec = get_uv_1(vertex_id, uv_1);
		Vector3 normal = get_normal(vertex_id, normal_found);
		Color color = get_colors(vertex_id, color_found);
		if (uv_0) {
			//print_verbose("added uv_0 st " + uv_0_vec);
			st->add_uv(uv_0_vec);
		}
		if (uv_1) {
			//print_verbose("added uv_1 st " + uv_1_vec);
			st->add_uv2(uv_1_vec);
		}

		if (normal_found) {
			st->add_normal(normal);
		}

		if (color_found) {
			st->add_color(color);
		}
	}
};

struct VertexMapping : Reference {
	Vector<float> weights = Vector<float>();
	Vector<Ref<FBXBone> > bones = Vector<Ref<FBXBone> >();

	/*** Will only add a vertex weight if it has been validated that it exists in godot **/
	void GetValidatedBoneWeightInfo(Vector<int> &out_bones, Vector<float> &out_weights);
};

// Caches mesh information and instantiates meshes for you using helper functions.
struct FBXMeshData : Reference {
	// vertex id, Weight Info
	// later: perf we can use array here
	Map<size_t, Ref<VertexMapping> > vertex_weights;

	// translate fbx mesh data from document context to FBX Mesh Geometry Context
	bool valid_weight_indexes = false;

	MeshInstance *create_fbx_mesh(const Assimp::FBX::MeshGeometry *mesh_geometry, const Assimp::FBX::Model *model);

	void GenFBXWeightInfo(const Assimp::FBX::MeshGeometry *mesh_geometry, Ref<SurfaceTool> st, size_t vertex_id);

	// basically this gives the correct ID for the vertex specified. so the weight data is correct for the meshes, as they're de-indexed.
	void FixWeightData(const Assimp::FBX::MeshGeometry *mesh_geometry);

	// verticies could go here
	// uvs could go here
	// normals could go here

	/* mesh maximum weight count */
	bool valid_weight_count = false;
	int max_weight_count = 0;
	uint64_t mesh_id = 0; // fbx mesh id
	uint64_t armature_id = 0;
	bool valid_armature_id = false;
	MeshInstance *godot_mesh_instance = nullptr;
};

#endif // EDITOR_SCENE_FBX_MESH_DATA_H