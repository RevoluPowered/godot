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
MeshInstance * FBXMeshVertexData::create_fbx_mesh(const Assimp::FBX::MeshGeometry *mesh_geometry, const Assimp::FBX::Model *model) {

	print_verbose("[doc] FBX creating godot mesh for: " + ImportUtils::FBXNodeToName(model->Name()));

	print_verbose("[doc] mesh has " + itos(max_weight_count) + " bone weights");


	Ref<ArrayMesh> mesh;
	mesh.instance();

	Ref<SurfaceTool> st;
	st.instance();

	const std::vector<int> &material_indices = mesh_geometry->GetMaterialIndices();

	bool no_material_found = material_indices.size() == 0;

	if (no_material_found) {
		print_error("no material is configured for mesh " + ImportUtils::FBXNodeToName(model->Name()));
	}

	std::__1::vector<Vector3> vertices = mesh_geometry->GetVertices();
	ImportUtils::AlignMeshAxes(vertices);
	std::__1::vector<uint32_t> face_vertex_counts = mesh_geometry->GetFaceIndexCounts();

	// godot has two uv coordinate channels
	const std::__1::vector<Vector2> &uv_coordinates_0 = mesh_geometry->GetTextureCoords(0);
	const std::__1::vector<Vector2> &uv_coordinates_1 = mesh_geometry->GetTextureCoords(1);
	const std::__1::vector<Vector3> &normals = mesh_geometry->GetNormals();
	const std::__1::vector<Color> &vertex_colors = mesh_geometry->GetVertexColors(0);

	// material id, primitive_type(triangles,lines, points etc), SurfaceData
	Map<int, Map<uint32_t, FBXSplitBySurfaceVertexMapping> > surface_split_by_material_primitive;

	// we need to map faces back to vertexes
	{
		// Map Reduce Algorithm
		// The problem: reduce face primitives and also reduce material indices without duplicating vertexes :D
		// vertex count (1,2,3,4, etc), FBX Surface Data (uvs and indices for faces...)
		// yeah two sets.. uhuh
		// you read that correct. <3
		//Map<uint32_t, FBXSplitBySurfaceVertexMapping> primitive_geometry; // triangles, points, lines, quads

		// material id, indices list
		//Map<int, Vector<int>> material_surfaces;

		//		// fbx vertex id - value stored in the array is the material number
		//		for(uint32_t fbx_vertex_id = 0; fbx_vertex_id < material_indices.size(); fbx_vertex_id++) {
		//			const int material_id = material_indices[fbx_vertex_id];
		//			material_surfaces[material_id].push_back(fbx_vertex_id);
		//		}

		// Mesh face data - split based on geometry type
		uint32_t cursor = 0;
		for (uint32_t face_id = 0; face_id < face_vertex_counts.size(); face_id++) {
			uint32_t vertex_count = face_vertex_counts[face_id];
			for (uint32_t y = 0; y < vertex_count; y++) {

				// some files don't have these configured at all :P
				int material_id = 0;
				if (cursor < material_indices.size()) {
					material_id = material_indices[cursor];
				}

				FBXSplitBySurfaceVertexMapping &mapping = surface_split_by_material_primitive[material_id][vertex_count];
				mapping.vertex_id.push_back(cursor);

				// ensure we aren't outside available indexes, some will be
				if (cursor < uv_coordinates_0.size()) {
					mapping.add_uv_0(uv_coordinates_0[cursor]);
				}

				if (cursor < uv_coordinates_1.size()) {
					mapping.add_uv_1(uv_coordinates_1[cursor]);
				}

				if (cursor < normals.size()) {
					mapping.normals.push_back(normals[cursor]);
				}

				if (cursor < vertex_colors.size()) {
					mapping.colors.push_back(vertex_colors[cursor]);
				}

				cursor++; // we manually increment cursor, we are essentially creating a new mesh.
				// each surface split is a mesh
			}
		}
	}

	print_verbose("[vertex count for mesh] " + itos(vertices.size()));

	// triangles surface for triangles
	for (int material = 0; material < surface_split_by_material_primitive.size(); material++) {
		if (surface_split_by_material_primitive[material].has(3)) {
			FBXSplitBySurfaceVertexMapping &mapping = surface_split_by_material_primitive[material][3];
			Vector<size_t> &mesh_vertex_ids = mapping.vertex_id;
			//Vector<size_t> &vertex_weight_mapping = mapping.vertex_weight_id;
			st->begin(Mesh::PRIMITIVE_TRIANGLES);

			// stream in vertexes
			for (int x = 0; x < mesh_vertex_ids.size(); x++) {
				size_t vertex_id = mapping.vertex_id[x];
				GenFBXWeightInfo(mesh_geometry, st, vertex_id);

				mapping.GenerateSurfaceMaterial(st, vertex_id);
				st->add_vertex(vertices[vertex_id]);
			}

			for (int x = 0; x < mesh_vertex_ids.size(); x += 3) {
				st->add_index(x + 2);
				st->add_index(x + 1);
				st->add_index(x);
			}

			Array triangle_mesh = st->commit_to_arrays();
			triangle_mesh.resize(VS::ARRAY_MAX);
			Array morphs;

			mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, triangle_mesh, morphs);
		}

		if (surface_split_by_material_primitive[material].has(4)) {
			FBXSplitBySurfaceVertexMapping &mapping = surface_split_by_material_primitive[material][4];
			Vector<size_t> &mesh_vertex_ids = mapping.vertex_id;

			print_verbose("quads: " + itos(mesh_vertex_ids.size()));
			st->begin(Mesh::PRIMITIVE_TRIANGLES);

			// stream in vertexes
			for (int x = 0; x < mesh_vertex_ids.size(); x++) {
				size_t vertex_id = mesh_vertex_ids[x];
				GenFBXWeightInfo(mesh_geometry, st, vertex_id);

				mapping.GenerateSurfaceMaterial(st, vertex_id);
				//print_verbose("vert: " + quads[x]);
				st->add_vertex(vertices[vertex_id]);
			}

			//int cursor = 0;
			for (int x = 0; x < mesh_vertex_ids.size(); x += 4) {
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

			Array triangle_mesh = st->commit_to_arrays();
			triangle_mesh.resize(VS::ARRAY_MAX);
			Array morphs;
			mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, triangle_mesh, morphs);
		}
	}

	if (normals.size() > 0) {
		st->generate_tangents();
	}
	// const std::vector<uint32_t> &face_primitives = mesh_geometry->GetFaceIndexCounts();

	// uint32_t cursor;
	// for (uint32_t indice_count : face_primitives) {
	// 	if (indice_count == 1) {
	// 		st->add_index(0 + cursor);
	// 		cursor++;
	// 	} else if (indice_count == 2) {
	// 		st->add_index(0 + cursor);
	// 		st->add_index(1 + cursor);
	// 		cursor += 2;
	// 	} else if (indice_count == 3) {
	// 		st->add_index(0 + cursor);
	// 		st->add_index(0 + cursor);
	// 		st->add_index(3 + cursor);
	// 		st->add_index(2 + cursor);

	// 		cursor += 6;
	// 	}
	// }

	// Ref<SpatialMaterial> material;
	// material.instance();
	// material->set_cull_mode(SpatialMaterial::CullMode::CULL_DISABLED);

	// mesh->add_surface_from_arrays(Mesh::PRIMITIVE_POINTS, array_mesh, morphs);
	//mesh->surface_set_material(0, material);
	// okay now enable it
	mesh->set_name(ImportUtils::FBXNodeToName(mesh_geometry->Name()));

	MeshInstance *godot_mesh = memnew(MeshInstance);
	godot_mesh->set_mesh(mesh);

	return godot_mesh;
}


void FBXMeshVertexData::GenFBXWeightInfo( const Assimp::FBX::MeshGeometry *mesh_geometry, Ref<SurfaceTool> st,
											  size_t vertex_id) {
		// mesh is de-indexed by FBX Mesh class so id's from document aren't the same
		// this convention will rewrite weight vertex ids safely, but most importantly only once :)
		if (!valid_weight_indexes) {
			FixWeightData(mesh_geometry);
		}

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

				//print_error("final count: " + itos(VertexWeights->weights.size()));

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


void FBXMeshVertexData::FixWeightData(const Assimp::FBX::MeshGeometry *mesh_geometry) {
	if (!valid_weight_indexes && mesh_geometry) {
		Map<size_t, Ref<VertexMapping> > fixed_weight_info;
		for (Map<size_t, Ref<VertexMapping> >::Element *element = vertex_weights.front(); element; element = element->next()) {
			unsigned int count;
			const unsigned int *vert = mesh_geometry->ToOutputVertexIndex(element->key(), count);
			//  print_verbose("begin translation of weight information");
			if (vert != nullptr) {
				for (unsigned int x = 0; x < count; x++) {
					//                        print_verbose("input vertex: " + itos(element->key()) + ", output vert data: " + itos(vert[x]) +
					//                                      " count: " + itos(count));

					// write fixed weight info to the new temp array
					fixed_weight_info.insert(vert[x], element->value());
				}
			}
		}

		//    print_verbose("size of fixed weight info:" + itos(fixed_weight_info.size()));

		// destructive part of this operation is done here
		vertex_weights = fixed_weight_info;

		//  print_verbose("completed weight fixup");
		valid_weight_indexes = true;
	}
}