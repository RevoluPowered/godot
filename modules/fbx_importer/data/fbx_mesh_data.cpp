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
			mesh_geometry->get_face_indices(),
			mesh_geometry->get_normals(),
			CombinationMode::Avg, // TODO How can we make this dynamic?
			&validate_vector_2or3);

	Vector<Vector2> uvs_0 = extract_per_vertex_data(
			vertex_count,
			mesh_geometry->get_face_indices(),
			mesh_geometry->get_uv_0(),
			CombinationMode::TakeFirst,
			&validate_vector_2or3);

	Vector<Vector2> uvs_1 = extract_per_vertex_data(
			vertex_count,
			mesh_geometry->get_face_indices(),
			mesh_geometry->get_uv_1(),
			CombinationMode::TakeFirst,
			&validate_vector_2or3);

	Vector<Color> colors = extract_per_vertex_data(
			vertex_count,
			mesh_geometry->get_face_indices(),
			mesh_geometry->get_colors(),
			CombinationMode::TakeFirst,
			&no_validation);

	// TODO what about tangends?
	// TODO what about binormals?
	// TODO there is other?

	Vector<int> materials = extract_per_vertex_data(
			vertex_count,
			mesh_geometry->get_face_indices(),
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
	for (size_t polygon_vertex_index = 0; polygon_vertex_index < mesh_geometry->get_face_indices().size(); polygon_vertex_index += 1) {

		polygon_vertices.push_back(get_vertex_from_polygon_vertex(mesh_geometry->get_face_indices(), polygon_vertex_index));

		if (is_end_of_polygon(mesh_geometry->get_face_indices(), polygon_vertex_index)) {
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
	// TODO

	// Phase 6. Compose the mesh and return it.
	Ref<ArrayMesh> mesh;
	mesh.instance();

	for (OAHashMap<int, Ref<SurfaceTool> >::Iterator it = surfaces.iter(); it.valid; it = surfaces.next_iter(it)) {
		mesh->add_surface_from_arrays(
				Mesh::PRIMITIVE_TRIANGLES,
				(*it.value)->commit_to_arrays()
				// TODO blend shapes / morphs, goes here
		);
	}

	MeshInstance *godot_mesh = memnew(MeshInstance);
	godot_mesh->set_mesh(mesh);

	return godot_mesh;

	//
	//	print_verbose("[doc] FBX creating godot mesh for: " + ImportUtils::FBXNodeToName(model->Name()));
	//
	//	print_verbose("[doc] mesh has " + itos(max_weight_count) + " bone weights");
	//
	//	Ref<ArrayMesh> mesh;
	//	mesh.instance();
	//
	//	// TODO why use a Ref?
	//	Ref<SurfaceTool> st;
	//	st.instance();
	//
	//	const std::vector<int> &material_indices = mesh_geometry->GetMaterialIndices();
	//
	//	bool no_material_found = material_indices.size() == 0;
	//
	//	if (no_material_found) {
	//		print_error("no material is configured for mesh " + ImportUtils::FBXNodeToName(model->Name()));
	//	}
	//
	//	std::vector<uint32_t> face_vertex_counts = mesh_geometry->GetFaceIndexCounts();
	//
	//	// godot has two uv coordinate channels
	//	const std::vector<Vector2> &uv_coordinates_0 = mesh_geometry->GetTextureCoords(0);
	//	const std::vector<Vector2> &uv_coordinates_1 = mesh_geometry->GetTextureCoords(1);
	//	const std::vector<Color> &vertex_colors = mesh_geometry->GetVertexColors(0);
	//	const std::vector<Vector3> &normals = mesh_geometry->GetNormals();
	//
	//	// material id, primitive_type(triangles,lines, points etc), SurfaceData
	//	Map<int, Map<uint32_t, FBXSplitBySurfaceVertexMapping> > surface_split_by_material_primitive;
	//	Map<int, Map<uint32_t, Vector<FBXSplitBySurfaceVertexMapping> > > surface_blend_shapes;
	//
	//	// Blend shapes in FBX
	//	// copy the entire mesh
	//	// match all the vertexes
	//	// match all normals to the 'index' in the ShapeGeometry from the blend shape
	//	// each blend shape is a clone of the original mesh
	//	// then the mesh is overwritten at indexes to provide a BLENDED SHAPE.
	//	// this means the core/root mesh is read
	//	// then after we copy and clone it, and apply the offsets
	//
	//	{
	//		// data is split up
	//		const std::vector<Vector3> &vertices = mesh_geometry->GetVertices();
	//
	//		// Map Reduce Algorithm
	//		// The problem: reduce face primitives and also reduce material indices without duplicating vertexes :D
	//		// vertex count (1,2,3,4, etc), FBX Surface Data (uvs and indices for faces...)
	//		// yeah two sets.. uhuh
	//		// you read that correct. <3
	//		//Map<uint32_t, FBXSplitBySurfaceVertexMapping> primitive_geometry; // triangles, points, lines, quads
	//
	//		// material id, indices list
	//		//Map<int, Vector<int>> material_surfaces;
	//
	//		//		// fbx vertex id - value stored in the array is the material number
	//		//		for(uint32_t fbx_vertex_id = 0; fbx_vertex_id < material_indices.size(); fbx_vertex_id++) {
	//		//			const int material_id = material_indices[fbx_vertex_id];
	//		//			material_surfaces[material_id].push_back(fbx_vertex_id);
	//		//		}
	//
	//		// Mesh face data - split based on geometry type
	//		uint32_t cursor = 0;
	//		for (uint32_t face_id = 0; face_id < face_vertex_counts.size(); face_id++) {
	//			uint32_t vertex_count = face_vertex_counts[face_id];
	//			for (uint32_t y = 0; y < vertex_count; y++) {
	//
	//				// some files don't have these configured at all :P
	//				int material_id = 0;
	//				if (cursor < material_indices.size()) {
	//					material_id = material_indices[cursor];
	//				}
	//
	//				FBXSplitBySurfaceVertexMapping &mapping = surface_split_by_material_primitive[material_id][vertex_count];
	//				mapping.vertex_with_id[cursor] = vertices[cursor];
	//
	//				// ensure we aren't outside available indexes, some will be
	//				if (cursor < uv_coordinates_0.size()) {
	//					mapping.add_uv_0(uv_coordinates_0[cursor]);
	//				}
	//
	//				if (cursor < uv_coordinates_1.size()) {
	//					mapping.add_uv_1(uv_coordinates_1[cursor]);
	//				}
	//
	//				if (cursor < normals.size()) {
	//					mapping.normals.push_back(normals[cursor]);
	//				}
	//
	//				if (cursor < vertex_colors.size()) {
	//					mapping.colors.push_back(vertex_colors[cursor]);
	//				}
	//
	//				cursor++; // we manually increment cursor, we are essentially creating a new mesh.
	//				// each surface split is a mesh
	//			}
	//		}
	//
	//		// Process Blend shapes.
	//		// algorithm
	//		// read existing mesh data
	//		// import blend shape classes
	//		// check for valid blend shapes
	//		// copy entire mesh
	//		// update vertexes based on index in the vertex array
	//		// apply same to normal data
	//		// this means i can render the same as other methods
	//		if (mesh_geometry->BlendShapeCount() > 0) {
	//			for (Map<int, Map<uint32_t, FBXSplitBySurfaceVertexMapping> >::Element *material_mesh = surface_split_by_material_primitive.front(); material_mesh; material_mesh = material_mesh->next()) {
	//				for (Map<uint32_t, FBXSplitBySurfaceVertexMapping>::Element *mesh_primitive = material_mesh->value().front(); mesh_primitive; mesh_primitive = mesh_primitive->next()) {
	//
	//					// Now map reduce in the blend shapes
	//					for (const Assimp::FBX::BlendShape *blendShape : mesh_geometry->GetBlendShapes()) {
	//						for (const Assimp::FBX::BlendShapeChannel *blendShapeChannel : blendShape->BlendShapeChannels()) {
	//							const std::vector<const Assimp::FBX::ShapeGeometry *> &shapeGeometries = blendShapeChannel->GetShapeGeometries();
	//
	//							for (const Assimp::FBX::ShapeGeometry *shapeGeometry : shapeGeometries) {
	//								const std::vector<Vector3> &blend_vertices = shapeGeometry->GetVertices();
	//								const std::vector<Vector3> &blend_normals = shapeGeometry->GetNormals();
	//								const std::vector<unsigned int> &blend_vertex_indices = shapeGeometry->GetIndices();
	//
	//								// intentionally copy entire mesh :O
	//								int material_id = material_mesh->key();
	//								FBXSplitBySurfaceVertexMapping blend_shape_mesh_copy = mesh_primitive->value();
	//								uint32_t primitive_type = mesh_primitive->key();
	//
	//								// now update our copy with the new data from the blend shape
	//								// as FBX blend shapes are just mesh diff's with the index being the vertex ID not the indice.
	//								for (unsigned int blend_vertex_index : blend_vertex_indices) {
	//									unsigned int indices_count;
	//									const unsigned int *indices = mesh_geometry->ToOutputVertexIndex(blend_vertex_index, indices_count);
	//									for (int i = 0; i < indices_count; i += 1) {
	//										const unsigned int index = indices[i];
	//
	//										// id is the cursor
	//										if (blend_shape_mesh_copy.vertex_with_id.has(index)) {
	//											// Actual blending - rewrite the same ID with the correct vertex position
	//
	//											// todo: various formats supported go here.
	//											blend_shape_mesh_copy.vertex_with_id[index] += blend_vertices[blend_vertex_index];
	//
	//											int counted_position = -1;
	//											for (Map<size_t, Vector3>::Element *vertex = blend_shape_mesh_copy.vertex_with_id.front(); vertex; vertex = vertex->next()) {
	//												counted_position++;
	//												if (vertex->key() == index) {
	//													print_verbose("found valid vertex count for mesh vertex key");
	//													break;
	//												}
	//											}
	//
	//											if (counted_position == -1) {
	//												print_error("invalid position for normal...");
	//											}
	//											// update copy of normals with correct blend shape values.
	//											blend_shape_mesh_copy.normals.set(counted_position, blend_normals[blend_vertex_index]);
	//											print_verbose("[success] mesh updated and cursor has valid match for " + itos(index));
	//										}
	//									}
	//								}
	//
	//								// make it a real thing
	//								surface_blend_shapes[material_id][primitive_type].push_back(blend_shape_mesh_copy);
	//							}
	//						}
	//					}
	//				}
	//			}
	//		}
	//	}
	//
	//	//print_verbose("[vertex count for mesh] " + itos(vertices.size()));
	//
	//	int blend_shape_count = 0;
	//	for (const Assimp::FBX::BlendShape *blendShape : mesh_geometry->GetBlendShapes()) {
	//		for (const Assimp::FBX::BlendShapeChannel *blendShapeChannel : blendShape->BlendShapeChannels()) {
	//			const std::vector<const Assimp::FBX::ShapeGeometry *> &shapeGeometries = blendShapeChannel->GetShapeGeometries();
	//			for (size_t i = 0; i < shapeGeometries.size(); i++) {
	//				const Assimp::FBX::ShapeGeometry *shapeGeometry = shapeGeometries.at(i);
	//				String anim_mesh_name = String(ImportUtils::FBXAnimMeshName(shapeGeometry->Name()).c_str());
	//				print_verbose("blend shape mesh name: " + anim_mesh_name);
	//
	//				// empty shape name should still work
	//				if (anim_mesh_name.empty()) {
	//					anim_mesh_name = String("morph_") + itos(i);
	//				}
	//
	//				// godot register blend shape.
	//				mesh->add_blend_shape(anim_mesh_name);
	//				mesh->set_blend_shape_mode(Mesh::BLEND_SHAPE_MODE_NORMALIZED); // TODO always normalized, Why?
	//				blend_shape_count++;
	//			}
	//		}
	//	}
	//
	//	print_verbose("blend shape count: " + itos(blend_shape_count));
	//
	//	// triangles surface for triangles
	//
	//	// basically a mesh is split by triangles, quads, lines and then by material. Godot requires this
	//	for (int material = 0; material < surface_split_by_material_primitive.size(); material++) {
	//		for (Map<uint32_t, FBXSplitBySurfaceVertexMapping>::Element *mesh_with_face_vertex_count = surface_split_by_material_primitive[material].front(); mesh_with_face_vertex_count; mesh_with_face_vertex_count = mesh_with_face_vertex_count->next()) {
	//			const uint32_t face_vertex_count = mesh_with_face_vertex_count->key();
	//
	//			Array morphs = Array();
	//
	//			//
	//			// Blend shape grabber
	//			//
	//			if (surface_blend_shapes.has(material)) {
	//				if (surface_blend_shapes[material].has(face_vertex_count)) {
	//					// we must grab the blend shape based on the other array not the current one since it's another mesh we created
	//					const Vector<FBXSplitBySurfaceVertexMapping> &mappings = surface_blend_shapes[material][face_vertex_count];
	//					for (int m = 0; m < mappings.size(); m += 1) {
	//						const FBXSplitBySurfaceVertexMapping &mapping = mappings[m];
	//						st->begin(Mesh::PRIMITIVE_TRIANGLES);
	//
	//						// Stream in vertexes.
	//						// Note: The blend shape doesn't need the indices
	//						for (const Map<size_t, Vector3>::Element *vertex_element = mapping.vertex_with_id.front(); vertex_element; vertex_element = vertex_element->next()) {
	//							const Vector3 vertex = vertex_element->value();
	//							size_t vertex_id = vertex_element->key(); // vertex id is the ORIGINAL FBX vertex id, required for blend shapes and weights.
	//							GenFBXWeightInfo(mesh_geometry, st, vertex_id);
	//							mapping.GenerateSurfaceMaterial(st, vertex_id);
	//							st->add_vertex(vertex);
	//						}
	//
	//						// generate tangents
	//						if (mapping.normals.size() > 0) {
	//							st->generate_tangents();
	//						}
	//
	//						morphs.push_back(st->commit_to_arrays());
	//					}
	//				}
	//			}
	//
	//			// Ordinary mesh handler - handles triangles, points, lines and quads.
	//			// converts points, lines, quads to triangles
	//			// indices are generated for triangles too.
	//
	//			const FBXSplitBySurfaceVertexMapping &mapping = mesh_with_face_vertex_count->value();
	//			st->begin(Mesh::PRIMITIVE_TRIANGLES);
	//
	//
	//			// todo: move this into godot.
	//			//	// generate output vertices, computing an adjacency table to
	//			//	// preserve the mapping from fbx indices to *this* indexing.
	////    	unsigned int count = 0;
	////    	for (int index : m_face_indices) {
	////    		// convert fbx document index to normal polygon index.
	////    		const int absi = index < 0 ? (-index - 1) : index;
	////
	////
	////    		// indices
	////    		print_verbose("index: " + itos(absi));
	////    	}
	//
	//			// stream in vertexes
	//			for (Map<size_t, Vector3>::Element *vertex_element = mapping.vertex_with_id.front(); vertex_element; vertex_element = vertex_element->next()) {
	//				const Vector3 vertex = vertex_element->value();
	//				size_t vertex_id = vertex_element->key(); // vertex id is the ORIGINAL FBX vertex id, required for blend shapes and weights.
	//				GenFBXWeightInfo(mesh_geometry, st, vertex_id);
	//				mapping.GenerateSurfaceMaterial(st, vertex_id);
	//				st->add_vertex(vertex);
	//			}
	//
	//			// generate indices - doesn't really matter much.
	//			mapping.GenerateIndices(st, face_vertex_count);
	//
	//			// generate tangents
	//			if (mapping.normals.size() > 0) {
	//				st->generate_tangents();
	//			}
	//
	//			Array mesh_committed = st->commit_to_arrays();
	//
	//			mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, mesh_committed, morphs);
	//		}
	//	}
	//
	//	// Ref<SpatialMaterial> material;
	//	// material.instance();
	//	// material->set_cull_mode(SpatialMaterial::CullMode::CULL_DISABLED);
	//
	//	// mesh->add_surface_from_arrays(Mesh::PRIMITIVE_POINTS, array_mesh, morphs);
	//	//mesh->surface_set_material(0, material);
	//	// okay now enable it
	//	mesh->set_name(ImportUtils::FBXNodeToName(mesh_geometry->Name()));
	//
	//	MeshInstance *godot_mesh = memnew(MeshInstance);
	//	godot_mesh->set_mesh(mesh);
	//
	//	return godot_mesh;
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