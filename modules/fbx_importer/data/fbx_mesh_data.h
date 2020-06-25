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

#include "fbx_bone.h"
#include "modules/fbx_importer/tools/import_utils.h"
#include "thirdparty/assimp/code/FBX/FBXMeshGeometry.h"

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

	void GenerateIndices(Ref<SurfaceTool> st, uint32_t mesh_face_count) const {
		// todo: can we remove the split by primitive type so it's only material
		// todo: implement the fbx poly mapping thing here
		// todo: convert indices to the godot format
		switch (mesh_face_count) {
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

					// todo: unfuck this
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
	/// The FBX files usually have more data per single vertex (usually this
	/// happens with the normals, that to generate the smooth groups the FBX
	/// contains the vertex normals for each face).
	/// With this enum is possible to control what to do, combine those or
	/// take the average.
	enum class CombinationMode {
		TakeFirst,
		Avg
	};

	// vertex id, Weight Info
	// later: perf we can use array here
	Map<size_t, Ref<VertexMapping> > vertex_weights;

	// translate fbx mesh data from document context to FBX Mesh Geometry Context
	bool valid_weight_indexes = false;

	MeshInstance *create_fbx_mesh(const Assimp::FBX::MeshGeometry *mesh_geometry, const Assimp::FBX::Model *model);

	void GenFBXWeightInfo(const Assimp::FBX::MeshGeometry *mesh_geometry, Ref<SurfaceTool> st, size_t vertex_id);

	/* mesh maximum weight count */
	bool valid_weight_count = false;
	int max_weight_count = 0;
	uint64_t mesh_id = 0; // fbx mesh id
	uint64_t armature_id = 0;
	bool valid_armature_id = false;
	MeshInstance *godot_mesh_instance = nullptr;

private:
	void add_vertex(
			Ref<SurfaceTool> p_surface_tool,
			int p_vertex,
			const std::vector<Vector3> &p_vertices_position,
			const Vector<Vector3> &p_normals,
			const Vector<Vector2> &p_uvs_0,
			const Vector<Vector2> &p_uvs_1,
			const Vector<Color> &p_colors,
			const Vector3 &p_morph_value = Vector3(),
			const Vector3 &p_morph_normal = Vector3());

	void triangulate_polygon(Ref<SurfaceTool> st, Vector<int> p_polygon_vertex) const;

	/// This function is responsible to convert the FBX polygon vertex to
	/// vertex index.
	/// The polygon vertices are stored in an array with some negative
	/// values. The negative values define the last face index.
	/// For example the following `face_array` contains two faces, the former
	/// with 3 vertices and the latter with a line:
	/// [0,2,-2,3,-5]
	/// Parsed as:
	/// [0, 2, 1, 3, 4]
	/// The negative values are computed using this formula: `(-value) - 1`
	///
	/// Returns the vertex index from the poligon vertex.
	/// Returns -1 if `p_index` is invalid.
	const int get_vertex_from_polygon_vertex(const std::vector<int> &p_face_indices, int p_index) const;

	/// Retuns true if this polygon_vertex_index is the end of a new polygon.
	const bool is_end_of_polygon(const std::vector<int> &p_face_indices, int p_index) const;

	/// Retuns true if this polygon_vertex_index is the begin of a new polygon.
	const bool is_start_of_polygon(const std::vector<int> &p_face_indices, int p_index) const;

	/// Returns the number of polygons.
	const int count_polygons(const std::vector<int> &p_face_indices) const;

	/// Used to extract data from the `MappingData` alligned with vertex.
	/// Useful to extract normal/uvs/colors/tangets/etc...
	/// If the function fails somehow, it returns an hollow vector and print an error.
	template <class T>
	Vector<T> extract_per_vertex_data(
			int p_vertex_count,
			const std::vector<Assimp::FBX::MeshGeometry::Edge> &p_edges,
			const std::vector<int> &p_face_indices,
			const Assimp::FBX::MeshGeometry::MappingData<T> &p_fbx_data,
			CombinationMode p_combination_mode,
			void (*validate_function)(T &r_current, const T &p_fall_back)) const;

	/// Used to extract data from the `MappingData` organized per polygon.
	/// Useful to extract the materila
	/// If the function fails somehow, it returns an hollow vector and print an error.
	template <class T>
	Vector<T> extract_per_polygon(
			int p_vertex_count,
			const std::vector<int> &p_face_indices,
			const Assimp::FBX::MeshGeometry::MappingData<T> &p_fbx_data,
			CombinationMode p_combination_mode,
			void (*validate_function)(T &r_current, const T &p_fall_back)) const;
};

#endif // EDITOR_SCENE_FBX_MESH_DATA_H