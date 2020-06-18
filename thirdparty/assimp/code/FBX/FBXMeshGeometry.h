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

#ifndef INCLUDED_AI_FBX_MESHGEOMETRY_H
#define INCLUDED_AI_FBX_MESHGEOMETRY_H

#include "core/vector.h"
#include "core/math/vector3.h"
#include "core/math/vector2.h"
#include "core/color.h"

#include "FBXDocument.h"
#include "FBXParser.h"

#include <iostream>

#define AI_MAX_NUMBER_OF_TEXTURECOORDS 4
#define AI_MAX_NUMBER_OF_COLOR_SETS 8

namespace Assimp {
namespace FBX {

/** 
 *  DOM base class for all kinds of FBX geometry 
 */
class Geometry : public Object {
public:
	Geometry(uint64_t id, const Element &element, const std::string &name, const Document &doc);
	virtual ~Geometry();

	/** Get the Skin attached to this geometry or NULL */
	const Skin *DeformerSkin() const;

	/** Get the BlendShape attached to this geometry or NULL */
	const std::vector<const BlendShape *> &GetBlendShapes() const;

	size_t BlendShapeCount() const
	{
		return blendShapes.size();
	}

private:
	const Skin *skin;
	std::vector<const BlendShape *> blendShapes;
};

typedef std::vector<int> MatIndexArray;

/** 
 *  DOM class for FBX geometry of type "Mesh"
 */
class MeshGeometry : public Geometry {
public:
	/** The class constructor */
	MeshGeometry(uint64_t id, const Element &element, const std::string &name, const Document &doc);

	/** The class destructor */
	virtual ~MeshGeometry();


	/* 	Reference type declared:
		- Direct (directly related to the mapping information type)
		- IndexToDirect (Map with key value, meaning depends on the MappingInformationType)

		ControlPoint is a vertex
		* None The mapping is undetermined.
		* ByVertex There will be one mapping coordinate for each surface control point/vertex.
			* If you have direct reference type verticies[x]
			* If you have IndexToDirect reference type the UV
		* ByPolygonVertex There will be one mapping coordinate for each vertex, for every polygon of which it is a part. This means that a vertex will have as many mapping coordinates as polygons of which it is a part. (Sorted by polygon, referencing vertex)
		* ByPolygon There can be only one mapping coordinate for the whole polygon.
			* One mapping per polygon polygon x has this normal x
			* For each vertex of the polygon then set the normal to x
		* ByEdge There will be one mapping coordinate for each unique edge in the mesh. This is meant to be used with smoothing layer elements. (Mapping is referencing the edge id)
		* AllSame There can be only one mapping coordinate for the whole surface.
	 */


	// None
	// ByVertice
	// ByPolygonVertex
	// ByPolygon
	// ByEdge
	// AllSame
	enum class MapType {
		none=0, // no mapping type
		vertex, // each mapping exists per vertex
		polygon_vertex, // per polygon vertex
		polygon, // per polygon
		edge, // maps per edge
		all_the_same // maps to everything
	};

	enum class ReferenceType {direct=0, index_to_direct=1};

	template<class T>
	struct MappingData
	{
		ReferenceType ref_type = ReferenceType::direct;
		MapType map_type = MapType::none;
		std::vector<T> data;
		std::vector<int> index;
	};

	std::vector<Vector3> &GetVertices() const;
	std::vector<int> &GetFaceIndices() const;
	MappingData<Vector3> &GetNormals() const;
	MappingData<Vector2> &Get_UV_0() const;
	MappingData<Vector2> &Get_UV_1() const;
	MappingData<Color> &GetVertexColors() const;
	MappingData<int> &GetMaterialAllocationID() const;
private:
	template <class T>
	MappingData<T> ResolveVertexDataArray(const Scope &source,
								const std::string &MappingInformationType,
								const std::string &ReferenceInformationType,
								const std::string &dataElementName);
	// read directly from the FBX file.
	std::vector<Vector3> m_vertices;
	std::vector<int> m_face_indices;
	MappingData<Vector3> m_normals;
	MappingData<Vector2> m_uv_0; // first uv coordinates
	MappingData<Vector2> m_uv_1; // second uv coordinates
	MappingData<Color> m_colors; // colors for the mesh
	MappingData<int> m_material_id; // slot of material used
};

/**
*  DOM class for FBX geometry of type "Shape"
*/
class ShapeGeometry : public Geometry {
public:
	/** The class constructor */
	ShapeGeometry(uint64_t id, const Element &element, const std::string &name, const Document &doc);

	/** The class destructor */
	virtual ~ShapeGeometry();

	/** Get a list of all vertex points, non-unique*/
	const std::vector<Vector3> &GetVertices() const;

	/** Get a list of all vertex normals or an empty array if
    *  no normals are specified. */
	const std::vector<Vector3> &GetNormals() const;

	/** Return list of vertex indices. */
	const std::vector<unsigned int> &GetIndices() const;

private:
	std::vector<Vector3> m_vertices;
	std::vector<Vector3> m_normals;
	std::vector<unsigned int> m_indices;
};
/**
*  DOM class for FBX geometry of type "Line"
*/
class LineGeometry : public Geometry {
public:
	/** The class constructor */
	LineGeometry(uint64_t id, const Element &element, const std::string &name, const Document &doc);

	/** The class destructor */
	virtual ~LineGeometry();

	/** Get a list of all vertex points, non-unique*/
	const std::vector<Vector3> &GetVertices() const;

	/** Return list of vertex indices. */
	const std::vector<int> &GetIndices() const;

private:
	std::vector<Vector3> m_vertices;
	std::vector<int> m_indices;
};

} // namespace FBX
} // namespace Assimp

#endif // INCLUDED_AI_FBX_MESHGEOMETRY_H
