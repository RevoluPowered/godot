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

#include "scene/resources/mesh.h"
#include "scene/resources/surface_tool.h"
#include <algorithm>

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
	Ref<SpatialMaterial> material;
	HashMap<PolygonId, Vector<DataIndex> > surface_polygon_vertex;
};

MeshInstance *FBXMeshData::create_fbx_mesh(const ImportState &state, const Assimp::FBX::MeshGeometry *mesh_geometry, const Assimp::FBX::Model *model) {

	const int vertex_count = mesh_geometry->get_vertices().size();

	// todo: make this just use a uint64_t FBX ID this is a copy of our original materials unfortunately.
	const std::vector<const Assimp::FBX::Material *> &material_lookup = model->GetMaterials();

	// Phase 1. Parse all FBX data.
	HashMap<int, Vector3> normals = extract_per_vertex_data(
			vertex_count,
			mesh_geometry->get_edge_map(),
			mesh_geometry->get_polygon_indices(),
			mesh_geometry->get_normals(),
			CombinationMode::Avg, // TODO How can we make this dynamic?
			&validate_vector_2or3,
			Vector3(1, 0, 0));

	HashMap<int, Vector2> uvs_0 = extract_per_vertex_data(
			vertex_count,
			mesh_geometry->get_edge_map(),
			mesh_geometry->get_polygon_indices(),
			mesh_geometry->get_uv_0(),
			CombinationMode::TakeFirst,
			&validate_vector_2or3,
			Vector2());

	HashMap<int, Vector2> uvs_1 = extract_per_vertex_data(
			vertex_count,
			mesh_geometry->get_edge_map(),
			mesh_geometry->get_polygon_indices(),
			mesh_geometry->get_uv_1(),
			CombinationMode::TakeFirst,
			&validate_vector_2or3,
			Vector2());

	HashMap<int, Color> colors = extract_per_vertex_data(
			vertex_count,
			mesh_geometry->get_edge_map(),
			mesh_geometry->get_polygon_indices(),
			mesh_geometry->get_colors(),
			CombinationMode::TakeFirst,
			&no_validation,
			Color());

	// TODO what about tangends?
	// TODO what about binormals?
	// TODO there is other?

	HashMap<int, SurfaceId> polygon_surfaces = extract_per_polygon(
			vertex_count,
			mesh_geometry->get_polygon_indices(),
			mesh_geometry->get_material_allocation_id(),
			CombinationMode::TakeFirst,
			&no_validation,
			-1);

	// The map key is the material allocator id that is also used as surface id.
	HashMap<SurfaceId, SurfaceData> surfaces;

	// Phase 2. For each material create a surface tool (So a different mesh).
	{
		if (polygon_surfaces.empty()) {
			// No material, just use the default one with index -1.
			// Set -1 to all polygons.
			const int polygon_count = count_polygons(mesh_geometry->get_polygon_indices());
			for (int p = 0; p < polygon_count; p += 1) {
				polygon_surfaces[p] = -1;
			}
		}

		// Create the surface now.
		for (const int *polygon_id = polygon_surfaces.next(nullptr); polygon_id != nullptr; polygon_id = polygon_surfaces.next(polygon_id)) {
			const int surface_id = polygon_surfaces[*polygon_id];
			if (surfaces.has(surface_id) == false) {
				SurfaceData sd;
				sd.surface_tool.instance();
				sd.surface_tool->begin(Mesh::PRIMITIVE_TRIANGLES);

				if (surface_id < 0) {
					// nothing to do
				} else if (surface_id < material_lookup.size()) {
					const Assimp::FBX::Material *mat_mapping = material_lookup.at(surface_id);
					const uint64_t mapping_id = mat_mapping->ID();
					if (state.cached_materials.has(mapping_id)) {
						sd.material = state.cached_materials[mapping_id];
					}
				} else {
					WARN_PRINT("out of bounds surface detected, FBX file has corrupt material data");
				}

				surfaces.set(surface_id, sd);
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
				ERR_FAIL_COND_V_MSG(polygon_surfaces.has(polygon_index) == false, nullptr, "The FBX file is currupted, This surface_index is not expected.");
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

			// This must be done before add_vertex because the surface tool is
			// expecting this before the st->add_vertex() call
			// todo: please use your own API inside this to retrieve vertex mapping data

			GenFBXWeightInfo(mesh_geometry, surface->surface_tool, vertex);
			// todo: please fix the uv coordinates
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
	int no_name_count = 0;
	Set<String> morphs_names;
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
					String name = ImportUtils::FBXAnimMeshName(shape_geometry->Name()).c_str();
					if (name.empty()) {
						name = "morph_" + itos(no_name_count);
						no_name_count += 1;
					}
					morphs_names.insert(name);
				}
			}
		}
	}

	// Phase 6. Compose the mesh and return it.
	Ref<ArrayMesh> mesh;
	mesh.instance();

	for (Set<String>::Element *e = morphs_names.front(); e; e = e->next()) {
		mesh->add_blend_shape(e->get());
	}

	for (const SurfaceId *surface_id = surfaces.next(nullptr); surface_id != nullptr; surface_id = surfaces.next(surface_id)) {
		SurfaceData *surface = surfaces.getptr(*surface_id);
		Array material_morphs;
		// Add the morphs for this material into the mesh.
		if (morphs.has(*surface_id)) {
			material_morphs = *morphs.getptr(*surface_id);
		}

		mesh->add_surface_from_arrays(
				Mesh::PRIMITIVE_TRIANGLES,
				surface->surface_tool->commit_to_arrays(),
				material_morphs);

		if (surface->material.is_valid()) {
			mesh->surface_set_name(*surface_id, surface->material->get_name());
			mesh->surface_set_material(*surface_id, surface->material);
		}

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
		const HashMap<int, Vector3> &p_normals,
		const HashMap<int, Vector2> &p_uvs_0,
		const HashMap<int, Vector2> &p_uvs_1,
		const HashMap<int, Color> &p_colors,
		const Vector3 &p_morph_value,
		const Vector3 &p_morph_normal) {

	ERR_FAIL_INDEX_MSG(p_vertex, (Vertex)p_vertices_position.size(), "FBX file is corrupted, the position of the vertex can't be retrieved.");

	if (p_normals.has(p_vertex)) {
		p_surface_tool->add_normal(p_normals[p_vertex] + p_morph_normal);
	}

	if (p_uvs_0.has(p_vertex)) {
		p_surface_tool->add_uv(p_uvs_0[p_vertex]);
	}

	if (p_uvs_1.has(p_vertex)) {
		p_surface_tool->add_uv2(p_uvs_1[p_vertex]);
	}

	if (p_colors.has(p_vertex)) {
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
	if (vertex_weights.empty()) {
		return;
	}

	ERR_FAIL_COND_MSG(!vertex_weights.has(vertex_id), "unable to resolve vertex supplied to weight information");

	Ref<VertexMapping> VertexWeights = vertex_weights[vertex_id];
	int weight_size = VertexWeights->weights.size();

	if (weight_size > 0) {
		// Weight normalisation to make bone weights in correct ordering
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
		print_verbose("[doc] triangle added weights to mesh for bones");
	}
}

const int FBXMeshData::get_vertex_from_polygon_vertex(const std::vector<int> &p_polygon_indices, int p_index) const {
	if (p_index < 0 || p_index >= (int)p_polygon_indices.size()) {
		return -1;
	}

	const int vertex = p_polygon_indices[p_index];
	if (vertex >= 0) {
		return vertex;
	} else {
		// Negative numbers are the end of the face, reversing the bits is
		// possible to obtain the positive correct vertex number.
		return ~vertex;
	}
}

const bool FBXMeshData::is_end_of_polygon(const std::vector<int> &p_polygon_indices, int p_index) const {
	if (p_index < 0 || p_index >= (int)p_polygon_indices.size()) {
		return false;
	}

	const int vertex = p_polygon_indices[p_index];

	// If the index is negative this is the end of the Polygon.
	return vertex < 0;
}

const bool FBXMeshData::is_start_of_polygon(const std::vector<int> &p_polygon_indices, int p_index) const {
	if (p_index < 0 || p_index >= (int)p_polygon_indices.size()) {
		return false;
	}

	if (p_index == 0) {
		return true;
	}

	// If the previous indices is negative this is the begin of a new Polygon.
	return p_polygon_indices[p_index - 1] < 0;
}

const int FBXMeshData::count_polygons(const std::vector<int> &p_polygon_indices) const {
	// The negative numbers define the end of the polygon. Counting the amount of
	// negatives the numbers of polygons are obtained.
	int count = 0;
	for (size_t i = 0; i < p_polygon_indices.size(); i += 1) {
		if (p_polygon_indices[i] < 0) {
			count += 1;
		}
	}
	return count;
}

template <class T>
HashMap<int, T> FBXMeshData::extract_per_vertex_data(
		int p_vertex_count,
		const std::vector<Assimp::FBX::MeshGeometry::Edge> &p_edge_map,
		const std::vector<int> &p_polygon_indices,
		const Assimp::FBX::MeshGeometry::MappingData<T> &p_fbx_data,
		CombinationMode p_combination_mode,
		void (*validate_function)(T &r_current, const T &p_fall_back),
		T p_fallback_value) const {

	ERR_FAIL_COND_V_MSG(p_fbx_data.ref_type == Assimp::FBX::MeshGeometry::ReferenceType::index_to_direct && p_fbx_data.index.size() == 0, (HashMap<int, T>()), "The FBX seems corrupted");

	// Aggregate vertex data.
	HashMap<Vertex, Vector<T> > aggregate_vertex_data;

	switch (p_fbx_data.map_type) {
		case Assimp::FBX::MeshGeometry::MapType::none: {
			// No data nothing to do.
			return (HashMap<int, T>());
		}
		case Assimp::FBX::MeshGeometry::MapType::vertex: {
			if (p_fbx_data.ref_type == Assimp::FBX::MeshGeometry::ReferenceType::direct) {
				// The data is mapped per vertex directly.
				ERR_FAIL_COND_V_MSG((int)p_fbx_data.data.size() != p_vertex_count, (HashMap<int, T>()), "FBX file corrupted: #ERR01");
				for (size_t vertex_index = 0; vertex_index < p_fbx_data.data.size(); vertex_index += 1) {
					aggregate_vertex_data[vertex_index].push_back(p_fbx_data.data[vertex_index]);
				}
			} else {
				// The data is mapped per vertex using a reference.
				// The indices array, contains a *reference_id for each vertex.
				// * Note that the reference_id is the id of data into the data array.
				//
				// https://help.autodesk.com/view/FBX/2017/ENU/?guid=__cpp_ref_class_fbx_layer_element_html
				ERR_FAIL_COND_V_MSG((int)p_fbx_data.index.size() != p_vertex_count, (HashMap<int, T>()), "FBX file corrupted: #ERR02");
				for (size_t vertex_index = 0; vertex_index < p_fbx_data.index.size(); vertex_index += 1) {
					ERR_FAIL_INDEX_V_MSG(p_fbx_data.index[vertex_index], (int)p_fbx_data.data.size(), (HashMap<int, T>()), "FBX file seems corrupted: #ERR03.")
					aggregate_vertex_data[vertex_index].push_back(p_fbx_data.data[p_fbx_data.index[vertex_index]]);
				}
			}
		} break;
		case Assimp::FBX::MeshGeometry::MapType::polygon_vertex: {
			if (p_fbx_data.ref_type == Assimp::FBX::MeshGeometry::ReferenceType::direct) {
				// The data are mapped per polygon vertex directly.
				ERR_FAIL_COND_V_MSG((int)p_polygon_indices.size() != (int)p_fbx_data.data.size(), (HashMap<int, T>()), "FBX file seems corrupted: #ERR04");
				for (size_t polygon_vertex_index = 0; polygon_vertex_index < p_fbx_data.data.size(); polygon_vertex_index += 1) {
					const int vertex_index = get_vertex_from_polygon_vertex(p_polygon_indices, polygon_vertex_index);
					ERR_FAIL_COND_V_MSG(vertex_index < 0, (HashMap<int, T>()), "FBX file corrupted: #ERR05");
					ERR_FAIL_COND_V_MSG(vertex_index >= p_vertex_count, (HashMap<int, T>()), "FBX file corrupted: #ERR06");

					aggregate_vertex_data[vertex_index].push_back(p_fbx_data.data[polygon_vertex_index]);
				}
			} else {
				// The data is mapped per polygon_vertex using a reference.
				// The indices array, contains a *reference_id for each polygon_vertex.
				// * Note that the reference_id is the id of data into the data array.
				//
				// https://help.autodesk.com/view/FBX/2017/ENU/?guid=__cpp_ref_class_fbx_layer_element_html
				ERR_FAIL_COND_V_MSG(p_polygon_indices.size() != p_fbx_data.index.size(), (HashMap<int, T>()), "FBX file corrupted: #ERR7");
				for (size_t polygon_vertex_index = 0; polygon_vertex_index < p_fbx_data.index.size(); polygon_vertex_index += 1) {
					const int vertex_index = get_vertex_from_polygon_vertex(p_polygon_indices, polygon_vertex_index);
					ERR_FAIL_COND_V_MSG(vertex_index < 0, (HashMap<int, T>()), "FBX file corrupted: #ERR8");
					ERR_FAIL_COND_V_MSG(vertex_index >= p_vertex_count, (HashMap<int, T>()), "FBX file seems corrupted: #ERR9.")
					ERR_FAIL_COND_V_MSG(p_fbx_data.index[polygon_vertex_index] < 0, (HashMap<int, T>()), "FBX file seems corrupted: #ERR10.")
					ERR_FAIL_COND_V_MSG(p_fbx_data.index[polygon_vertex_index] >= (int)p_fbx_data.data.size(), (HashMap<int, T>()), "FBX file seems corrupted: #ERR11.")
					aggregate_vertex_data[vertex_index].push_back(p_fbx_data.data[p_fbx_data.index[polygon_vertex_index]]);
				}
			}
		} break;
		case Assimp::FBX::MeshGeometry::MapType::polygon: {
			if (p_fbx_data.ref_type == Assimp::FBX::MeshGeometry::ReferenceType::direct) {
				// The data are mapped per polygon directly.
				const int polygon_count = count_polygons(p_polygon_indices);
				ERR_FAIL_COND_V_MSG(polygon_count != (int)p_fbx_data.data.size(), (HashMap<int, T>()), "FBX file seems corrupted: #ERR12");

				// Advance each polygon vertex, each new polygon advance the polygon index.
				int polygon_index = -1;
				for (size_t polygon_vertex_index = 0;
						polygon_vertex_index < p_polygon_indices.size();
						polygon_vertex_index += 1) {

					if (is_start_of_polygon(p_polygon_indices, polygon_vertex_index)) {
						polygon_index += 1;
						ERR_FAIL_INDEX_V_MSG(polygon_index, (int)p_fbx_data.data.size(), (HashMap<int, T>()), "FBX file seems corrupted: #ERR13");
					}

					const int vertex_index = get_vertex_from_polygon_vertex(p_polygon_indices, polygon_vertex_index);
					ERR_FAIL_INDEX_V_MSG(vertex_index, p_vertex_count, (HashMap<int, T>()), "FBX file corrupted: #ERR14");

					aggregate_vertex_data[vertex_index].push_back(p_fbx_data.data[polygon_index]);
				}
				ERR_FAIL_COND_V_MSG((polygon_index + 1) != polygon_count, (HashMap<int, T>()), "FBX file seems corrupted: #ERR16. Not all Polygons are present in the file.")
			} else {
				// The data is mapped per polygon using a reference.
				// The indices array, contains a *reference_id for each polygon.
				// * Note that the reference_id is the id of data into the data array.
				//
				// https://help.autodesk.com/view/FBX/2017/ENU/?guid=__cpp_ref_class_fbx_layer_element_html
				const int polygon_count = count_polygons(p_polygon_indices);
				ERR_FAIL_COND_V_MSG(polygon_count != (int)p_fbx_data.index.size(), (HashMap<int, T>()), "FBX file seems corrupted: #ERR17");

				// Advance each polygon vertex, each new polygon advance the polygon index.
				int polygon_index = -1;
				for (size_t polygon_vertex_index = 0;
						polygon_vertex_index < p_polygon_indices.size();
						polygon_vertex_index += 1) {

					if (is_start_of_polygon(p_polygon_indices, polygon_vertex_index)) {
						polygon_index += 1;
						ERR_FAIL_INDEX_V_MSG(polygon_index, (int)p_fbx_data.index.size(), (HashMap<int, T>()), "FBX file seems corrupted: #ERR18");
						ERR_FAIL_INDEX_V_MSG(p_fbx_data.index[polygon_index], (int)p_fbx_data.data.size(), (HashMap<int, T>()), "FBX file seems corrupted: #ERR19");
					}

					const int vertex_index = get_vertex_from_polygon_vertex(p_polygon_indices, polygon_vertex_index);
					ERR_FAIL_INDEX_V_MSG(vertex_index, p_vertex_count, (HashMap<int, T>()), "FBX file corrupted: #ERR20");

					aggregate_vertex_data[vertex_index].push_back(p_fbx_data.data[p_fbx_data.index[polygon_index]]);
				}
				ERR_FAIL_COND_V_MSG((polygon_index + 1) != polygon_count, (HashMap<int, T>()), "FBX file seems corrupted: #ERR22. Not all Polygons are present in the file.")
			}
		} break;
		case Assimp::FBX::MeshGeometry::MapType::edge: {
			if (p_fbx_data.ref_type == Assimp::FBX::MeshGeometry::ReferenceType::direct) {
				// The data are mapped per edge directly.
				ERR_FAIL_COND_V_MSG(p_edge_map.size() != p_fbx_data.data.size(), (HashMap<int, T>()), "FBX file seems corrupted: #ERR23");
				for (size_t edge_index = 0; edge_index < p_fbx_data.data.size(); edge_index += 1) {
					const Assimp::FBX::MeshGeometry::Edge edge = Assimp::FBX::MeshGeometry::get_edge(p_edge_map, edge_index);
					ERR_FAIL_INDEX_V_MSG(edge.vertex_0, p_vertex_count, (HashMap<int, T>()), "FBX file corrupted: #ERR24");
					ERR_FAIL_INDEX_V_MSG(edge.vertex_1, p_vertex_count, (HashMap<int, T>()), "FBX file corrupted: #ERR25");
					ERR_FAIL_INDEX_V_MSG(edge.vertex_0, (int)p_fbx_data.data.size(), (HashMap<int, T>()), "FBX file corrupted: #ERR26");
					ERR_FAIL_INDEX_V_MSG(edge.vertex_1, (int)p_fbx_data.data.size(), (HashMap<int, T>()), "FBX file corrupted: #ERR27");
					aggregate_vertex_data[edge.vertex_0].push_back(p_fbx_data.data[edge_index]);
					aggregate_vertex_data[edge.vertex_1].push_back(p_fbx_data.data[edge_index]);
				}
			} else {
				// The data is mapped per edge using a reference.
				// The indices array, contains a *reference_id for each polygon.
				// * Note that the reference_id is the id of data into the data array.
				//
				// https://help.autodesk.com/view/FBX/2017/ENU/?guid=__cpp_ref_class_fbx_layer_element_html
				ERR_FAIL_COND_V_MSG(p_edge_map.size() != p_fbx_data.index.size(), (HashMap<int, T>()), "FBX file seems corrupted: #ERR28");
				for (size_t edge_index = 0; edge_index < p_fbx_data.data.size(); edge_index += 1) {
					const Assimp::FBX::MeshGeometry::Edge edge = Assimp::FBX::MeshGeometry::get_edge(p_edge_map, edge_index);
					ERR_FAIL_INDEX_V_MSG(edge.vertex_0, p_vertex_count, (HashMap<int, T>()), "FBX file corrupted: #ERR29");
					ERR_FAIL_INDEX_V_MSG(edge.vertex_1, p_vertex_count, (HashMap<int, T>()), "FBX file corrupted: #ERR30");
					ERR_FAIL_INDEX_V_MSG(edge.vertex_0, (int)p_fbx_data.index.size(), (HashMap<int, T>()), "FBX file corrupted: #ERR31");
					ERR_FAIL_INDEX_V_MSG(edge.vertex_1, (int)p_fbx_data.index.size(), (HashMap<int, T>()), "FBX file corrupted: #ERR32");
					ERR_FAIL_INDEX_V_MSG(p_fbx_data.index[edge.vertex_0], (int)p_fbx_data.data.size(), (HashMap<int, T>()), "FBX file corrupted: #ERR33");
					ERR_FAIL_INDEX_V_MSG(p_fbx_data.index[edge.vertex_1], (int)p_fbx_data.data.size(), (HashMap<int, T>()), "FBX file corrupted: #ERR34");
					aggregate_vertex_data[edge.vertex_0].push_back(p_fbx_data.data[p_fbx_data.index[edge_index]]);
					aggregate_vertex_data[edge.vertex_1].push_back(p_fbx_data.data[p_fbx_data.index[edge_index]]);
				}
			}
		} break;
		case Assimp::FBX::MeshGeometry::MapType::all_the_same: {
			// No matter the mode, no matter the data size; The first always win
			// and is set to all the vertices.
			ERR_FAIL_COND_V_MSG(p_fbx_data.data.size() <= 0, (HashMap<int, T>()), "FBX file seems corrupted: #ERR35");
			if (p_fbx_data.data.size() > 0) {
				for (int vertex_index = 0; vertex_index < p_vertex_count; vertex_index += 1) {
					aggregate_vertex_data[vertex_index].push_back(p_fbx_data.data[0]);
				}
			}
		} break;
	}

	if (aggregate_vertex_data.size() == 0) {
		return (HashMap<int, T>());
	}

	// A map is used because turns out that the some FBX file are not well organized
	// with vertices well compacted. Using a map allows avoid those issues.
	HashMap<Vertex, T> vertices;

	// Iterate over the aggregated data to compute the data per vertex.
	if (p_combination_mode == CombinationMode::TakeFirst) {
		// Take the first value for each vertex.
		for (const Vertex *index = aggregate_vertex_data.next(nullptr); index != nullptr; index = aggregate_vertex_data.next(index)) {
			Vector<T> *aggregated_vertex = aggregate_vertex_data.getptr(*index);
			// This can't be null because we are just iterating.
			CRASH_COND(aggregated_vertex == nullptr);

			ERR_FAIL_INDEX_V_MSG(0, aggregated_vertex->size(), (HashMap<int, T>()), "The FBX file is corrupted, No valid data for this vertex index.");
			// Validate the final value.
			T value = (*aggregated_vertex)[0];
			validate_function(value, (*aggregated_vertex)[0]);
			vertices[*index] = value;
		}
	} else {
		// Take the average value for each vertex.
		for (const Vertex *index = aggregate_vertex_data.next(nullptr); index != nullptr; index = aggregate_vertex_data.next(index)) {
			Vector<T> *aggregated_vertex = aggregate_vertex_data.getptr(*index);
			// This can't be null because we are just iterating.
			CRASH_COND(aggregated_vertex == nullptr);

			ERR_FAIL_COND_V_MSG(aggregated_vertex->size() <= 0, (HashMap<int, T>()), "The FBX file is corrupted, No valid data for this vertex index.");

			T combined = (*aggregated_vertex)[0]; // Make sure the data is always correctly initialized.
			for (int i = 1; i < aggregated_vertex->size(); i += 1) {
				combined += (*aggregated_vertex)[i];
			}
			combined = combined / aggregated_vertex->size();
			// Validate the final value.
			validate_function(combined, (*aggregated_vertex)[0]);
			vertices[*index] = combined;
		}
	}

	// Sanitize the data now, if the file is broken we can try import it anyway.
	bool problem_found = false;
	for (size_t i = 0; i < p_polygon_indices.size(); i += 1) {
		const Vertex vertex = get_vertex_from_polygon_vertex(p_polygon_indices, i);
		if (vertices.has(vertex) == false) {
			vertices[vertex] = p_fallback_value;
			problem_found = true;
		}
	}
	if (problem_found) {
		WARN_PRINT("Some data is missing, this FBX file may be corrupted: #WARN0.");
	}

	return vertices;
}

template <class T>
HashMap<int, T> FBXMeshData::extract_per_polygon(
		int p_vertex_count,
		const std::vector<int> &p_polygon_indices,
		const Assimp::FBX::MeshGeometry::MappingData<T> &p_fbx_data,
		CombinationMode p_combination_mode,
		void (*validate_function)(T &r_current, const T &p_fall_back),
		T p_fallback_value) const {

	ERR_FAIL_COND_V_MSG(p_fbx_data.ref_type == Assimp::FBX::MeshGeometry::ReferenceType::index_to_direct && p_fbx_data.index.size() == 0, (HashMap<int, T>()), "The FBX seems corrupted");

	const int polygon_count = count_polygons(p_polygon_indices);

	// Aggregate vertex data.
	HashMap<int, Vector<T> > aggregate_polygon_data;

	switch (p_fbx_data.map_type) {
		case Assimp::FBX::MeshGeometry::MapType::none: {
			// No data nothing to do.
			return (HashMap<int, T>());
		}
		case Assimp::FBX::MeshGeometry::MapType::vertex: {
			ERR_FAIL_V_MSG((HashMap<int, T>()), "This data can't be extracted and organized per polygon, since into the FBX is mapped per vertex. This should not happen.");
		} break;
		case Assimp::FBX::MeshGeometry::MapType::polygon_vertex: {
			ERR_FAIL_V_MSG((HashMap<int, T>()), "This data can't be extracted and organized per polygon, since into the FBX is mapped per polygon vertex. This should not happen.");
		} break;
		case Assimp::FBX::MeshGeometry::MapType::polygon: {
			if (p_fbx_data.ref_type == Assimp::FBX::MeshGeometry::ReferenceType::direct) {
				// The data are mapped per polygon directly.
				ERR_FAIL_COND_V_MSG(polygon_count != (int)p_fbx_data.data.size(), (HashMap<int, T>()), "FBX file is corrupted: #ERR51");

				// Advance each polygon vertex, each new polygon advance the polygon index.
				for (int polygon_index = 0;
						polygon_index < polygon_count;
						polygon_index += 1) {

					ERR_FAIL_INDEX_V_MSG(polygon_index, (int)p_fbx_data.data.size(), (HashMap<int, T>()), "FBX file is corrupted: #ERR52");
					aggregate_polygon_data[polygon_index].push_back(p_fbx_data.data[polygon_index]);
				}
			} else {
				// The data is mapped per polygon using a reference.
				// The indices array, contains a *reference_id for each polygon.
				// * Note that the reference_id is the id of data into the data array.
				//
				// https://help.autodesk.com/view/FBX/2017/ENU/?guid=__cpp_ref_class_fbx_layer_element_html
				ERR_FAIL_COND_V_MSG(polygon_count != (int)p_fbx_data.index.size(), (HashMap<int, T>()), "FBX file seems corrupted: #ERR52");

				// Advance each polygon vertex, each new polygon advance the polygon index.
				for (int polygon_index = 0;
						polygon_index < polygon_count;
						polygon_index += 1) {

					ERR_FAIL_INDEX_V_MSG(polygon_index, (int)p_fbx_data.index.size(), (HashMap<int, T>()), "FBX file is corrupted: #ERR53");
					ERR_FAIL_INDEX_V_MSG(p_fbx_data.index[polygon_index], (int)p_fbx_data.data.size(), (HashMap<int, T>()), "FBX file is corrupted: #ERR54");
					aggregate_polygon_data[polygon_index].push_back(p_fbx_data.data[p_fbx_data.index[polygon_index]]);
				}
			}
		} break;
		case Assimp::FBX::MeshGeometry::MapType::edge: {
			ERR_FAIL_V_MSG((HashMap<int, T>()), "This data can't be extracted and organized per polygon, since into the FBX is mapped per edge. This should not happen.");
		} break;
		case Assimp::FBX::MeshGeometry::MapType::all_the_same: {
			// No matter the mode, no matter the data size; The first always win
			// and is set to all the vertices.
			ERR_FAIL_COND_V_MSG(p_fbx_data.data.size() <= 0, (HashMap<int, T>()), "FBX file seems corrupted: #ERR55");
			if (p_fbx_data.data.size() > 0) {
				for (int polygon_index = 0; polygon_index < polygon_count; polygon_index += 1) {
					aggregate_polygon_data[polygon_index].push_back(p_fbx_data.data[0]);
				}
			}
		} break;
	}

	if (aggregate_polygon_data.size() == 0) {
		return (HashMap<int, T>());
	}

	// A map is used because turns out that the some FBX file are not well organized
	// with vertices well compacted. Using a map allows avoid those issues.
	HashMap<int, T> polygons;

	// Iterate over the aggregated data to compute the data per vertex.
	if (p_combination_mode == CombinationMode::TakeFirst) {
		// Take the first value for each vertex.
		for (const Vertex *index = aggregate_polygon_data.next(nullptr); index != nullptr; index = aggregate_polygon_data.next(index)) {
			Vector<T> *aggregated_polygon = aggregate_polygon_data.getptr(*index);
			// This can't be null because we are just iterating.
			CRASH_COND(aggregated_polygon == nullptr);

			ERR_FAIL_INDEX_V_MSG(0, (int)aggregated_polygon->size(), (HashMap<int, T>()), "The FBX file is corrupted, No valid data for this polygon index.");

			// Validate the final value.
			T value = (*aggregated_polygon)[0];
			validate_function(value, (*aggregated_polygon)[0]);
			polygons[*index] = value;
		}
	} else {
		// Take the average value for each vertex.
		for (const Vertex *index = aggregate_polygon_data.next(nullptr); index != nullptr; index = aggregate_polygon_data.next(index)) {
			Vector<T> *aggregated_polygon = aggregate_polygon_data.getptr(*index);
			// This can't be null because we are just iterating.
			CRASH_COND(aggregated_polygon == nullptr);

			ERR_FAIL_INDEX_V_MSG(0, aggregated_polygon->size(), (HashMap<int, T>()), "The FBX file is corrupted, No valid data for this polygon index.");

			T combined = (*aggregated_polygon)[0]; // Make sure the data is initialized correctly
			for (int i = 1; i < aggregated_polygon->size(); i += 1) {
				combined += (*aggregated_polygon)[i];
			}
			combined = combined / aggregated_polygon->size();
			// Validate the final value.
			validate_function(combined, (*aggregated_polygon)[0]);
			polygons[*index] = combined;
		}
	}

	// Sanitize the data now, if the file is broken we can try import it anyway.
	bool problem_found = false;
	for (int polygon_i = 0; polygon_i < polygon_count; polygon_i += 1) {
		if (polygons.has(polygon_i) == false) {
			polygons[polygon_i] = p_fallback_value;
			problem_found = true;
		}
	}
	if (problem_found) {
		WARN_PRINT("Some data is missing, this FBX file may be corrupted: #WARN1.");
	}

	return polygons;
}