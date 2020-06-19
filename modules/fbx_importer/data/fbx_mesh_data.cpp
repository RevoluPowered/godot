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

	Vector<int> materials = extract_per_vertex_data(
			vertex_count,
			mesh_geometry->get_polygon_indices(),
			mesh_geometry->get_material_allocation_id(),
			CombinationMode::TakeFirst,
			&no_validation);

	// The extract function is already doint the data check the data; so the
	// `CRASH_COND` cannot never happen.
	CRASH_COND(normals.size() != 0 && normals.size() != (int)mesh_geometry->get_vertices().size());
	CRASH_COND(uvs_0.size() != 0 && uvs_0.size() != (int)mesh_geometry->get_vertices().size());
	CRASH_COND(uvs_1.size() != 0 && uvs_1.size() != (int)mesh_geometry->get_vertices().size());
	CRASH_COND(colors.size() != 0 && colors.size() != (int)mesh_geometry->get_vertices().size());

	/// The map key is the material allocator id.
	OAHashMap<int, Ref<SurfaceTool> > surfaces;

	// Phase 2. For each material create a surface tool (So a different mesh).
	{
		if (materials.size() == 0) {
			Ref<SurfaceTool> material_tool;
			material_tool.instance();
			material_tool->begin(Mesh::PRIMITIVE_TRIANGLES);
			surfaces.set(-1, material_tool);
		} else {
			const Assimp::FBX::MeshGeometry::MappingData<int> &material_data = mesh_geometry->get_material_allocation_id();
			for (size_t i = 0; i < material_data.data.size(); i += 1) {
				Ref<SurfaceTool> material_tool;
				material_tool.instance();
				material_tool->begin(Mesh::PRIMITIVE_TRIANGLES);
				surfaces.set(material_data.data[i], material_tool);
			}
		}
	}

	// Phase 3. Compose the vertices.
	for (size_t vertex = 0; vertex < mesh_geometry->get_vertices().size(); vertex += 1) {
		const int material_id = materials.size() == 0 ? -1 : materials[vertex];
		// This can't happen because, above the surface is created for all the
		// materials.
		CRASH_COND(surfaces.has(material_id) == false);

		Ref<SurfaceTool> surface;
		surfaces.lookup(material_id, surface);

		if (normals.size() != 0) {
			surface->add_normal(normals[vertex]);
		}

		if (uvs_0.size() != 0) {
			surface->add_uv(uvs_0[vertex]);
		}

		if (uvs_1.size() != 0) {
			surface->add_uv2(uvs_1[vertex]);
		}

		if (colors.size() != 0) {
			surface->add_color(colors[vertex]);
		}

		// TODO what about tangends?
		// TODO what about binormals?
		// TODO there is other?

		// The surface tool want the vertex as last.
		surface->add_vertex(mesh_geometry->get_vertices()[vertex]);
	}

	// Phase 4. Triangulate the polygons.
	Vector<int> polygon_vertices;
	for (size_t polygon_vertex_index = 0; polygon_vertex_index < mesh_geometry->get_polygon_indices().size(); polygon_vertex_index += 1) {

		polygon_vertices.push_back(get_vertex_from_polygon_vertex(mesh_geometry->get_polygon_indices(), polygon_vertex_index));

		if (is_end_of_polygon(mesh_geometry->get_polygon_indices(), polygon_vertex_index)) {
			// Validate vertices andtake the `material_id`.
			ERR_FAIL_COND_V_MSG(polygon_vertices.size() <= 0, nullptr, "The FBX file is corrupted: #ERR100");

			int material_id = -1;
			for (int i = 0; i < polygon_vertices.size(); i += 1) {
				ERR_FAIL_COND_V_MSG(polygon_vertices[i] < 0, nullptr, "The FBX file is corrupted: #ERR101");
				ERR_FAIL_COND_V_MSG(polygon_vertices[i] >= vertex_count, nullptr, "The FBX file is corrupted: #ERR102");
				if (materials.size() > 0) {
					material_id = materials[polygon_vertices[i]];
					// TODO Please support the case when the poligon as more than 1 material!!
					ERR_FAIL_COND_V_MSG(materials[polygon_vertices[0]] != material_id, nullptr, "TODO SUPPORT THIS CASE PLEASE");
				}
			}

			// Trinagulate

			// This can't happen because, above the surface is created for all the
			// materials.
			CRASH_COND(surfaces.has(material_id) == false);

			Ref<SurfaceTool> surface;
			surfaces.lookup(material_id, surface);

			triangulate_polygon(surface, polygon_vertices);

			polygon_vertices.clear();
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

				int material_id = -1;

				Ref<SurfaceTool> morphs_st;
				morphs_st.instance();
				morphs_st->begin(Mesh::PRIMITIVE_TRIANGLES);

				for (size_t vertex_index = 0; vertex_index < mesh_geometry->get_vertices().size(); vertex_index += 1) {

					Vector3 morphs_vertex;
					Vector3 morphs_normal;
					// Search this vertex index into morph info, to see if it change.
					for (size_t i = 0; i < morphs_vertex_indices.size(); i += 1) {
						if (morphs_vertex_indices[i] == vertex_index) {

							ERR_FAIL_COND_V_MSG(i >= morphs_vertices.size(), nullptr, "The FBX file is corrupted: #ERR107");
							morphs_vertex = morphs_vertices[i];
							if (i < morphs_normals.size()) {
								morphs_normal = morphs_normals[i];
							}

							// Lockup the material for this morphs.
							if (materials.size() > 0) {
								// TODO Please support the case when the poligon as more than 1 material!!
								ERR_FAIL_COND_V_MSG(material_id != -1 && materials[vertex_index] != material_id, nullptr, "TODO SUPPORT THIS CASE PLEASE!");
								material_id = materials[vertex_index];
							}

							break;
						}
					}

					if (normals.size() != 0) {
						morphs_st->add_normal(normals[vertex_index] + morphs_normal);
					}

					if (uvs_0.size() != 0) {
						morphs_st->add_uv(uvs_0[vertex_index]);
					}

					if (uvs_1.size() != 0) {
						morphs_st->add_uv2(uvs_1[vertex_index]);
					}

					if (colors.size() != 0) {
						morphs_st->add_color(colors[vertex_index]);
					}

					// TODO what about tangends?
					// TODO what about binormals?
					// TODO there is other?

					// Note: This must always happens last (This is how ST works).
					// Note: Never add indices.
					morphs_st->add_vertex(mesh_geometry->get_vertices()[vertex_index] + morphs_vertex);
				}

				ERR_FAIL_COND_V_MSG(materials.size() != 0 && material_id == -1, nullptr, "This FBX file is corrupted: #108");

				morphs[material_id].push_back(morphs_st->commit_to_arrays());
				MorphsInfo info;
				info.name = ImportUtils::FBXAnimMeshName(shape_geometry->Name()).c_str();
				morphs_info[material_id].push_back(info);
			}
		}
	}

	// Phase 6. Compose the mesh and return it.
	Ref<ArrayMesh> mesh;
	mesh.instance();

	for (OAHashMap<int, Ref<SurfaceTool> >::Iterator it = surfaces.iter(); it.valid; it = surfaces.next_iter(it)) {
		Array material_morphs;
		// Add the morphs for this material into the mesh.
		if (morphs.has(*it.key)) {
			material_morphs = *morphs.getptr(*it.key);
			Vector<MorphsInfo> *infos = morphs_info.getptr(*it.key);
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
				(*it.value)->commit_to_arrays(),
				material_morphs);

		mesh->set_blend_shape_mode(Mesh::BLEND_SHAPE_MODE_NORMALIZED); // TODO always normalized, Why?
	}

	MeshInstance *godot_mesh = memnew(MeshInstance);
	godot_mesh->set_mesh(mesh);

	return godot_mesh;
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
			break;
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
		} break;
		case Assimp::FBX::MeshGeometry::MapType::edge: {
			// TODO
			CRASH_NOW_MSG("Understand how edges are stored, so support it! This is simple to do, just don't have time now!!!!!");
		} break;
		case Assimp::FBX::MeshGeometry::MapType::all_the_same: {
			// No matter the mode, no matter the data size; The first always win
			// and is set to all the vertices.
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

			T combined;
			for (int i = 0; i < aggregated_vertex->size(); i += 1) {
				combined += (*aggregated_vertex)[i];
			}
			vertices_ptr[index] = combined / aggregated_vertex->size();
			// Validate the final value.
			validate_function(vertices_ptr[index], (*aggregated_vertex)[0]);
		}
	}

	return vertices;
}