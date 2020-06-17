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

MeshInstance *FBXMeshData::create_fbx_mesh(const Assimp::FBX::MeshGeometry *mesh_geometry, const Assimp::FBX::Model *model) {

	print_verbose("[doc] FBX creating godot mesh for: " + ImportUtils::FBXNodeToName(model->Name()));

	print_verbose("[doc] mesh has " + itos(max_weight_count) + " bone weights");

	Ref<ArrayMesh> mesh;
	mesh.instance();

	// TODO why use a Ref?
	Ref<SurfaceTool> st;
	st.instance();

	const std::vector<int> &material_indices = mesh_geometry->GetMaterialIndices();

	bool no_material_found = material_indices.size() == 0;

	if (no_material_found) {
		print_error("no material is configured for mesh " + ImportUtils::FBXNodeToName(model->Name()));
	}

	std::vector<uint32_t> face_vertex_counts = mesh_geometry->GetFaceIndexCounts();

	// godot has two uv coordinate channels
	const std::vector<Vector2> &uv_coordinates_0 = mesh_geometry->GetTextureCoords(0);
	const std::vector<Vector2> &uv_coordinates_1 = mesh_geometry->GetTextureCoords(1);
	const std::vector<Color> &vertex_colors = mesh_geometry->GetVertexColors(0);
	const std::vector<Vector3> &normals = mesh_geometry->GetNormals();

	// material id, primitive_type(triangles,lines, points etc), SurfaceData
	Map<int, Map<uint32_t, FBXSplitBySurfaceVertexMapping> > surface_split_by_material_primitive;
	Map<int, Map<uint32_t, Vector<FBXSplitBySurfaceVertexMapping> > > surface_blend_shapes;

	// Blend shapes in FBX
	// copy the entire mesh
	// match all the vertexes
	// match all normals to the 'index' in the ShapeGeometry from the blend shape
	// each blend shape is a clone of the original mesh
	// then the mesh is overwritten at indexes to provide a BLENDED SHAPE.
	// this means the core/root mesh is read
	// then after we copy and clone it, and apply the offsets

	{
		// data is split up
		std::vector<Vector3> vertices = mesh_geometry->GetVertices();

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
				mapping.vertex_with_id[cursor] = vertices[cursor];

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

		// Process Blend shapes.
		// algorithm
		// read existing mesh data
		// import blend shape classes
		// check for valid blend shapes
		// copy entire mesh
		// update vertexes based on index in the vertex array
		// apply same to normal data
		// this means i can render the same as other methods
		if (mesh_geometry->BlendShapeCount() > 0) {
			for (Map<int, Map<uint32_t, FBXSplitBySurfaceVertexMapping> >::Element *material_mesh = surface_split_by_material_primitive.front(); material_mesh; material_mesh = material_mesh->next()) {
				for (Map<uint32_t, FBXSplitBySurfaceVertexMapping>::Element *mesh_primitive = material_mesh->value().front(); mesh_primitive; mesh_primitive = mesh_primitive->next()) {

					// Now map reduce in the blend shapes
					for (const Assimp::FBX::BlendShape *blendShape : mesh_geometry->GetBlendShapes()) {
						for (const Assimp::FBX::BlendShapeChannel *blendShapeChannel : blendShape->BlendShapeChannels()) {
							const std::vector<const Assimp::FBX::ShapeGeometry *> &shapeGeometries = blendShapeChannel->GetShapeGeometries();

							for (const Assimp::FBX::ShapeGeometry *shapeGeometry : shapeGeometries) {
								const std::vector<Vector3> &blend_vertices = shapeGeometry->GetVertices();
								const std::vector<Vector3> &blend_normals = shapeGeometry->GetNormals();
								const std::vector<unsigned int> &vertex_index = shapeGeometry->GetIndices();

								// intentionally copy entire mesh :O
								int material_id = material_mesh->key();
								FBXSplitBySurfaceVertexMapping blend_shape_mesh_copy = mesh_primitive->value();
								uint32_t primitive_type = mesh_primitive->key();

								// now update our copy with the new data from the blend shape
								// as FBX blend shapes are just mesh diff's with the index being the vertex ID not the indice.
								int idx = 0;
								for (unsigned int id : vertex_index) {
									// id is the cursor
									if (blend_shape_mesh_copy.vertex_with_id.has(id)) {
										// Actual blending - rewrite the same ID with the correct vertex position

										// todo: various formats supported go here.
										blend_shape_mesh_copy.vertex_with_id[id] += blend_vertices[idx];

										int counted_position = -1;
										for (Map<size_t, Vector3>::Element *vertex = blend_shape_mesh_copy.vertex_with_id.front(); vertex; vertex = vertex->next()) {
											counted_position++;
											if (vertex->key() == id) {
												print_verbose("found valid vertex count for mesh vertex key");
												break;
											}
										}

										if (counted_position == -1) {
											print_error("invalid position for normal...");
										}
										// update copy of normals with correct blend shape values.
										blend_shape_mesh_copy.normals.set(counted_position, blend_normals[idx]);
										print_verbose("[success] mesh updated and cursor has valid match for " + itos(id));
										idx++;
									}
								}

								// make it a real thing
								surface_blend_shapes[material_id][primitive_type].push_back(blend_shape_mesh_copy);
							}
						}
					}
				}
			}
		}
	}

	//print_verbose("[vertex count for mesh] " + itos(vertices.size()));

	int blend_shape_count = 0;
	for (const Assimp::FBX::BlendShape *blendShape : mesh_geometry->GetBlendShapes()) {
		for (const Assimp::FBX::BlendShapeChannel *blendShapeChannel : blendShape->BlendShapeChannels()) {
			const std::vector<const Assimp::FBX::ShapeGeometry *> &shapeGeometries = blendShapeChannel->GetShapeGeometries();
			for (size_t i = 0; i < shapeGeometries.size(); i++) {
				const Assimp::FBX::ShapeGeometry *shapeGeometry = shapeGeometries.at(i);
				String anim_mesh_name = String(ImportUtils::FBXAnimMeshName(shapeGeometry->Name()).c_str());
				print_verbose("blend shape mesh name: " + anim_mesh_name);

				// empty shape name should still work
				if (anim_mesh_name.empty()) {
					anim_mesh_name = String("morph_") + itos(i);
				}

				// godot register blend shape.
				mesh->add_blend_shape(anim_mesh_name);
				mesh->set_blend_shape_mode(Mesh::BLEND_SHAPE_MODE_NORMALIZED); // TODO always normalized, Why?
				blend_shape_count++;
			}
		}
	}

	print_verbose("blend shape count: " + itos(blend_shape_count));

	// triangles surface for triangles

	// basically a mesh is split by triangles, quads, lines and then by material. Godot requires this
	for (int material = 0; material < surface_split_by_material_primitive.size(); material++) {
		for( Map<uint32_t, FBXSplitBySurfaceVertexMapping>::Element *mesh_with_face_vertex_count = surface_split_by_material_primitive[material].front(); mesh_with_face_vertex_count; mesh_with_face_vertex_count=mesh_with_face_vertex_count->next())
		{
			const uint32_t face_vertex_count = mesh_with_face_vertex_count->key();

			Array morphs = Array();

			//
			// Blend shape grabber
			//
			if (surface_blend_shapes.has(material)) {
				if (surface_blend_shapes[material].has(face_vertex_count)) {
					// we must grab the blend shape based on the other array not the current one since it's another mesh we created
					const Vector<FBXSplitBySurfaceVertexMapping> &mappings = surface_blend_shapes[material][face_vertex_count];
					for (int m = 0; m < mappings.size(); m += 1) {
						const FBXSplitBySurfaceVertexMapping &mapping = mappings[m];
						st->begin(Mesh::PRIMITIVE_TRIANGLES);

						// Stream in vertexes.
						// Note: The blend shape doesn't need the indices
						for (const Map<size_t, Vector3>::Element *vertex_element = mapping.vertex_with_id.front(); vertex_element; vertex_element = vertex_element->next()) {
							const Vector3 vertex = vertex_element->value();
							size_t vertex_id = vertex_element->key(); // vertex id is the ORIGINAL FBX vertex id, required for blend shapes and weights.
							GenFBXWeightInfo(mesh_geometry, st, vertex_id);
							mapping.GenerateSurfaceMaterial(st, vertex_id);
							st->add_vertex(vertex);
						}

						// generate tangents
						if (mapping.normals.size() > 0) {
							st->generate_tangents();
						}

						morphs.push_back(st->commit_to_arrays());
					}
				}
			}

			// Ordinary mesh handler - handles triangles, points, lines and quads.
			// converts points, lines, quads to triangles
			// indices are generated for triangles too.

			const FBXSplitBySurfaceVertexMapping &mapping = mesh_with_face_vertex_count->value();
			st->begin(Mesh::PRIMITIVE_TRIANGLES);

			// stream in vertexes
			for (Map<size_t, Vector3>::Element *vertex_element = mapping.vertex_with_id.front(); vertex_element; vertex_element = vertex_element->next()) {
				const Vector3 vertex = vertex_element->value();
				size_t vertex_id = vertex_element->key(); // vertex id is the ORIGINAL FBX vertex id, required for blend shapes and weights.
				GenFBXWeightInfo(mesh_geometry, st, vertex_id);
				mapping.GenerateSurfaceMaterial(st, vertex_id);
				st->add_vertex(vertex);
			}

			// generate indices - doesn't really matter much.
			mapping.GenerateIndices(st, face_vertex_count);

			// generate tangents
			if (mapping.normals.size() > 0) {
				st->generate_tangents();
			}

			Array mesh_committed = st->commit_to_arrays();

			mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, mesh_committed, morphs);
		}
	}


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

void FBXMeshData::GenFBXWeightInfo(const Assimp::FBX::MeshGeometry *mesh_geometry, Ref<SurfaceTool> st,
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

void FBXMeshData::FixWeightData(const Assimp::FBX::MeshGeometry *mesh_geometry) {
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