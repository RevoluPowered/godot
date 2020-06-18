/*
Open Asset Import Library (assimp)
----------------------------------------------------------------------

Copyright (c) 2006-2019, assimp team


All rights reserved.

Redistribution and use of this software in source and binary forms,
with or without modification, are permitted provided that the
following conditions are met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

* Redistributions in binary form must reproduce the above
  copyright notice, this list of conditions and the
  following disclaimer in the documentation and/or other
  materials provided with the distribution.

* Neither the name of the assimp team, nor the names of its
  contributors may be used to endorse or promote products
  derived from this software without specific prior
  written permission of the assimp team.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

----------------------------------------------------------------------
*/

/** @file  FBXMeshGeometry.cpp
 *  @brief Assimp::FBX::MeshGeometry implementation
 */

#ifndef ASSIMP_BUILD_NO_FBX_IMPORTER

#include <functional>

#include "FBXDocument.h"
#include "FBXDocumentUtil.h"
#include "FBXImportSettings.h"
#include "FBXMeshGeometry.h"
#include <core/math/vector3.h>

namespace Assimp {
namespace FBX {

using namespace Util;

// ------------------------------------------------------------------------------------------------
Geometry::Geometry(uint64_t id, const Element &element, const std::string &name, const Document &doc) :
		Object(id, element, name), skin() {
	const std::vector<const Connection *> &conns = doc.GetConnectionsByDestinationSequenced(ID(), "Deformer");
	for (const Connection *con : conns) {
		const Skin *const sk = ProcessSimpleConnection<Skin>(*con, false, "Skin -> Geometry", element);
		if (sk) {
			skin = sk;
		}
		const BlendShape *const bsp = ProcessSimpleConnection<BlendShape>(*con, false, "BlendShape -> Geometry",
				element);
		if (bsp) {
			blendShapes.push_back(bsp);
		}
	}
}

// ------------------------------------------------------------------------------------------------
Geometry::~Geometry() {
	// empty
}

// ------------------------------------------------------------------------------------------------
const std::vector<const BlendShape *> &Geometry::GetBlendShapes() const {
	return blendShapes;
}

// ------------------------------------------------------------------------------------------------
const Skin *Geometry::DeformerSkin() const {
	return skin;
}

// ------------------------------------------------------------------------------------------------
MeshGeometry::MeshGeometry(uint64_t id, const Element &element, const std::string &name, const Document &doc) :
		Geometry(id, element, name, doc) {
	const Scope *sc = element.Compound();
	if (!sc) {
		DOMError("failed to read Geometry object (class: Mesh), no data scope found");
	}

	if (!HasElement(*sc, "Vertices")) {
		return; // this happened!
	}

	// must have Mesh elements:
	const Element &Vertices = GetRequiredElement(*sc, "Vertices", &element);
	const Element &PolygonVertexIndex = GetRequiredElement(*sc, "PolygonVertexIndex", &element);

	// optional Mesh elements:
	const ElementCollection &Layer = sc->GetCollection("Layer");

	// read mesh data into arrays
	// todo: later we can actually store arrays for these :)
	// and not vector3
	ParseVectorDataArray(m_vertices, Vertices);
	ParseVectorDataArray(m_face_indices, PolygonVertexIndex);

	if (m_vertices.empty()) {
		print_error("encountered mesh with no vertices");
	}

	if (m_face_indices.empty()) {
		print_error("encountered mesh with no faces");
	}

	// now read the sub mesh information from the geometry (normals, uvs, etc)
	for (ElementMap::const_iterator it = Layer.first; it != Layer.second; ++it) {
		const TokenList &tokens = (*it).second->Tokens();
		const Scope &layer = GetRequiredScope(*(*it).second);
		const ElementCollection &LayerElement = layer.GetCollection("LayerElement");
		for (ElementMap::const_iterator eit = LayerElement.first; eit != LayerElement.second; ++eit) {
			std::string name = (*eit).first;
			Element *element_layer = (*eit).second;
			const Scope &layer_element = GetRequiredScope(*element_layer);
			std::cout << "[read layer] " << name << std::endl;
			const Element &Type = GetRequiredElement(layer_element, "Type");
			const Element &TypedIndex = GetRequiredElement(layer_element, "TypedIndex");
			const std::string &type = ParseTokenAsString(GetRequiredToken(Type, 0));
			const int typedIndex = ParseTokenAsInt(GetRequiredToken(TypedIndex, 0));

			std::cout << "[layer element] type: " << type << ", " << typedIndex << std::endl;

			// get object / mesh directly from the FBX by the element ID.
			const Scope &top = GetRequiredScope(element);

			// Get collection of elements from the NormalLayerMap
			// this must contain our proper elements.
			const ElementCollection candidates = top.GetCollection(type);

			/* typedef std::vector< Scope* > ScopeList;
			 * typedef std::fbx_unordered_multimap< std::string, Element* > ElementMap;
			 * typedef std::pair<ElementMap::const_iterator,ElementMap::const_iterator> ElementCollection;
			 */

			for (ElementMap::const_iterator cand_iter = candidates.first; cand_iter != candidates.second; ++cand_iter) {
				std::string val = (*cand_iter).first;
				//Element *element = (*canditer).second;
				std::cout << "key: " << val << std::endl;

				const Scope& layer_scope = GetRequiredScope(*(*cand_iter).second);
				const Token& layer_token = GetRequiredToken(*(*cand_iter).second, 0);
				const int index = ParseTokenAsInt(layer_token);
				if (index == typedIndex) {
					const std::string &MappingInformationType = ParseTokenAsString(GetRequiredToken(
							GetRequiredElement(layer_scope, "MappingInformationType"), 0));

					const std::string &ReferenceInformationType = ParseTokenAsString(GetRequiredToken(
							GetRequiredElement(layer_scope, "ReferenceInformationType"), 0));

				// Not required:
				// LayerElementTangent
				// LayerElementBinormal - perpendicular to tangent.
				if (type == "LayerElementUV") {
						if(index == 0) {
							m_uv_0 = ResolveVertexDataArray<Vector2>(layer_scope, MappingInformationType, ReferenceInformationType, "UV");
						}
						else if(index == 1)
						{
							m_uv_1 = ResolveVertexDataArray<Vector2>(layer_scope, MappingInformationType, ReferenceInformationType, "UV");
						}
					} else if (type == "LayerElementMaterial") {
						m_material_id = ResolveVertexDataArray<int>(layer_scope, MappingInformationType, ReferenceInformationType, "Materials");
					} else if (type == "LayerElementNormal") {
						m_normals = ResolveVertexDataArray<Vector3>(layer_scope, MappingInformationType, ReferenceInformationType, "Normals");
					} else if (type == "LayerElementColor") {
						m_colors = ResolveVertexDataArray<Color>(layer_scope, MappingInformationType, ReferenceInformationType, "Colors");
					}
				}
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
MeshGeometry::~MeshGeometry() {
	// empty
}



// ------------------------------------------------------------------------------------------------
// Lengthy utility function to read and resolve a FBX vertex data array - that is, the
// output is in polygon vertex order. This logic is used for reading normals, UVs, colors,
// tangents ..
template <class T>
MeshGeometry::MappingData<T> MeshGeometry::ResolveVertexDataArray(const Scope &source,
		const std::string &MappingInformationType,
		const std::string &ReferenceInformationType,
		const std::string &dataElementName) {

	// UVIndex, MaterialIndex, NormalIndex, etc..
	std::string indexDataElementName = dataElementName + "Index";
	// goal: expand everything to be per vertex

	ReferenceType l_ref_type = ReferenceType::direct;

	// purposefully merging legacy to IndexToDirect
	if (ReferenceInformationType == "IndexToDirect" || ReferenceInformationType == "Index") {
		// set non legacy index to direct mapping
		l_ref_type = ReferenceType::index_to_direct;

		// override invalid files - should not happen but if it does we're safe.
		if (!HasElement(source, indexDataElementName)) {
			l_ref_type = ReferenceType::direct;
		}
	}

	MapType l_map_type = MapType::none;

	if (MappingInformationType == "None") {
		l_map_type = MapType::none;
	} else if (MappingInformationType == "ByVertice") {
		l_map_type = MapType::vertex;
	} else if (MappingInformationType == "ByPolygonVertex") {
		l_map_type = MapType::polygon_vertex;
	} else if (MappingInformationType == "ByPolygon") {
		l_map_type = MapType::polygon;
	} else if (MappingInformationType == "ByEdge") {
		l_map_type = MapType::edge;
	} else if (MappingInformationType == "AllSame") {
		l_map_type = MapType::all_the_same;
	} else {
		print_error("invalid mapping type: " + String(MappingInformationType.c_str()));
	}

	// create mapping data
	MeshGeometry::MappingData<T> tempData;
	tempData.map_type = l_map_type;
	tempData.ref_type = l_ref_type;

	// parse data into array
	ParseVectorDataArray(tempData.data, GetRequiredElement(source, dataElementName));

	// index array wont always exist
	const Element* element = GetOptionalElement(source, indexDataElementName);
	if(element) {
		ParseVectorDataArray(tempData.index, *element );
	}

	return tempData;
}
// ------------------------------------------------------------------------------------------------
ShapeGeometry::ShapeGeometry(uint64_t id, const Element &element, const std::string &name, const Document &doc) :
		Geometry(id, element, name, doc) {
	const Scope *sc = element.Compound();
	if (nullptr == sc) {
		DOMError("failed to read Geometry object (class: Shape), no data scope found");
	}
	const Element &Indexes = GetRequiredElement(*sc, "Indexes", &element);
	const Element &Normals = GetRequiredElement(*sc, "Normals", &element);
	const Element &Vertices = GetRequiredElement(*sc, "Vertices", &element);
	ParseVectorDataArray(m_indices, Indexes);
	ParseVectorDataArray(m_vertices, Vertices);
	ParseVectorDataArray(m_normals, Normals);
}

// ------------------------------------------------------------------------------------------------
ShapeGeometry::~ShapeGeometry() {
	// empty
}
// ------------------------------------------------------------------------------------------------
const std::vector<Vector3> &ShapeGeometry::GetVertices() const {
	return m_vertices;
}
// ------------------------------------------------------------------------------------------------
const std::vector<Vector3> &ShapeGeometry::GetNormals() const {
	return m_normals;
}
// ------------------------------------------------------------------------------------------------
const std::vector<unsigned int> &ShapeGeometry::GetIndices() const {
	return m_indices;
}
// ------------------------------------------------------------------------------------------------
LineGeometry::LineGeometry(uint64_t id, const Element &element, const std::string &name, const Document &doc) :
		Geometry(id, element, name, doc) {
	const Scope *sc = element.Compound();
	if (!sc) {
		DOMError("failed to read Geometry object (class: Line), no data scope found");
	}
	const Element &Points = GetRequiredElement(*sc, "Points", &element);
	const Element &PointsIndex = GetRequiredElement(*sc, "PointsIndex", &element);
	ParseVectorDataArray(m_vertices, Points);
	ParseVectorDataArray(m_indices, PointsIndex);
}

// ------------------------------------------------------------------------------------------------
LineGeometry::~LineGeometry() {
	// empty
}
// ------------------------------------------------------------------------------------------------
const std::vector<Vector3> &LineGeometry::GetVertices() const {
	return m_vertices;
}
// ------------------------------------------------------------------------------------------------
const std::vector<int> &LineGeometry::GetIndices() const {
	return m_indices;
}
} // namespace FBX
} // namespace Assimp
#endif
