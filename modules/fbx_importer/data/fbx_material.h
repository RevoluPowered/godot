/*************************************************************************/
/*  fbx_material.h					                                     */
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

#ifndef GODOT_FBX_MATERIAL_H
#define GODOT_FBX_MATERIAL_H

#include "thirdparty/assimp/code/FBX/FBXDocument.h"
#include "core/reference.h"
#include "core/ustring.h"
#include "modules/fbx_importer/tools/import_utils.h"
#include "scene/resources/material.h"

struct FBXMaterial : Reference {
protected:
	String material_name = String();
	mutable const Assimp::FBX::Material *material = nullptr;

public:

	String get_material_name() const
	{
		return material_name;
	}

	void set_imported_material(const Assimp::FBX::Material *p_material) {
		material = p_material;
	}

	static void add_search_string( String p_filename, String p_current_directory, String search_directory, Vector<String>& texture_search_paths)
	{
		if(search_directory.empty()) {
			texture_search_paths.push_back(p_current_directory.get_base_dir().plus_file(p_filename));
		} else {
			texture_search_paths.push_back(p_current_directory.get_base_dir().plus_file(search_directory + "/" + p_filename));
			texture_search_paths.push_back(p_current_directory.get_base_dir().plus_file("../" + search_directory + "/" + p_filename));
		}
	}

	// fbx will not give us good path information and let's not regex them to fix them
	// no relative paths are in fbx generally they have a rel field but it's populated incorrectly by the SDK.
	static String find_texture_path_by_filename( const String p_filename, const String p_current_directory )
	{
		_Directory dir;
		Vector<String> paths;
		add_search_string(p_filename, p_current_directory, "", paths);
		add_search_string(p_filename, p_current_directory, "texture", paths);
		add_search_string(p_filename, p_current_directory, "textures", paths);
		add_search_string(p_filename, p_current_directory, "materials", paths);
		add_search_string(p_filename, p_current_directory, "mats", paths);
		add_search_string(p_filename, p_current_directory, "pictures", paths);
		add_search_string(p_filename, p_current_directory, "images", paths);

		for (int i = 0; i < paths.size(); i++) {
			if (dir.file_exists(paths[i])) {
				return paths[i];
			}
		}

		return "";
	}

	/* Godot materials
	 *** Texture Maps:
	 * Albedo - color, texture
	 * Metallic - specular, metallic, texture
	 * Roughness - roughness, texture
	 * Emission - color, texture
	 * Normal Map - scale, texture
	 * Ambient Occlusion - texture
	 * Refraction - scale, texture
	 *** Has Settings for:
	 * UV1 - SCALE, OFFSET
	 * UV2 - SCALE, OFFSET
	 *** Flags for
	 * Transparent
	 * Cull Mode
	 */

	enum class MapMode {
		AlbedoM = 0,
		MetallicM,
		SpecularM,
		EmissionM,
		RoughnessM,
		NormalM,
		AmbientOcclusionM,
		RefractionM,
		ReflectionM,
	};

	const std::map<std::string, SpatialMaterial::TextureParam > fbx_mapping_paths = {
		    /* Diffuse */
			{"DiffuseColor", SpatialMaterial::TextureParam::TEXTURE_ALBEDO },
			{"Maya|DiffuseTexture", SpatialMaterial::TextureParam::TEXTURE_ALBEDO },
			{"Maya|baseColor|file", SpatialMaterial::TextureParam::TEXTURE_ALBEDO },
			{"3dsMax|Parameters|base_color_map", SpatialMaterial::TextureParam::TEXTURE_ALBEDO },
			{"Maya|TEX_color_map|file", SpatialMaterial::TextureParam::TEXTURE_ALBEDO },
			/* Emission */
			{"EmissiveColor", SpatialMaterial::TextureParam::TEXTURE_EMISSION },
			{"EmissiveFactor", SpatialMaterial::TextureParam::TEXTURE_EMISSION },
			{"Maya|emissionColor|file", SpatialMaterial::TextureParam::TEXTURE_EMISSION },
			{"3dsMax|Parameters|emission_map", SpatialMaterial::TextureParam::TEXTURE_EMISSION },
			{"Maya|TEX_emissive_map|file", SpatialMaterial::TextureParam::TEXTURE_EMISSION },
			/* Metallic */
			{"Maya|metalness|file", SpatialMaterial::TextureParam::TEXTURE_METALLIC },
			{"3dsMax|Parameters|metalness_map", SpatialMaterial::TextureParam::TEXTURE_METALLIC },
			{"Maya|TEX_metallic_map|file", SpatialMaterial::TextureParam::TEXTURE_METALLIC },
			{"SpecularColor", SpatialMaterial::TextureParam::TEXTURE_METALLIC },
			{"Maya|SpecularTexture", SpatialMaterial::TextureParam::TEXTURE_METALLIC },
			{"ShininessExponent", SpatialMaterial::TextureParam::TEXTURE_METALLIC },
            /* Roughness */
			{"Maya|diffuseRoughness|file", SpatialMaterial::TextureParam::TEXTURE_ROUGHNESS },
			{"3dsMax|Parameters|roughness_map", SpatialMaterial::TextureParam::TEXTURE_ROUGHNESS },
			{"Maya|TEX_roughness_map|file", SpatialMaterial::TextureParam::TEXTURE_ROUGHNESS },
		    /* Normal */
			{"NormalMap", SpatialMaterial::TextureParam::TEXTURE_NORMAL},
			{"Bump", SpatialMaterial::TextureParam::TEXTURE_NORMAL},
			{"3dsMax|Parameters|bump_map", SpatialMaterial::TextureParam::TEXTURE_NORMAL},
			{"Maya|NormalTexture", SpatialMaterial::TextureParam::TEXTURE_NORMAL},
			{"Maya|normalCamera|file", SpatialMaterial::TextureParam::TEXTURE_NORMAL},
			{"Maya|TEX_normal_map|file", SpatialMaterial::TextureParam::TEXTURE_NORMAL},
			/* AO */
			{"Maya|TEX_ao_map|file", SpatialMaterial::TextureParam::TEXTURE_AMBIENT_OCCLUSION},
		//	{"TransparentColor",SpatialMaterial::TextureParam::TEXTURE_CHANNEL_ALPHA },
		//	{"TransparencyFactor",SpatialMaterial::TextureParam::TEXTURE_CHANNEL_ALPHA }
	};

	struct TextureFileMapping : Reference {
		SpatialMaterial::TextureParam map_mode = SpatialMaterial::TEXTURE_ALBEDO;
		String name = String();
	};

	/* storing the texture properties like color */
	template <class T>
	struct TexturePropertyMapping : Reference {
		SpatialMaterial::TextureParam map_mode = SpatialMaterial::TextureParam::TEXTURE_ALBEDO;
		const T property = T();
	};

	// used for texture files - so you can map albedo,diffuse,etc
	Vector<Ref<TextureFileMapping>> extract_texture_mappings( const Assimp::FBX::Material *material ) const {
		Vector<Ref<TextureFileMapping>> mappings = Vector<Ref<TextureFileMapping>>();

		for (std::pair<std::string, const Assimp::FBX::Texture *> texture : material->Textures()) {
			String texture_name = ImportUtils::FBXNodeToName(texture.second->Name());
			const std::string& fbx_mapping_name = texture.first;

			if(fbx_mapping_paths.count(fbx_mapping_name) > 0)
			{
				const SpatialMaterial::TextureParam& mapping_mode = fbx_mapping_paths.at(fbx_mapping_name);
				Ref<TextureFileMapping> file_mapping;
				file_mapping.instance();
				file_mapping->map_mode = mapping_mode;
				file_mapping->name = texture_name;
				mappings.push_back(file_mapping);
			}
		}

		// does anyone use this?
		for (std::pair<std::string, const Assimp::FBX::LayeredTexture *> layer_textures : material->LayeredTextures()) {
			std::string texture_name = layer_textures.second->Name();
			const std::string& fbx_mapping_name = layer_textures.first;

			if(fbx_mapping_paths.count(fbx_mapping_name) > 0)
			{
				const SpatialMaterial::TextureParam& mapping_mode = fbx_mapping_paths.at(fbx_mapping_name);
				Ref<TextureFileMapping> file_mapping;
				file_mapping.instance();
				file_mapping->map_mode = mapping_mode;
				file_mapping->name = String(texture_name.c_str());
				mappings.push_back(file_mapping);
			}
		}

		return mappings;
	}

	// used for single properties like metalic/specularintensity/energy/scale
	Vector<TexturePropertyMapping<float> > extract_float_properties() const;

	// used for vertex color mappings
	Vector<TexturePropertyMapping<Color> > extract_color_properties() const;

	Ref<SpatialMaterial> import_material(ImportState& state) {
		const String p_fbx_current_directory = state.path;
		ERR_FAIL_COND_V(material == nullptr, nullptr);
		Ref<SpatialMaterial> spatial_material;
		spatial_material.instance();

		// read the material file
		// is material two sided
		// read material name
		print_verbose("[material] material name: " + ImportUtils::FBXNodeToName(material->Name()));
		material_name = ImportUtils::FBXNodeToName(material->Name());

		// allocate texture mappings
		const Vector<Ref<TextureFileMapping>> texture_mappings = extract_texture_mappings(material);

		for( int x = 0; x < texture_mappings.size(); x++) {
			Ref<TextureFileMapping> mapping = texture_mappings.get(x);
			Ref<Texture> texture;
			print_verbose("texture mapping name: " + mapping->name);

			if(state.cached_image_searches.has(mapping->name))
			{
				texture = state.cached_image_searches[mapping->name];
			}
			else
			{
				print_verbose("material mapping name: " + mapping->name);
				String path = find_texture_path_by_filename(mapping->name, p_fbx_current_directory);
				if(!path.empty())
				{
					Ref<Image> image;
					image.instance();
					Ref<ImageTexture> image_texture;

					if(ImageLoader::load_image(path, image) == OK)
					{
						image_texture.instance();
						image_texture->create_from_image(image);
						texture = image_texture;
						state.cached_image_searches[mapping->name] = texture;

						int32_t flags = Texture::FLAGS_DEFAULT;
						texture->set_flags(flags);

						print_verbose("created texture for loaded image file");
					}
					else
					{
						ERR_CONTINUE_MSG(true, "unable to import image file not loaded yet TODO");
					}
				}
			}
			spatial_material->set_texture(mapping->map_mode, texture);
		}

		spatial_material->set_name(material_name);

		return spatial_material;
	}
};

#endif // GODOT_FBX_MATERIAL_H
