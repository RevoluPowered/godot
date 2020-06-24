/*************************************************************************/
/*  fbx_mesh_data.cpp			                                         */
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

#include "fbx_mesh_data.h"
#include "core/io/image_loader.h"
#include "core/oa_hash_map.h"
#include "data/fbx_anim_container.h"
#include "data/fbx_skeleton.h"
#include "editor/editor_log.h"
#include "editor/editor_node.h"
#include "editor/import/resource_importer_scene.h"
#include "editor_scene_importer_fbx.h"
#include "scene/3d/bone_attachment.h"
#include "scene/3d/camera.h"
#include "scene/3d/light.h"
#include "scene/3d/mesh_instance.h"
#include "scene/main/node.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"
#include "scene/resources/surface_tool.h"
#include "tools/import_utils.h"

#include <code/FBX/FBXDocument.h>
#include <code/FBX/FBXImportSettings.h>
#include <code/FBX/FBXParser.h>
#include <code/FBX/FBXProperties.h>
#include <code/FBX/FBXTokenizer.h>
#include <thirdparty/assimp/code/FBX/FBXMeshGeometry.h>
#include <assimp/Importer.hpp>
#include <string>

void VertexMapping::GetValidatedBoneWeightInfo(Vector<int> &out_bones, Vector<float> &out_weights) {
	ERR_FAIL_COND_MSG(bones.size() != weights.size(), "[doc] error unable to handle incorrect bone weight info");
	ERR_FAIL_COND_MSG(out_bones.size() > 0 && out_weights.size() > 0, "[doc] error input must be empty before using this function, accidental re-use?");
	for (int idx = 0; idx < weights.size(); idx++) {
		Ref<FBXBone> bone = bones[idx];
		float weight = weights[idx];
		if (bone.is_valid()) {
			out_bones.push_back(bone->godot_bone_id);
			out_weights.push_back(weight);
			//print_verbose("[" + itos(idx) + "] valid bone weight: " + itos(bone->godot_bone_id) + " weight: " + rtos(weight));
		} else {
			out_bones.push_back(0);
			out_weights.push_back(0);
			if (bone.is_valid()) {
				ERR_PRINT("skeleton misconfigured");
			} else {
				//print_verbose("[" + itos(idx) + "] fake weight: 0");
			}
		}
	}
}

template <class T>
void validate_vector_2or3(T &r_value, const T &p_fall_back) {
	if (r_value.length_squared() <= CMP_EPSILON) {
		r_value = p_fall_back;
	}
	r_value.normalize();
}

template <class T>
void no_validation(T &r_value, const T &p_fall_back) {
}

typedef int Vertex;
typedef int SurfaceId;
typedef int PolygonId;
typedef int DataIndex;

struct SurfaceData {
	Ref<SurfaceTool> surface_tool;
	// Contains vertices, calling this data so it's the same name used in the FBX format.
	Vector<Vertex> data;
	HashMap<PolygonId, Vector<DataIndex> > surface_polygon_vertex;
};

MeshInstance *FBXMeshData::create_fbx_mesh(const Assimp::FBX::MeshGeometry *mesh_geometry, const Assimp::FBX::Model *model) {

	const int vertex_count = mesh_geometry->get_vertices().size();

	// Phase 1. Parse all FBX data.
	Vector<Vector3> normals = extract_per_vertex_data(
			vertex_count,
			mesh_geometry->get_polygon_indices(),
			mesh_geometry->get_normals(),
			CombinationMode::Avg, // TODO How can we make this dynamic?
			&validate_vector_2or3);

	Vector<Vector2> uvs_0 = extract_per_vertex_data(
			vertex_count,
			mesh_geometry->get_polygon_indices(),
			mesh_geometry->get_uv_0(),
			CombinationMode::TakeFirst,
			&validate_vector_2or3);

	Vector<Vector2> uvs_1 = extract_per_vertex_data(
			vertex_count,
			mesh_geometry->get_polygon_indices(),
			mesh_geometry->get_uv_1(),
			CombinationMode::TakeFirst,
			&validate_vector_2or3);

	Vector<Color> colors = extract_per_vertex_data(
			vertex_count,
			mesh_geometry->get_polygon_indices(),
			mesh_geometry->get_colors(),
			CombinationMode::TakeFirst,
			&no_validation);

	// TODO what about tangends?
	// TODO what about binormals?
	// TODO there is other?

	// The extract function is already doint the data check the data; so the
	// `CRASH_COND` cannot never happen.
	CRASH_COND(normals.size() != 0 && normals.size() != (int)mesh_geometry->get_vertices().size());
	CRASH_COND(uvs_0.size() != 0 && uvs_0.size() != (int)mesh_geometry->get_vertices().size());
	CRASH_COND(uvs_1.size() != 0 && uvs_1.size() != (int)mesh_geometry->get_vertices().size());
	CRASH_COND(colors.size() != 0 && colors.size() != (int)mesh_geometry->get_vertices().size());

	Vector<SurfaceId> polygon_surfaces = extract_per_polygon(
			vertex_count,
			mesh_geometry->get_polygon_indices(),
			mesh_geometry->get_material_allocation_id(),
			CombinationMode::TakeFirst,
			&no_validation);

	// The map key is the material allocator id that is also used as surface id.
	HashMap<SurfaceId, SurfaceData> surfaces;

	// Phase 2. For each material create a surface tool (So a different mesh).
	{
		if (polygon_surfaces.size() == 0) {
			// No material, just use the default one with index -1.
			SurfaceData sd;
			sd.surface_tool.instance();
			sd.surface_tool->begin(Mesh::PRIMITIVE_TRIANGLES);
			surfaces.set(-1, sd);

			// Set -1 to all polygons.
			const int polygon_count = count_polygons(mesh_geometry->get_polygon_indices());
			polygon_surfaces.resize(polygon_count);
			for (int p = 0; p < polygon_count; p += 1) {
				polygon_surfaces.write[p] = -1;
			}
		} else {
			const Assimp::FBX::MeshGeometry::MappingData<int> &material_data = mesh_geometry->get_material_allocation_id();
			for (size_t i = 0; i < material_data.data.size(); i += 1) {
				SurfaceData sd;
				sd.surface_tool.instance();
				sd.surface_tool->begin(Mesh::PRIMITIVE_TRIANGLES);
				surfaces.set(material_data.data[i], sd);
			}
		}
	}

	// Phase 3. Map the vertices relative to each surface, in this way we can
	// just insert the vertices that we need per each surface.

	{
		PolygonId polygon_index = -1;
		SurfaceId surface_id = -1;
		SurfaceData *surface_data;

		for (size_t polygon_vertex = 0; polygon_vertex < mesh_geometry->get_polygon_indices().size(); polygon_vertex += 1) {
			if (is_start_of_polygon(mesh_geometry->get_polygon_indices(), polygon_vertex)) {
				polygon_index += 1;
				ERR_FAIL_COND_V_MSG(polygon_surfaces.size() <= polygon_index, nullptr, "The FBX file is currupted, This surface_index is not expected.");
				surface_id = polygon_surfaces[polygon_index];
				surface_data = surfaces.getptr(surface_id);
				CRASH_COND(surface_data == nullptr); // Can't be null.
			}

			const int vertex = get_vertex_from_polygon_vertex(mesh_geometry->get_polygon_indices(), polygon_vertex);

			// The vertex position in the surface
			int surface_polygon_vertex_index = surface_data->data.find(vertex);
			if (surface_polygon_vertex_index < 0) {
				// This is a new vertex, store it.
				surface_polygon_vertex_index = surface_data->data.size();
				surface_data->data.push_back(vertex);
			}

			surface_data->surface_polygon_vertex[polygon_index].push_back(surface_polygon_vertex_index);
		}
	}

	// Phase 4. Per each surface just insert the vertices and add the indices.
	for (const SurfaceId *surface_id = surfaces.next(nullptr); surface_id != nullptr; surface_id = surfaces.next(surface_id)) {
		SurfaceData *surface = surfaces.getptr(*surface_id);

		// Just add the vertices data.
		for (int i = 0; i < surface->data.size(); i += 1) {
			const Vertex vertex = surface->data[i];

			add_vertex(
					surface->surface_tool,
					vertex,
					mesh_geometry->get_vertices(),
					normals,
					uvs_0,
					uvs_1,
					colors);
		}

		// Triangulate the various polygons and add the indices.
		for (const PolygonId *polygon_id = surface->surface_polygon_vertex.next(nullptr); polygon_id != nullptr; polygon_id = surface->surface_polygon_vertex.next(polygon_id)) {
			const Vector<DataIndex> *polygon_indices = surface->surface_polygon_vertex.getptr(*polygon_id);

			triangulate_polygon(surface->surface_tool, *polygon_indices);
		}
	}

	// Phase 5. Compose the morphs if any.
	// The morphs are organized also per material.
	struct MorphsInfo {
		String name;
	};
	HashMap<int, Vector<MorphsInfo> > morphs_info;
	HashMap<int, Array> morphs;

	for (const Assimp::FBX::BlendShape *blend_shape : mesh_geometry->get_blend_shapes()) {
		for (const Assimp::FBX::BlendShapeChannel *blend_shape_channel : blend_shape->BlendShapeChannels()) {
			const std::vector<const Assimp::FBX::ShapeGeometry *> &shape_geometries = blend_shape_channel->GetShapeGeometries();
			for (const Assimp::FBX::ShapeGeometry *shape_geometry : shape_geometries) {

				// TODO we have only these??
				const std::vector<unsigned int> &morphs_vertex_indices = shape_geometry->GetIndices();
				const std::vector<Vector3> &morphs_vertices = shape_geometry->GetVertices();
				const std::vector<Vector3> &morphs_normals = shape_geometry->GetNormals();

				ERR_FAIL_COND_V_MSG((int)morphs_vertex_indices.size() > vertex_count, nullptr, "The FBX file is corrupted: #ERR103");
				ERR_FAIL_COND_V_MSG(morphs_vertex_indices.size() != morphs_vertices.size(), nullptr, "The FBX file is corrupted: #ERR104");
				ERR_FAIL_COND_V_MSG((int)morphs_vertices.size() > vertex_count, nullptr, "The FBX file is corrupted: #ERR105");
				ERR_FAIL_COND_V_MSG(morphs_normals.size() != 0 && morphs_normals.size() != morphs_vertices.size(), nullptr, "The FBX file is corrupted: #ERR106");

				for (const SurfaceId *surface_id = surfaces.next(nullptr); surface_id != nullptr; surface_id = surfaces.next(surface_id)) {
					const SurfaceData *surface = surfaces.getptr(*surface_id);

					Ref<SurfaceTool> morphs_st;
					morphs_st.instance();
					morphs_st->begin(Mesh::PRIMITIVE_TRIANGLES);

					// Just add the vertices data.
					for (int vi = 0; vi < surface->data.size(); vi += 1) {
						const Vertex vertex = surface->data[vi];

						// See if there is a morph change for this vertex.
						Vector3 morphs_vertex;
						Vector3 morphs_normal;
						for (size_t i = 0; i < morphs_vertex_indices.size(); i += 1) {
							if ((Vertex)morphs_vertex_indices[i] == vertex) {
								ERR_FAIL_COND_V_MSG(i >= morphs_vertices.size(), nullptr, "The FBX file is corrupted, there is a morph for this vertex but without value.");
								morphs_vertex = morphs_vertices[i];
								if (i < morphs_normals.size()) {
									morphs_normal = morphs_normals[i];
								}
								break;
							}
						}
						add_vertex(
								morphs_st,
								vertex,
								mesh_geometry->get_vertices(),
								normals,
								uvs_0,
								uvs_1,
								colors,
								morphs_vertex,
								morphs_normal);
					}

					morphs[*surface_id].push_back(morphs_st->commit_to_arrays());
					MorphsInfo info;
					info.name = ImportUtils::FBXAnimMeshName(shape_geometry->Name()).c_str();
					morphs_info[*surface_id].push_back(info);
				}
			}
		}
	}

	// Phase 6. Compose the mesh and return it.
	Ref<ArrayMesh> mesh;
	mesh.instance();

	for (const SurfaceId *surface_id = surfaces.next(nullptr); surface_id != nullptr; surface_id = surfaces.next(surface_id)) {
		SurfaceData *surface = surfaces.getptr(*surface_id);
		Array material_morphs;
		// Add the morphs for this material into the mesh.
		if (morphs.has(*surface_id)) {
			material_morphs = *morphs.getptr(*surface_id);
			Vector<MorphsInfo> *infos = morphs_info.getptr(*surface_id);
			for (int i = 0; i < infos->size(); i += 1) {
				if ((*infos)[i].name.empty()) {
					// Note: Don't need to make this unique here.
					mesh->add_blend_shape("morphs");
				} else {
					mesh->add_blend_shape((*infos)[i].name);
				}
			}
		}

		mesh->add_surface_from_arrays(
				Mesh::PRIMITIVE_TRIANGLES,
				surface->surface_tool->commit_to_arrays(),
				material_morphs);

		mesh->set_blend_shape_mode(Mesh::BLEND_SHAPE_MODE_NORMALIZED); // TODO always normalized, Why?
	}

	MeshInstance *godot_mesh = memnew(MeshInstance);
	godot_mesh->set_mesh(mesh);

	return godot_mesh;
}

void FBXMeshData::add_vertex(
		Ref<SurfaceTool> p_surface_tool,
		Vertex p_vertex,
		const std::vector<Vector3> &p_vertices_position,
		const Vector<Vector3> &p_normals,
		const Vector<Vector2> &p_uvs_0,
		const Vector<Vector2> &p_uvs_1,
		const Vector<Color> &p_colors,
		const Vector3 &p_morph_value,
		const Vector3 &p_morph_normal) {

	ERR_FAIL_COND_MSG(p_vertex >= (Vertex)p_vertices_position.size(), "FBX file is corrupted, the position of the vertex can't be retrieved.");

	if (p_normals.size() != 0) {
		p_surface_tool->add_normal(p_normals[p_vertex] + p_morph_normal);
	}

	if (p_uvs_0.size() != 0) {
		p_surface_tool->add_uv(p_uvs_0[p_vertex]);
	}

	if (p_uvs_1.size() != 0) {
		p_surface_tool->add_uv2(p_uvs_1[p_vertex]);
	}

	if (p_colors.size() != 0) {
		p_surface_tool->add_color(p_colors[p_vertex]);
	}

	// TODO what about tangends?
	// TODO what about binormals?
	// TODO there is other?

	// The surface tool want the vertex position as last thing.
	p_surface_tool->add_vertex(p_vertices_position[p_vertex] + p_morph_value);
}

void FBXMeshData::triangulate_polygon(Ref<SurfaceTool> st, Vector<int> p_polygon_vertex) const {
	const int polygon_vertex_count = p_polygon_vertex.size();
	switch (polygon_vertex_count) {
		case 1:
			// Point triangulation
			st->add_index(p_polygon_vertex[0]);
			st->add_index(p_polygon_vertex[0]);
			st->add_index(p_polygon_vertex[0]);
			break;
		case 2: // TODO: validate this
			// Line triangulation
			st->add_index(p_polygon_vertex[1]);
			st->add_index(p_polygon_vertex[1]);
			st->add_index(p_polygon_vertex[0]);
			break;
		case 3:
			// Just a triangle.
			st->add_index(p_polygon_vertex[2]);
			st->add_index(p_polygon_vertex[1]);
			st->add_index(p_polygon_vertex[0]);
			break;
		case 4:
			// Quad triangulation.
			// We have 4 points, so we have 2 triangles, and We have CCW.
			// First triangle.
			st->add_index(p_polygon_vertex[2]);
			st->add_index(p_polygon_vertex[1]);
			st->add_index(p_polygon_vertex[0]);

			// Second side of triangle
			st->add_index(p_polygon_vertex[3]);
			st->add_index(p_polygon_vertex[2]);
			st->add_index(p_polygon_vertex[0]);

			// anti clockwise rotation in indices
			// note had to reverse right from left here
			// [0](x) bottom right (-1,-1)
			// [1](x+1) bottom left (1,-1)
			// [2](x+2) top left (1,1)
			// [3](x+3) top right (-1,1)
			break;
		default:
			ERR_FAIL_MSG("This polygon size is not yet supported, Please triangulate you mesh!");
			break;
	}
}

void FBXMeshData::GenFBXWeightInfo(const Assimp::FBX::MeshGeometry *mesh_geometry, Ref<SurfaceTool> st,
		size_t vertex_id) {
	if (vertex_weights.has(vertex_id)) {
		Ref<VertexMapping> VertexWeights = vertex_weights[vertex_id];
		int weight_size = VertexWeights->weights.size();

		if (weight_size > 0) {

			//print_error("initial count: " + itos(weight_size));
			if (VertexWeights->weights.size() < max_weight_count) {
				// missing weight count - how many do we not have?
				int missing_count = max_weight_count - weight_size;
				//print_verbose("adding missing count : " + itos(missing_count));
				for (int empty_weight_id = 0; empty_weight_id < missing_count; empty_weight_id++) {
					VertexWeights->weights.push_back(0); // no weight
					VertexWeights->bones.push_back(Ref<FBXBone>()); // invalid entry on purpose
				}
			}

			Vector<float> valid_weights;
			Vector<int> valid_bone_ids;

			VertexWeights->GetValidatedBoneWeightInfo(valid_bone_ids, valid_weights);

			st->add_weights(valid_weights);
			st->add_bones(valid_bone_ids);

			//print_verbose("[doc] triangle added weights to mesh for bones");
		}
	} else {
		//print_error("no weight data for vertex: " + itos(vertex_id));
	}
}

const int FBXMeshData::get_vertex_from_polygon_vertex(const std::vector<int> &p_face_indices, int p_index) const {
	if (p_index < 0 || p_index >= (int)p_face_indices.size()) {
		return -1;
	}

	const int vertex = p_face_indices[p_index];
	if (vertex >= 0) {
		return vertex;
	} else {
		return (-vertex) - 1;
	}
}

const bool FBXMeshData::is_end_of_polygon(const std::vector<int> &p_face_indices, int p_index) const {
	if (p_index < 0 || p_index >= (int)p_face_indices.size()) {
		return false;
	}

	const int vertex = p_face_indices[p_index];

	// If the index is negative this is the end of the Polygon.
	return vertex < 0;
}

const bool FBXMeshData::is_start_of_polygon(const std::vector<int> &p_face_indices, int p_index) const {
	if (p_index < 0 || p_index >= (int)p_face_indices.size()) {
		return false;
	}

	if (p_index == 0) {
		return true;
	}

	// If the previous indices is negative this is the begin of a new Polygon.
	return p_face_indices[p_index - 1] < 0;
}

const int FBXMeshData::count_polygons(const std::vector<int> &p_face_indices) const {
	// The negative numbers define the end of the polygon. Counting the amount of
	// negatives the numbers of polygons are obtained.
	int count = 0;
	for (size_t i = 0; i < p_face_indices.size(); i += 1) {
		if (p_face_indices[i] < 0) {
			count += 1;
		}
	}
	return count;
}

template <class T>
Vector<T> FBXMeshData::extract_per_vertex_data(
		int p_vertex_count,
		const std::vector<int> &p_face_indices, // TODO consider renaming to Polygon
		const Assimp::FBX::MeshGeometry::MappingData<T> &p_fbx_data,
		CombinationMode p_combination_mode,
		void (*validate_function)(T &r_current, const T &p_fall_back)) const {

	ERR_FAIL_COND_V_MSG(p_fbx_data.ref_type == Assimp::FBX::MeshGeometry::ReferenceType::index_to_direct && p_fbx_data.index.size() == 0, Vector<T>(), "The FBX seems corrupted");

	// Aggregate vertex data.
	HashMap<int, Vector<T> > aggregate_vertex_data;

	// TODO test all branch of this function.
	switch (p_fbx_data.map_type) {
		case Assimp::FBX::MeshGeometry::MapType::none: {
			// No data nothing to do.
			return Vector<T>();
		}
		case Assimp::FBX::MeshGeometry::MapType::vertex: {
			if (p_fbx_data.ref_type == Assimp::FBX::MeshGeometry::ReferenceType::direct) {
				// The data is mapped per vertex directly.
				ERR_FAIL_COND_V_MSG((int)p_fbx_data.data.size() != p_vertex_count, Vector<T>(), "FBX file corrupted: #ERR01");
				for (size_t vertex_index = 0; vertex_index < p_fbx_data.data.size(); vertex_index += 1) {
					aggregate_vertex_data[vertex_index].push_back(p_fbx_data.data[vertex_index]);
				}
			} else {
				// The data is mapped per vertex using a reference.
				// The indices array, contains a *reference_id for each vertex.
				// * Note that the reference_id is the id of data into the data array.
				//
				// https://help.autodesk.com/view/FBX/2017/ENU/?guid=__cpp_ref_class_fbx_layer_element_html
				// TODO NEED VALIDATION
				ERR_FAIL_COND_V_MSG((int)p_fbx_data.index.size() != p_vertex_count, Vector<T>(), "FBX file corrupted: #ERR02");
				for (size_t vertex_index = 0; vertex_index < p_fbx_data.index.size(); vertex_index += 1) {
					ERR_FAIL_COND_V_MSG(p_fbx_data.index[vertex_index] >= (int)p_fbx_data.data.size(), Vector<T>(), "FBX file seems corrupted: #ERR03.")
					aggregate_vertex_data[vertex_index].push_back(p_fbx_data.data[p_fbx_data.index[vertex_index]]);
				}
			}
		} break;
		case Assimp::FBX::MeshGeometry::MapType::polygon_vertex: {
			if (p_fbx_data.ref_type == Assimp::FBX::MeshGeometry::ReferenceType::direct) {
				// The data are mapped per poligon vertex directly.
				// TODO NEED VALIDATION
				ERR_FAIL_COND_V_MSG((int)p_face_indices.size() != (int)p_fbx_data.data.size(), Vector<T>(), "FBX file seems corrupted: #ERR04");
				for (size_t polygon_vertex_index = 0; polygon_vertex_index < p_fbx_data.data.size(); polygon_vertex_index += 1) {
					const int vertex_index = get_vertex_from_polygon_vertex(p_face_indices, polygon_vertex_index);
					ERR_FAIL_COND_V_MSG(vertex_index < 0, Vector<T>(), "FBX file corrupted: #ERR05");
					ERR_FAIL_COND_V_MSG(vertex_index >= p_vertex_count, Vector<T>(), "FBX file corrupted: #ERR06");

					aggregate_vertex_data[vertex_index].push_back(p_fbx_data.data[polygon_vertex_index]);
				}
			} else {
				// The data is mapped per polygon_vertex using a reference.
				// The indices array, contains a *reference_id for each polygon_vertex.
				// * Note that the reference_id is the id of data into the data array.
				//
				// https://help.autodesk.com/view/FBX/2017/ENU/?guid=__cpp_ref_class_fbx_layer_element_html
				// TODO NEED VALIDATION
				ERR_FAIL_COND_V_MSG(p_face_indices.size() != p_fbx_data.index.size(), Vector<T>(), "FBX file corrupted: #ERR7");
				for (size_t polygon_vertex_index = 0; polygon_vertex_index < p_fbx_data.index.size(); polygon_vertex_index += 1) {
					const int vertex_index = get_vertex_from_polygon_vertex(p_face_indices, polygon_vertex_index);
					ERR_FAIL_COND_V_MSG(vertex_index < 0, Vector<T>(), "FBX file corrupted: #ERR8");
					ERR_FAIL_COND_V_MSG(vertex_index >= p_vertex_count, Vector<T>(), "FBX file seems corrupted: #ERR9.")
					ERR_FAIL_COND_V_MSG(p_fbx_data.index[polygon_vertex_index] < 0, Vector<T>(), "FBX file seems corrupted: #ERR10.")
					ERR_FAIL_COND_V_MSG(p_fbx_data.index[polygon_vertex_index] >= (int)p_fbx_data.data.size(), Vector<T>(), "FBX file seems corrupted: #ERR11.")
					aggregate_vertex_data[vertex_index].push_back(p_fbx_data.data[p_fbx_data.index[polygon_vertex_index]]);
				}
			}
		} break;
		case Assimp::FBX::MeshGeometry::MapType::polygon: {
			if (p_fbx_data.ref_type == Assimp::FBX::MeshGeometry::ReferenceType::direct) {
				// The data are mapped per poligon directly.
				// TODO NEED VALIDATION
				const int polygon_count = count_polygons(p_face_indices);
				ERR_FAIL_COND_V_MSG(polygon_count != (int)p_fbx_data.data.size(), Vector<T>(), "FBX file seems corrupted: #ERR12");

				// Advance each polygon vertex, each new polygon advance the polygon index.
				int polygon_index = -1;
				for (size_t polygon_vertex_index = 0;
						polygon_vertex_index < p_face_indices.size();
						polygon_vertex_index += 1) {

					if (is_start_of_polygon(p_face_indices, polygon_vertex_index)) {
						polygon_index += 1;
						ERR_FAIL_COND_V_MSG(polygon_index >= (int)p_fbx_data.data.size(), Vector<T>(), "FBX file seems corrupted: #ERR13");
					}

					const int vertex_index = get_vertex_from_polygon_vertex(p_face_indices, polygon_vertex_index);
					ERR_FAIL_COND_V_MSG(vertex_index < 0, Vector<T>(), "FBX file corrupted: #ERR14");
					ERR_FAIL_COND_V_MSG(vertex_index >= p_vertex_count, Vector<T>(), "FBX file corrupted: #ERR15");

					aggregate_vertex_data[vertex_index].push_back(p_fbx_data.data[polygon_index]);
				}
				ERR_FAIL_COND_V_MSG((polygon_index + 1) != polygon_count, Vector<T>(), "FBX file seems corrupted: #ERR16. Not all Polygons are present in the file.")
			} else {
				// The data is mapped per polygon using a reference.
				// The indices array, contains a *reference_id for each polygon.
				// * Note that the reference_id is the id of data into the data array.
				//
				// https://help.autodesk.com/view/FBX/2017/ENU/?guid=__cpp_ref_class_fbx_layer_element_html
				// TODO NEED VALIDATION
				const int polygon_count = count_polygons(p_face_indices);
				ERR_FAIL_COND_V_MSG(polygon_count != (int)p_fbx_data.index.size(), Vector<T>(), "FBX file seems corrupted: #ERR17");

				// Advance each polygon vertex, each new polygon advance the polygon index.
				int polygon_index = -1;
				for (size_t polygon_vertex_index = 0;
						polygon_vertex_index < p_face_indices.size();
						polygon_vertex_index += 1) {

					if (is_start_of_polygon(p_face_indices, polygon_vertex_index)) {
						polygon_index += 1;
						ERR_FAIL_COND_V_MSG(polygon_index >= (int)p_fbx_data.index.size(), Vector<T>(), "FBX file seems corrupted: #ERR18");
						ERR_FAIL_COND_V_MSG(p_fbx_data.index[polygon_index] >= (int)p_fbx_data.data.size(), Vector<T>(), "FBX file seems corrupted: #ERR19");
					}

					const int vertex_index = get_vertex_from_polygon_vertex(p_face_indices, polygon_vertex_index);
					ERR_FAIL_COND_V_MSG(vertex_index < 0, Vector<T>(), "FBX file corrupted: #ERR20");
					ERR_FAIL_COND_V_MSG(vertex_index >= p_vertex_count, Vector<T>(), "FBX file corrupted: #ERR21");

					aggregate_vertex_data[vertex_index].push_back(p_fbx_data.data[p_fbx_data.index[polygon_index]]);
				}
				ERR_FAIL_COND_V_MSG((polygon_index + 1) != polygon_count, Vector<T>(), "FBX file seems corrupted: #ERR22. Not all Polygons are present in the file.")
			}
		} break;
		case Assimp::FBX::MeshGeometry::MapType::edge: {
			// TODO
			CRASH_NOW_MSG("Understand how edges are stored, so support it! This is simple to do, just don't have time now!!!!!");
		} break;
		case Assimp::FBX::MeshGeometry::MapType::all_the_same: {
			// No matter the mode, no matter the data size; The first always win
			// and is set to all the vertices.
			ERR_FAIL_COND_V_MSG(p_fbx_data.data.size() <= 0, Vector<T>(), "FBX file seems corrupted: #ERR23");
			if (p_fbx_data.data.size() > 0) {
				for (int vertex_index = 0; vertex_index < p_vertex_count; vertex_index += 1) {
					aggregate_vertex_data[vertex_index].push_back(p_fbx_data.data[0]);
				}
			}
		} break;
	}

	if (aggregate_vertex_data.size() == 0) {
		return Vector<T>();
	}

	Vector<T> vertices;
	vertices.resize(p_vertex_count);
	T *vertices_ptr = vertices.ptrw();

	// Iterate over the aggregated data to compute the data per vertex.
	if (p_combination_mode == CombinationMode::TakeFirst) {
		// Take the first value for each vertex.
		for (int index = 0; index < p_vertex_count; index += 1) {
			ERR_FAIL_COND_V_MSG(aggregate_vertex_data.has(index) == false, Vector<T>(), "The FBX file is corrupted, The vertex index was not found.");

			const Vector<T> *aggregated_vertex = aggregate_vertex_data.getptr(index);
			CRASH_COND(aggregated_vertex == nullptr); // Can't happen, already checked.

			ERR_FAIL_COND_V_MSG(aggregated_vertex->size() <= 0, Vector<T>(), "The FBX file is corrupted, No valid data for this vertex index.");
			// Validate the final value.
			vertices_ptr[index] = (*aggregated_vertex)[0];
			validate_function(vertices_ptr[index], (*aggregated_vertex)[0]);
		}
	} else {
		// Take the average value for each vertex.
		for (int index = 0; index < vertices.size(); index += 1) {
			ERR_FAIL_COND_V_MSG(aggregate_vertex_data.has(index) == false, Vector<T>(), "The FBX file is corrupted, The vertex index was not found.");

			const Vector<T> *aggregated_vertex = aggregate_vertex_data.getptr(index);
			CRASH_COND(aggregated_vertex == nullptr); // Can't happen, already checked.
			ERR_FAIL_COND_V_MSG(aggregated_vertex->size() <= 0, Vector<T>(), "The FBX file is corrupted, No valid data for this vertex index.");

			T combined = (*aggregated_vertex)[0]; // Make sure the data is always correctly initialized.
			for (int i = 1; i < aggregated_vertex->size(); i += 1) {
				combined += (*aggregated_vertex)[i];
			}
			vertices_ptr[index] = combined / aggregated_vertex->size();
			// Validate the final value.
			validate_function(vertices_ptr[index], (*aggregated_vertex)[0]);
		}
	}

	return vertices;
}

template <class T>
Vector<T> FBXMeshData::extract_per_polygon(
		int p_vertex_count,
		const std::vector<int> &p_face_indices, // TODO consider renaming to Polygon
		const Assimp::FBX::MeshGeometry::MappingData<T> &p_fbx_data,
		CombinationMode p_combination_mode,
		void (*validate_function)(T &r_current, const T &p_fall_back)) const {

	ERR_FAIL_COND_V_MSG(p_fbx_data.ref_type == Assimp::FBX::MeshGeometry::ReferenceType::index_to_direct && p_fbx_data.index.size() == 0, Vector<T>(), "The FBX seems corrupted");

	const int polygon_count = count_polygons(p_face_indices);

	// Aggregate vertex data.
	HashMap<int, Vector<T> > aggregate_polygon_data;

	// TODO test all branch of this function.
	switch (p_fbx_data.map_type) {
		case Assimp::FBX::MeshGeometry::MapType::none: {
			// No data nothing to do.
			return Vector<T>();
		}
		case Assimp::FBX::MeshGeometry::MapType::vertex: {
			ERR_FAIL_V_MSG(Vector<T>(), "This data can't be extracted and organized per polygon, since into the FBX is mapped per vertex. This should not happen.");
		} break;
		case Assimp::FBX::MeshGeometry::MapType::polygon_vertex: {
			ERR_FAIL_V_MSG(Vector<T>(), "This data can't be extracted and organized per polygon, since into the FBX is mapped per polygon vertex. This should not happen.");
		} break;
		case Assimp::FBX::MeshGeometry::MapType::polygon: {
			if (p_fbx_data.ref_type == Assimp::FBX::MeshGeometry::ReferenceType::direct) {
				// The data are mapped per poligon directly.
				// TODO NEED VALIDATION
				ERR_FAIL_COND_V_MSG(polygon_count != (int)p_fbx_data.data.size(), Vector<T>(), "FBX file seems corrupted: #ERR51");

				// Advance each polygon vertex, each new polygon advance the polygon index.
				for (int polygon_index = 0;
						polygon_index < polygon_count;
						polygon_index += 1) {

					aggregate_polygon_data[polygon_index].push_back(p_fbx_data.data[polygon_index]);
				}
			} else {
				// The data is mapped per polygon using a reference.
				// The indices array, contains a *reference_id for each polygon.
				// * Note that the reference_id is the id of data into the data array.
				//
				// https://help.autodesk.com/view/FBX/2017/ENU/?guid=__cpp_ref_class_fbx_layer_element_html
				// TODO NEED VALIDATION
				ERR_FAIL_COND_V_MSG(polygon_count != (int)p_fbx_data.index.size(), Vector<T>(), "FBX file seems corrupted: #ERR52");

				// Advance each polygon vertex, each new polygon advance the polygon index.
				for (int polygon_index = 0;
						polygon_index < polygon_count;
						polygon_index += 1) {

					aggregate_polygon_data[polygon_index].push_back(p_fbx_data.data[p_fbx_data.index[polygon_index]]);
				}
			}
		} break;
		case Assimp::FBX::MeshGeometry::MapType::edge: {
			// TODO Probably support this?
			ERR_FAIL_V_MSG(Vector<T>(), "This data can't be extracted and organized per polygon, since into the FBX is mapped per edge. This should not happen.");
		} break;
		case Assimp::FBX::MeshGeometry::MapType::all_the_same: {
			// No matter the mode, no matter the data size; The first always win
			// and is set to all the vertices.
			ERR_FAIL_COND_V_MSG(p_fbx_data.data.size() <= 0, Vector<T>(), "FBX file seems corrupted: #ERR53");
			if (p_fbx_data.data.size() > 0) {
				for (int polygon_index = 0; polygon_index < polygon_count; polygon_index += 1) {
					aggregate_polygon_data[polygon_index].push_back(p_fbx_data.data[0]);
				}
			}
		} break;
	}

	if (aggregate_polygon_data.size() == 0) {
		return Vector<T>();
	}

	Vector<T> polygons;
	polygons.resize(polygon_count);
	T *polygons_ptr = polygons.ptrw();

	// Iterate over the aggregated data to compute the data per vertex.
	if (p_combination_mode == CombinationMode::TakeFirst) {
		// Take the first value for each vertex.
		for (int index = 0; index < polygon_count; index += 1) {
			ERR_FAIL_COND_V_MSG(aggregate_polygon_data.has(index) == false, Vector<T>(), "The FBX file is corrupted, The polygon index was not found.");
			const Vector<T> *aggregated_polygon = aggregate_polygon_data.getptr(index);

			ERR_FAIL_COND_V_MSG(aggregated_polygon->size() <= 0, Vector<T>(), "The FBX file is corrupted, No valid data for this polygon index.");
			// Validate the final value.
			polygons_ptr[index] = (*aggregated_polygon)[0];
			validate_function(polygons_ptr[index], (*aggregated_polygon)[0]);
		}
	} else {
		// Take the average value for each vertex.
		for (int index = 0; index < polygon_count; index += 1) {
			ERR_FAIL_COND_V_MSG(aggregate_polygon_data.has(index) == false, Vector<T>(), "The FBX file is corrupted, The polygon index was not found.");
			const Vector<T> *aggregated_polygon = aggregate_polygon_data.getptr(index);

			ERR_FAIL_COND_V_MSG(aggregated_polygon->size() <= 0, Vector<T>(), "The FBX file is corrupted, No valid data for this polygon index.");

			T combined = (*aggregated_polygon)[0]; // Make sure the data is initialized correctly
			for (int i = 1; i < aggregated_polygon->size(); i += 1) {
				combined += (*aggregated_polygon)[i];
			}
			polygons_ptr[index] = combined / aggregated_polygon->size();
			// Validate the final value.
			validate_function(polygons_ptr[index], (*aggregated_polygon)[0]);
		}
	}

	return polygons;
}